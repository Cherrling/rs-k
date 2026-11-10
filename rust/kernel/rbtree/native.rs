// SPDX-License-Identifier: GPL-2.0

//! Native implementation of the intrusive red-black tree operations.
//!
//! The C entry points in `lib/rbtree.c` are retained as ABI adapters so existing callers keep the
//! same exported symbols. All tree traversal and rebalancing execute here when
//! `CONFIG_RUST_RBTREE` is enabled.

use crate::bindings;
use core::ptr;
use macros::export;

type Node = bindings::rb_node;
type Root = bindings::rb_root;
type RootLinked = bindings::rb_root_linked;
type NodeLinked = bindings::rb_node_linked;
type Callbacks = bindings::rb_augment_callbacks;
type RotateFn = Option<unsafe extern "C" fn(old: *mut Node, new: *mut Node)>;

const RB_RED: usize = 0;
const RB_BLACK: usize = 1;

/// Writes a node pointer with `WRITE_ONCE()` semantics.
///
/// # Safety
///
/// `slot` must point to a writable, aligned node pointer. `value` must be null or point to a
/// valid node.
#[inline]
unsafe fn write_pointer(slot: *mut *mut Node, value: *mut Node) {
    // SAFETY: The caller guarantees that `slot` is a valid, aligned pointer to a node pointer.
    unsafe { bindings::atomic_ptr_set(slot.cast(), value.cast()) };
}

#[inline]
fn parent_of_pc(pc: usize) -> *mut Node {
    (pc & !3) as *mut Node
}

/// Decodes the parent field of a node.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`.
#[inline]
unsafe fn parent_node(node: *mut Node) -> *mut Node {
    // SAFETY: The caller guarantees that `node` is valid.
    unsafe { parent_of_pc((*node).__rb_parent_color) }
}

#[inline]
fn is_black_pc(pc: usize) -> bool {
    (pc & RB_BLACK) != 0
}

/// Reads the color of a node.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`.
#[inline]
unsafe fn is_red(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees that `node` is valid.
    !is_black_pc(unsafe { (*node).__rb_parent_color })
}

/// Reads the color of a node.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`.
#[inline]
unsafe fn is_black(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees that `node` is valid.
    is_black_pc(unsafe { (*node).__rb_parent_color })
}

/// Colors a node black without changing its parent.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`.
#[inline]
unsafe fn set_black(node: *mut Node) {
    // SAFETY: The caller guarantees that `node` is valid. This matches `rb_set_black()`.
    unsafe { (*node).__rb_parent_color += RB_BLACK };
}

/// Sets a node's parent without changing its color.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`. `new_parent` must be null or point to a
/// valid, aligned `rb_node`.
#[inline]
unsafe fn set_parent(node: *mut Node, new_parent: *mut Node) {
    // SAFETY: The caller guarantees that `node` is valid.
    unsafe {
        (*node).__rb_parent_color = ((*node).__rb_parent_color & RB_BLACK) + new_parent as usize;
    }
}

/// Sets a node's parent and color.
///
/// # Safety
///
/// `node` must point to a valid, initialized `rb_node`. `new_parent` must be null or point to a
/// valid, aligned `rb_node`. `color` must be `RB_RED` or `RB_BLACK`.
#[inline]
unsafe fn set_parent_color(node: *mut Node, new_parent: *mut Node, color: usize) {
    // SAFETY: The caller guarantees that `node` is valid.
    unsafe { (*node).__rb_parent_color = new_parent as usize + color };
}

/// Replaces the child pointer from `parent`, or the root pointer, that refers to `old`.
///
/// # Safety
///
/// `old` and `new` must be null or point to valid nodes. `parent` must be null or the valid parent
/// of `old`. `root` must point to the valid tree root that contains `old`.
#[inline]
unsafe fn change_child(old: *mut Node, new: *mut Node, parent: *mut Node, root: *mut Root) {
    if parent.is_null() {
        // SAFETY: The caller guarantees that `root` is valid.
        unsafe { write_pointer(ptr::addr_of_mut!((*root).rb_node), new) };
    } else {
        // SAFETY: The caller guarantees that `parent` is valid and is the parent of `old`.
        if unsafe { (*parent).rb_left == old } {
            // SAFETY: `rb_left` is a valid node pointer field in `parent`.
            unsafe { write_pointer(ptr::addr_of_mut!((*parent).rb_left), new) };
        } else {
            // SAFETY: `rb_right` is a valid node pointer field in `parent`.
            unsafe { write_pointer(ptr::addr_of_mut!((*parent).rb_right), new) };
        }
    }
}

/// Replaces a child pointer in an RCU-protected tree.
///
/// # Safety
///
/// The pointer relationships and RCU protection required by `__rb_change_child_rcu()` must hold.
#[inline]
unsafe fn change_child_rcu(old: *mut Node, new: *mut Node, parent: *mut Node, root: *mut Root) {
    // SAFETY: The caller guarantees the same pointer relationships as `__rb_change_child_rcu()`.
    unsafe { bindings::rb_change_child_rcu(old, new, parent, root) };
}

/// Rotates `new` above `old` and transfers the parent relationship.
///
/// # Safety
///
/// `old` and `new` must be valid nodes in `root` that are in a parent-child relationship suitable
/// for the requested rotation. `color` must be `RB_RED` or `RB_BLACK`.
#[inline]
unsafe fn rotate_set_parents(old: *mut Node, new: *mut Node, root: *mut Root, color: usize) {
    // SAFETY: The caller guarantees that `old` and `new` are valid nodes in `root`.
    let parent = unsafe { parent_node(old) };
    // SAFETY: See the comment above.
    unsafe {
        (*new).__rb_parent_color = (*old).__rb_parent_color;
        set_parent_color(old, new, color);
        change_child(old, new, parent, root);
    }
}

/// Invokes an optional augmentation rotation callback.
///
/// # Safety
///
/// If `rotate` is present, it must be callable with `old` and `new`, and both nodes must remain
/// valid for the duration of the call.
#[inline]
unsafe fn call_rotate(rotate: RotateFn, old: *mut Node, new: *mut Node) {
    if let Some(rotate) = rotate {
        // SAFETY: The caller guarantees that the callback accepts valid nodes from this rotation.
        unsafe { rotate(old, new) };
    }
}

/// Restores red-black invariants after linking a node.
///
/// # Safety
///
/// `node` must already be linked into the valid tree `root`. Callers must serialize modifications
/// to the tree as required by the existing C API. If present, `rotate` must satisfy the augmented
/// rbtree callback contract.
unsafe fn insert(node: *mut Node, root: *mut Root, rotate: RotateFn) {
    // SAFETY: The caller guarantees that `node` is linked into `root`.
    let mut parent = unsafe { parent_node(node) };
    let mut node = node;

    loop {
        if parent.is_null() {
            // SAFETY: `node` is valid and becomes the root.
            unsafe { (*node).__rb_parent_color = RB_BLACK };
            break;
        }

        // SAFETY: `parent` is a valid node in the tree.
        if unsafe { is_black(parent) } {
            break;
        }

        // A red node stores its parent directly in `__rb_parent_color`.
        // SAFETY: `parent` is a valid red node in the tree.
        let gparent = unsafe { parent_node(parent) };
        // SAFETY: `gparent` is valid and is the grandparent of `node`.
        let right = unsafe { (*gparent).rb_right };
        if parent != right {
            // SAFETY: `right` is the non-null uncle of `node`.
            if !right.is_null() && unsafe { is_red(right) } {
                // SAFETY: All nodes involved are valid.
                unsafe {
                    set_parent_color(right, gparent, RB_BLACK);
                    set_parent_color(parent, gparent, RB_BLACK);
                }
                node = gparent;
                // SAFETY: `node` is valid and currently red exactly when this cast is valid.
                parent = unsafe { parent_node(node) };
                // SAFETY: `node` is valid.
                unsafe { set_parent_color(node, parent, RB_RED) };
                continue;
            }

            // SAFETY: `parent` is valid.
            let mut tmp = unsafe { (*parent).rb_right };
            if node == tmp {
                // SAFETY: `node` is valid.
                tmp = unsafe { (*node).rb_left };
                // SAFETY: The pointer relationships match the C implementation.
                unsafe {
                    write_pointer(ptr::addr_of_mut!((*parent).rb_right), tmp);
                    write_pointer(ptr::addr_of_mut!((*node).rb_left), parent);
                    if !tmp.is_null() {
                        set_parent_color(tmp, parent, RB_BLACK);
                    }
                    set_parent_color(parent, node, RB_RED);
                    call_rotate(rotate, parent, node);
                }
                parent = node;
                // SAFETY: `node` is valid.
                tmp = unsafe { (*node).rb_right };
            }

            // SAFETY: The pointer relationships match the C implementation.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*gparent).rb_left), tmp);
                write_pointer(ptr::addr_of_mut!((*parent).rb_right), gparent);
                if !tmp.is_null() {
                    set_parent_color(tmp, gparent, RB_BLACK);
                }
                rotate_set_parents(gparent, parent, root, RB_RED);
                call_rotate(rotate, gparent, parent);
            }
            break;
        } else {
            // SAFETY: `gparent` is valid.
            let mut tmp = unsafe { (*gparent).rb_left };
            // SAFETY: `tmp` is the non-null uncle of `node`.
            if !tmp.is_null() && unsafe { is_red(tmp) } {
                // SAFETY: All nodes involved are valid.
                unsafe {
                    set_parent_color(tmp, gparent, RB_BLACK);
                    set_parent_color(parent, gparent, RB_BLACK);
                }
                node = gparent;
                // SAFETY: `node` is valid and currently red exactly when this cast is valid.
                parent = unsafe { parent_node(node) };
                // SAFETY: `node` is valid.
                unsafe { set_parent_color(node, parent, RB_RED) };
                continue;
            }

            // SAFETY: `parent` is valid.
            tmp = unsafe { (*parent).rb_left };
            if node == tmp {
                // SAFETY: `node` is valid.
                tmp = unsafe { (*node).rb_right };
                // SAFETY: The pointer relationships match the C implementation.
                unsafe {
                    write_pointer(ptr::addr_of_mut!((*parent).rb_left), tmp);
                    write_pointer(ptr::addr_of_mut!((*node).rb_right), parent);
                    if !tmp.is_null() {
                        set_parent_color(tmp, parent, RB_BLACK);
                    }
                    set_parent_color(parent, node, RB_RED);
                    call_rotate(rotate, parent, node);
                }
                parent = node;
                // SAFETY: `node` is valid.
                tmp = unsafe { (*node).rb_left };
            }

            // SAFETY: The pointer relationships match the C implementation.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*gparent).rb_right), tmp);
                write_pointer(ptr::addr_of_mut!((*parent).rb_left), gparent);
                if !tmp.is_null() {
                    set_parent_color(tmp, gparent, RB_BLACK);
                }
                rotate_set_parents(gparent, parent, root, RB_RED);
                call_rotate(rotate, gparent, parent);
            }
            break;
        }
    }
}

/// Restores red-black invariants after removing a black node.
///
/// # Safety
///
/// `parent` must identify the valid subtree that lost one black node, `root` must be its valid
/// tree root, and callers must serialize tree modifications. If present, `rotate` must satisfy the
/// augmented rbtree callback contract.
unsafe fn erase_color(mut parent: *mut Node, root: *mut Root, rotate: RotateFn) {
    let mut node = ptr::null_mut();

    loop {
        // SAFETY: `parent` is a valid node.
        let right = unsafe { (*parent).rb_right };
        if node != right {
            let mut sibling = right;
            // SAFETY: `sibling` is valid.
            if unsafe { is_red(sibling) } {
                // SAFETY: `sibling` is valid.
                let tmp1 = unsafe { (*sibling).rb_left };
                // SAFETY: These are the case 1 pointer updates from the C implementation.
                unsafe {
                    write_pointer(ptr::addr_of_mut!((*parent).rb_right), tmp1);
                    write_pointer(ptr::addr_of_mut!((*sibling).rb_left), parent);
                    set_parent_color(tmp1, parent, RB_BLACK);
                    rotate_set_parents(parent, sibling, root, RB_RED);
                    call_rotate(rotate, parent, sibling);
                }
                sibling = tmp1;
            }

            // SAFETY: `sibling` is valid.
            let mut tmp1 = unsafe { (*sibling).rb_right };
            // SAFETY: `tmp1` is the non-null right child of `sibling`.
            if tmp1.is_null() || unsafe { is_black(tmp1) } {
                // SAFETY: `sibling` is valid.
                let tmp2 = unsafe { (*sibling).rb_left };
                // SAFETY: `tmp2` is the non-null left child of `sibling`.
                if tmp2.is_null() || unsafe { is_black(tmp2) } {
                    // SAFETY: `sibling` and `parent` are valid.
                    unsafe { set_parent_color(sibling, parent, RB_RED) };
                    // SAFETY: `parent` is valid.
                    if unsafe { is_red(parent) } {
                        // SAFETY: `parent` is valid.
                        unsafe { set_black(parent) };
                    } else {
                        node = parent;
                        // SAFETY: `node` is valid.
                        parent = unsafe { parent_node(node) };
                        if !parent.is_null() {
                            continue;
                        }
                    }
                    break;
                }

                // SAFETY: `tmp2` is valid.
                tmp1 = unsafe { (*tmp2).rb_right };
                // SAFETY: These are the case 3 pointer updates from the C implementation.
                unsafe {
                    write_pointer(ptr::addr_of_mut!((*sibling).rb_left), tmp1);
                    write_pointer(ptr::addr_of_mut!((*tmp2).rb_right), sibling);
                    write_pointer(ptr::addr_of_mut!((*parent).rb_right), tmp2);
                    if !tmp1.is_null() {
                        set_parent_color(tmp1, sibling, RB_BLACK);
                    }
                    call_rotate(rotate, sibling, tmp2);
                }
                tmp1 = sibling;
                sibling = tmp2;
            }

            // SAFETY: `sibling` is valid.
            let tmp2 = unsafe { (*sibling).rb_left };
            // SAFETY: These are the case 4 pointer updates from the C implementation.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*parent).rb_right), tmp2);
                write_pointer(ptr::addr_of_mut!((*sibling).rb_left), parent);
                set_parent_color(tmp1, sibling, RB_BLACK);
                if !tmp2.is_null() {
                    set_parent(tmp2, parent);
                }
                rotate_set_parents(parent, sibling, root, RB_BLACK);
                call_rotate(rotate, parent, sibling);
            }
            break;
        }

        // SAFETY: `parent` is valid.
        let mut sibling = unsafe { (*parent).rb_left };
        // SAFETY: `sibling` is valid.
        if unsafe { is_red(sibling) } {
            // SAFETY: `sibling` is valid.
            let tmp1 = unsafe { (*sibling).rb_right };
            // SAFETY: These are the mirrored case 1 pointer updates.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*parent).rb_left), tmp1);
                write_pointer(ptr::addr_of_mut!((*sibling).rb_right), parent);
                set_parent_color(tmp1, parent, RB_BLACK);
                rotate_set_parents(parent, sibling, root, RB_RED);
                call_rotate(rotate, parent, sibling);
            }
            sibling = tmp1;
        }

        // SAFETY: `sibling` is valid.
        let mut tmp1 = unsafe { (*sibling).rb_left };
        // SAFETY: `tmp1` is the non-null left child of `sibling`.
        if tmp1.is_null() || unsafe { is_black(tmp1) } {
            // SAFETY: `sibling` is valid.
            let tmp2 = unsafe { (*sibling).rb_right };
            // SAFETY: `tmp2` is the non-null right child of `sibling`.
            if tmp2.is_null() || unsafe { is_black(tmp2) } {
                // SAFETY: `sibling` and `parent` are valid.
                unsafe { set_parent_color(sibling, parent, RB_RED) };
                // SAFETY: `parent` is valid.
                if unsafe { is_red(parent) } {
                    // SAFETY: `parent` is valid.
                    unsafe { set_black(parent) };
                } else {
                    node = parent;
                    // SAFETY: `node` is valid.
                    parent = unsafe { parent_node(node) };
                    if !parent.is_null() {
                        continue;
                    }
                }
                break;
            }

            // SAFETY: `tmp2` is valid.
            tmp1 = unsafe { (*tmp2).rb_left };
            // SAFETY: These are the mirrored case 3 pointer updates.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*sibling).rb_right), tmp1);
                write_pointer(ptr::addr_of_mut!((*tmp2).rb_left), sibling);
                write_pointer(ptr::addr_of_mut!((*parent).rb_left), tmp2);
                if !tmp1.is_null() {
                    set_parent_color(tmp1, sibling, RB_BLACK);
                }
                call_rotate(rotate, sibling, tmp2);
            }
            tmp1 = sibling;
            sibling = tmp2;
        }

        // SAFETY: `sibling` is valid.
        let tmp2 = unsafe { (*sibling).rb_right };
        // SAFETY: These are the mirrored case 4 pointer updates.
        unsafe {
            write_pointer(ptr::addr_of_mut!((*parent).rb_left), tmp2);
            write_pointer(ptr::addr_of_mut!((*sibling).rb_right), parent);
            set_parent_color(tmp1, sibling, RB_BLACK);
            if !tmp2.is_null() {
                set_parent(tmp2, parent);
            }
            rotate_set_parents(parent, sibling, root, RB_BLACK);
            call_rotate(rotate, parent, sibling);
        }
        break;
    }
}

/// Invokes an optional augmentation propagation callback.
///
/// # Safety
///
/// `callbacks` must be null or point to valid callbacks with a non-null `propagate` function.
/// `node` and `stop` must satisfy that callback's contract.
#[inline]
unsafe fn call_propagate(callbacks: *const Callbacks, node: *mut Node, stop: *mut Node) {
    if !callbacks.is_null() {
        // SAFETY: The augmented API contract requires non-null callbacks.
        unsafe { ((*callbacks).propagate.unwrap_unchecked())(node, stop) };
    }
}

/// Invokes an optional augmentation copy callback.
///
/// # Safety
///
/// `callbacks` must be null or point to valid callbacks with a non-null `copy` function. `old` and
/// `new` must satisfy that callback's contract.
#[inline]
unsafe fn call_copy(callbacks: *const Callbacks, old: *mut Node, new: *mut Node) {
    if !callbacks.is_null() {
        // SAFETY: The augmented API contract requires non-null callbacks.
        unsafe { ((*callbacks).copy.unwrap_unchecked())(old, new) };
    }
}

/// Removes a node and maintains augmented subtree state.
///
/// # Safety
///
/// `node` must be linked in the valid tree `root`. `callbacks` must be null or satisfy the
/// augmented rbtree callback contract. Callers must serialize tree modifications.
unsafe fn erase_augmented_inner(
    node: *mut Node,
    root: *mut Root,
    callbacks: *const Callbacks,
) -> *mut Node {
    // SAFETY: `node` is valid.
    let child = unsafe { (*node).rb_right };
    // SAFETY: `node` is valid.
    let mut tmp = unsafe { (*node).rb_left };
    let rebalance;
    let propagate_start;

    if tmp.is_null() {
        // SAFETY: `node` is valid.
        let pc = unsafe { (*node).__rb_parent_color };
        let parent = parent_of_pc(pc);
        // SAFETY: `node` is linked in `root`.
        unsafe { change_child(node, child, parent, root) };
        if !child.is_null() {
            // SAFETY: `child` is valid.
            unsafe { (*child).__rb_parent_color = pc };
            rebalance = ptr::null_mut();
        } else {
            rebalance = if is_black_pc(pc) {
                parent
            } else {
                ptr::null_mut()
            };
        }
        propagate_start = parent;
    } else if child.is_null() {
        // SAFETY: `tmp` is valid.
        let pc = unsafe { (*node).__rb_parent_color };
        // SAFETY: `tmp` is valid.
        unsafe { (*tmp).__rb_parent_color = pc };
        let parent = parent_of_pc(pc);
        // SAFETY: `node` is linked in `root`.
        unsafe { change_child(node, tmp, parent, root) };
        rebalance = ptr::null_mut();
        propagate_start = parent;
    } else {
        let mut successor = child;
        // SAFETY: `successor` is valid.
        tmp = unsafe { (*successor).rb_left };
        let child2;
        let mut parent;

        if tmp.is_null() {
            parent = successor;
            // SAFETY: `successor` is valid.
            child2 = unsafe { (*successor).rb_right };
            // SAFETY: `node` and `successor` are valid.
            unsafe { call_copy(callbacks, node, successor) };
        } else {
            loop {
                parent = successor;
                successor = tmp;
                // SAFETY: `successor` is valid.
                tmp = unsafe { (*successor).rb_left };
                if tmp.is_null() {
                    break;
                }
            }
            // SAFETY: `successor` is valid.
            child2 = unsafe { (*successor).rb_right };
            // SAFETY: These are the case 3 pointer updates from the C implementation.
            unsafe {
                write_pointer(ptr::addr_of_mut!((*parent).rb_left), child2);
                write_pointer(ptr::addr_of_mut!((*successor).rb_right), child);
                set_parent(child, successor);
                call_copy(callbacks, node, successor);
                call_propagate(callbacks, parent, successor);
            }
        }

        // SAFETY: `node` is valid.
        tmp = unsafe { (*node).rb_left };
        // SAFETY: The pointer relationships match the C implementation.
        unsafe {
            write_pointer(ptr::addr_of_mut!((*successor).rb_left), tmp);
            set_parent(tmp, successor);
        }

        // SAFETY: `node` is valid.
        let pc = unsafe { (*node).__rb_parent_color };
        tmp = parent_of_pc(pc);
        // SAFETY: `node` is linked in `root`.
        unsafe { change_child(node, successor, tmp, root) };

        if !child2.is_null() {
            // SAFETY: `child2` is valid.
            unsafe { set_parent_color(child2, parent, RB_BLACK) };
            rebalance = ptr::null_mut();
        } else {
            // SAFETY: `successor` is valid.
            rebalance = if unsafe { is_black(successor) } {
                parent
            } else {
                ptr::null_mut()
            };
        }
        // SAFETY: `successor` is valid.
        unsafe { (*successor).__rb_parent_color = pc };
        propagate_start = successor;
    }

    // SAFETY: All pointers passed to the callback are valid for the completed operation.
    unsafe { call_propagate(callbacks, propagate_start, ptr::null_mut()) };
    rebalance
}

/// Removes a node and performs any required rebalancing.
///
/// # Safety
///
/// `node` must be linked in the valid tree `root`. `callbacks` must be null or satisfy the
/// augmented rbtree callback contract. Callers must serialize tree modifications.
unsafe fn erase_inner(node: *mut Node, root: *mut Root, callbacks: *const Callbacks) {
    // SAFETY: `node` is linked in `root`.
    let rebalance = unsafe { erase_augmented_inner(node, root, callbacks) };
    if !rebalance.is_null() {
        let rotate = if callbacks.is_null() {
            None
        } else {
            // SAFETY: The augmented API contract requires non-null callbacks.
            unsafe { (*callbacks).rotate }
        };
        // SAFETY: `rebalance` identifies the subtree that needs rebalancing.
        unsafe { erase_color(rebalance, root, rotate) };
    }
}

/// Inserts `node` into `root` and restores the red-black tree invariants.
///
/// # Safety
///
/// `node` must already be linked into `root` with `rb_link_node()`, and callers must serialize
/// modifications to the tree as required by the existing C API.
#[export]
unsafe extern "C" fn rust_rb_insert_color(node: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { insert(node, root, None) };
}

/// Inserts an augmented `node` and invokes the rotation callback for each rotation.
///
/// # Safety
///
/// The contracts of `rb_insert_augmented()` apply. `augment_rotate` must be callable for nodes in
/// this tree.
#[export]
unsafe extern "C" fn rust_rb_insert_augmented(
    node: *mut Node,
    root: *mut Root,
    augment_rotate: RotateFn,
) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { insert(node, root, augment_rotate) };
}

/// Restores invariants after removing a black node.
///
/// # Safety
///
/// The contracts of `__rb_erase_color()` apply.
#[export]
unsafe extern "C" fn rust_rb_erase_color(
    parent: *mut Node,
    root: *mut Root,
    augment_rotate: RotateFn,
) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { erase_color(parent, root, augment_rotate) };
}

/// Removes `node` from `root`.
///
/// # Safety
///
/// The contracts of `rb_erase()` apply.
#[export]
unsafe extern "C" fn rust_rb_erase(node: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { erase_inner(node, root, ptr::null()) };
}

/// Removes an augmented `node` from `root`.
///
/// # Safety
///
/// The contracts of `rb_erase_augmented()` apply. `augment` must point to valid callbacks.
#[export]
unsafe extern "C" fn rust_rb_erase_augmented(
    node: *mut Node,
    root: *mut Root,
    augment: *const Callbacks,
) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { erase_inner(node, root, augment) };
}

/// Removes a node from a linked tree.
///
/// # Safety
///
/// The contracts of `rb_erase_linked()` apply.
#[export]
unsafe extern "C" fn rust_rb_erase_linked(node: *mut NodeLinked, root: *mut RootLinked) -> bool {
    // SAFETY: The caller guarantees both pointers are valid.
    unsafe {
        if (*node).prev.is_null() {
            (*root).rb_leftmost = (*node).next;
        } else {
            (*(*node).prev).next = (*node).next;
        }

        if !(*node).next.is_null() {
            (*(*node).next).prev = (*node).prev;
        }

        erase_inner(
            ptr::addr_of_mut!((*node).node),
            ptr::addr_of_mut!((*root).rb_root),
            ptr::null(),
        );

        (*node).node.__rb_parent_color = node as usize;
        (*node).prev = ptr::null_mut();
        (*node).next = ptr::null_mut();
        !(*root).rb_leftmost.is_null()
    }
}

/// Returns the next node in sort order.
///
/// # Safety
///
/// `node` must point to a valid node or an empty node.
#[export]
unsafe extern "C" fn rust_rb_next(node: *const Node) -> *mut Node {
    let mut node = node;
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { (*node).__rb_parent_color == node as usize } {
        return ptr::null_mut();
    }

    // SAFETY: `node` is valid.
    if !unsafe { (*node).rb_right }.is_null() {
        // SAFETY: `node` has a valid right child.
        node = unsafe { (*node).rb_right };
        // SAFETY: Every non-null child is a valid node.
        while !unsafe { (*node).rb_left }.is_null() {
            // SAFETY: Every non-null child is a valid node.
            node = unsafe { (*node).rb_left };
        }
        return node.cast_mut();
    }

    // SAFETY: `node` is valid.
    let mut parent = unsafe { parent_node(node.cast_mut()) };
    while !parent.is_null() {
        // SAFETY: `parent` is valid and non-null.
        if node != unsafe { (*parent).rb_right } {
            break;
        }
        node = parent;
        // SAFETY: `node` is valid.
        parent = unsafe { parent_node(node.cast_mut()) };
    }
    parent
}

/// Returns the previous node in sort order.
///
/// # Safety
///
/// `node` must point to a valid node or an empty node.
#[export]
unsafe extern "C" fn rust_rb_prev(node: *const Node) -> *mut Node {
    let mut node = node;
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { (*node).__rb_parent_color == node as usize } {
        return ptr::null_mut();
    }

    // SAFETY: `node` is valid.
    if !unsafe { (*node).rb_left }.is_null() {
        // SAFETY: `node` has a valid left child.
        node = unsafe { (*node).rb_left };
        // SAFETY: Every non-null child is a valid node.
        while !unsafe { (*node).rb_right }.is_null() {
            // SAFETY: Every non-null child is a valid node.
            node = unsafe { (*node).rb_right };
        }
        return node.cast_mut();
    }

    // SAFETY: `node` is valid.
    let mut parent = unsafe { parent_node(node.cast_mut()) };
    while !parent.is_null() {
        // SAFETY: `parent` is valid and non-null.
        if node != unsafe { (*parent).rb_left } {
            break;
        }
        node = parent;
        // SAFETY: `node` is valid.
        parent = unsafe { parent_node(node.cast_mut()) };
    }
    parent
}

/// Replaces `victim` with `new` without rebalancing.
///
/// # Safety
///
/// The contracts of `rb_replace_node()` apply.
#[export]
unsafe extern "C" fn rust_rb_replace_node(victim: *mut Node, new: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe {
        let parent = parent_node(victim);
        ptr::copy(victim, new, 1);
        if !(*victim).rb_left.is_null() {
            set_parent((*victim).rb_left, new);
        }
        if !(*victim).rb_right.is_null() {
            set_parent((*victim).rb_right, new);
        }
        change_child(victim, new, parent, root);
    }
}

/// Replaces `victim` with `new` for an RCU-protected tree.
///
/// # Safety
///
/// The contracts of `rb_replace_node_rcu()` apply.
#[export]
unsafe extern "C" fn rust_rb_replace_node_rcu(victim: *mut Node, new: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe {
        let parent = parent_node(victim);
        ptr::copy(victim, new, 1);
        if !(*victim).rb_left.is_null() {
            set_parent((*victim).rb_left, new);
        }
        if !(*victim).rb_right.is_null() {
            set_parent((*victim).rb_right, new);
        }
        change_child_rcu(victim, new, parent, root);
    }
}

/// Returns the deepest node reachable by preferring left children.
///
/// # Safety
///
/// `node` must point to a valid, non-null node in a valid tree.
#[inline]
unsafe fn left_deepest(node: *const Node) -> *mut Node {
    let mut node = node;
    loop {
        // SAFETY: The caller guarantees that `node` is valid.
        if !unsafe { (*node).rb_left }.is_null() {
            // SAFETY: The child is a valid node.
            node = unsafe { (*node).rb_left };
        // SAFETY: The caller guarantees that `node` is valid.
        } else if !unsafe { (*node).rb_right }.is_null() {
            // SAFETY: The child is a valid node.
            node = unsafe { (*node).rb_right };
        } else {
            return node.cast_mut();
        }
    }
}

/// Returns the next node in postorder.
///
/// # Safety
///
/// `node` must be null or point to a valid node.
#[export]
unsafe extern "C" fn rust_rb_next_postorder(node: *const Node) -> *mut Node {
    if node.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: Guaranteed by this function's contract.
    let parent = unsafe { parent_node(node.cast_mut()) };
    if parent.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: `parent` is valid and non-null.
    let parent_left = unsafe { (*parent).rb_left };
    // SAFETY: `parent` is valid and non-null.
    let parent_right = unsafe { (*parent).rb_right };
    if node == parent_left && !parent_right.is_null() {
        // SAFETY: The right child is valid.
        return unsafe { left_deepest(parent_right) };
    }
    parent
}

/// Returns the first node in postorder.
///
/// # Safety
///
/// `root` must point to a valid tree root.
#[export]
unsafe extern "C" fn rust_rb_first_postorder(root: *const Root) -> *mut Node {
    // SAFETY: Guaranteed by this function's contract.
    let node = unsafe { (*root).rb_node };
    if node.is_null() {
        ptr::null_mut()
    } else {
        // SAFETY: `node` is a valid root node.
        unsafe { left_deepest(node) }
    }
}
