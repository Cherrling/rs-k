// SPDX-License-Identifier: GPL-2.0

//! Native interval tree operations.
//!
//! Rust owns interval insertion, augmented-subtree maintenance, interval
//! search, overlap iteration, and used or hole span iteration. C retains the
//! intrusive layouts and the narrow augmented-rbtree primitives.

use crate::{bindings, container_of};
use core::ptr;
use macros::export;

type Endpoint = usize;
type Node = bindings::interval_tree_node;
type RbNode = bindings::rb_node;
type Root = bindings::rb_root_cached;
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
type SpanIter = bindings::interval_tree_span_iter;

/// Returns the node that contains an embedded red-black-tree link.
///
/// # Safety
///
/// `rb` must point to the `rb` member of an interval tree node.
#[inline]
unsafe fn node_from_rb(rb: *mut RbNode) -> *mut Node {
    // SAFETY: The caller guarantees that `rb` is embedded in a node.
    unsafe { container_of!(rb, Node, rb) }
}

/// Returns the red-black-tree link of an interval tree node.
///
/// # Safety
///
/// `node` must point to a valid interval tree node.
#[inline]
unsafe fn rb_of_node(node: *mut Node) -> *mut RbNode {
    // SAFETY: The caller guarantees a valid node.
    unsafe { ptr::addr_of_mut!((*node).rb) }
}

/// Returns the parent encoded in a red-black-tree node.
///
/// # Safety
///
/// `rb` must point to a valid red-black-tree node.
#[inline]
unsafe fn parent(rb: *mut RbNode) -> *mut RbNode {
    // SAFETY: The caller guarantees a valid node.
    ((unsafe { (*rb).__rb_parent_color }) & !3) as *mut RbNode
}

/// Returns the current span start or end, which share one union.
///
/// # Safety
///
/// `iter` must contain a valid span value.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[inline]
unsafe fn span_start(iter: *mut SpanIter) -> Endpoint {
    // SAFETY: The caller guarantees a live iterator.
    unsafe { (*iter).__bindgen_anon_1.start_hole }
}

/// Sets the current span start or end, which share one union.
///
/// # Safety
///
/// `iter` must contain a live span value.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[inline]
unsafe fn set_span_start(iter: *mut SpanIter, value: Endpoint) {
    // SAFETY: The caller guarantees a live iterator.
    unsafe { (*iter).__bindgen_anon_1.start_hole = value };
}

/// Returns the current span end, which shares a union with the used end.
///
/// # Safety
///
/// `iter` must contain a valid span value.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[inline]
unsafe fn span_last(iter: *mut SpanIter) -> Endpoint {
    // SAFETY: The caller guarantees a live iterator.
    unsafe { (*iter).__bindgen_anon_2.last_hole }
}

/// Sets the current span end, which shares a union with the used end.
///
/// # Safety
///
/// `iter` must contain a live span value.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[inline]
unsafe fn set_span_last(iter: *mut SpanIter, value: Endpoint) {
    // SAFETY: The caller guarantees a live iterator.
    unsafe { (*iter).__bindgen_anon_2.last_hole = value };
}

/// Adds an interval node at its ordered position.
///
/// # Safety
///
/// `root` must be initialized. `node` must be isolated. The caller must
/// serialize all accesses to the tree.
unsafe fn insert(node: *mut Node, root: *mut Root) {
    // SAFETY: The caller guarantees valid objects.
    let start = unsafe { (*node).start };
    // SAFETY: The caller guarantees valid objects.
    let last = unsafe { (*node).last };

    // SAFETY: The root is initialized.
    let mut link = unsafe { ptr::addr_of_mut!((*root).rb_root.rb_node) };
    let mut rb_parent = ptr::null_mut();
    let mut leftmost = true;

    // SAFETY: Each linked red-black-tree node belongs to an interval node.
    while unsafe { !(*link).is_null() } {
        // SAFETY: The loop condition guarantees a linked node.
        let current_rb = unsafe { *link };
        // SAFETY: `current_rb` is embedded in an interval node.
        let current = unsafe { node_from_rb(current_rb) };

        // Maintain the maximum endpoint in every visited subtree.
        // SAFETY: `current` is valid.
        if unsafe { (*current).__subtree_last < last } {
            // SAFETY: `current` is valid.
            unsafe { (*current).__subtree_last = last };
        }

        // Equal start values are inserted into the right subtree.
        // SAFETY: `current` and `current_rb` are valid.
        if start < unsafe { (*current).start } {
            // SAFETY: The left link is writable.
            link = unsafe { ptr::addr_of_mut!((*current_rb).rb_left) };
        } else {
            // SAFETY: The right link is writable.
            link = unsafe { ptr::addr_of_mut!((*current_rb).rb_right) };
            leftmost = false;
        }
        rb_parent = current_rb;
    }

    // SAFETY: The caller guarantees a valid node.
    unsafe { (*node).__subtree_last = last };
    // SAFETY: Rust found the exact insertion link and the C helper performs
    // the common intrusive link and augmented rebalance primitives.
    unsafe {
        bindings::rust_interval_tree_link_insert(node, root, rb_parent, link, leftmost);
    }
}

/// Removes an interval node.
///
/// # Safety
///
/// `root` must contain `node`. The caller must serialize all accesses to the
/// tree.
unsafe fn remove(node: *mut Node, root: *mut Root) {
    // SAFETY: The C helper performs the common augmented erase primitive.
    unsafe { bindings::rust_interval_tree_erase(node, root) };
}

/// Finds the first interval that overlaps a range in a subtree.
///
/// # Safety
///
/// `node` must be a subtree root for which `start` is not greater than the
/// subtree maximum endpoint.
unsafe fn subtree_search(mut node: *mut Node, start: Endpoint, last: Endpoint) -> *mut Node {
    loop {
        // SAFETY: The caller guarantees a valid subtree root.
        let left_rb = unsafe { (*node).rb.rb_left };
        if !left_rb.is_null() {
            // SAFETY: `left_rb` is embedded in an interval node.
            let left = unsafe { node_from_rb(left_rb) };
            // SAFETY: The left subtree has a valid maximum endpoint.
            if start <= unsafe { (*left).__subtree_last } {
                node = left;
                continue;
            }
        }

        // SAFETY: The caller guarantees a valid node.
        if unsafe { (*node).start } <= last {
            // SAFETY: The caller guarantees a valid node.
            if start <= unsafe { (*node).last } {
                return node;
            }
            // The interval-tree invariant guarantees a right child here.
            // SAFETY: `rb_right` is embedded in an interval node.
            node = unsafe { node_from_rb((*node).rb.rb_right) };
            continue;
        }

        return ptr::null_mut();
    }
}

/// Finds the first interval that overlaps a range.
///
/// # Safety
///
/// `root` must be initialized and the caller must serialize tree access.
unsafe fn iter_first(root: *mut Root, start: Endpoint, last: Endpoint) -> *mut Node {
    // SAFETY: The caller guarantees an initialized root.
    let root_rb = unsafe { (*root).rb_root.rb_node };
    if root_rb.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: A non-empty root contains a valid tree root.
    let root_node = unsafe { node_from_rb(root_rb) };
    // Fast rejection using the maximum endpoint in the complete tree.
    // SAFETY: `root_node` is valid.
    if unsafe { (*root_node).__subtree_last } < start {
        return ptr::null_mut();
    }

    // SAFETY: A non-empty cached tree has a valid leftmost node.
    let leftmost_rb = unsafe { (*root).rb_leftmost };
    // SAFETY: The cached leftmost is embedded in an interval node.
    let leftmost = unsafe { node_from_rb(leftmost_rb) };
    // SAFETY: `leftmost` is valid.
    if unsafe { (*leftmost).start } > last {
        return ptr::null_mut();
    }

    // SAFETY: The fast rejection guarantees the subtree-search precondition.
    unsafe { subtree_search(root_node, start, last) }
}

/// Finds the next interval that overlaps a range.
///
/// # Safety
///
/// `node` must be a valid interval node in an initialized tree. The caller
/// must serialize tree access.
unsafe fn iter_next(mut node: *mut Node, start: Endpoint, last: Endpoint) -> *mut Node {
    loop {
        // SAFETY: The caller guarantees a valid node.
        let mut rb = unsafe { (*node).rb.rb_right };
        if !rb.is_null() {
            // SAFETY: The right link is embedded in an interval node.
            let right = unsafe { node_from_rb(rb) };
            // SAFETY: The right subtree has a valid maximum endpoint.
            if start <= unsafe { (*right).__subtree_last } {
                // SAFETY: The endpoint check satisfies the search precondition.
                return unsafe { subtree_search(right, start, last) };
            }
        }

        // Move up until the current subtree is its parent's left child.
        loop {
            // SAFETY: The caller guarantees a valid node.
            rb = unsafe { parent(rb_of_node(node)) };
            if rb.is_null() {
                return ptr::null_mut();
            }
            // SAFETY: The previous node link is valid while ascending.
            let previous = unsafe { rb_of_node(node) };
            // SAFETY: The parent link is embedded in an interval node.
            node = unsafe { node_from_rb(rb) };
            // SAFETY: `node` is valid.
            rb = unsafe { (*node).rb.rb_right };
            if previous != rb {
                break;
            }
        }

        // SAFETY: The ancestor is a valid interval node.
        if last < unsafe { (*node).start } {
            return ptr::null_mut();
        }
        // SAFETY: `node` is valid.
        if start <= unsafe { (*node).last } {
            return node;
        }
    }
}

/// Extends the current used span over all adjacent or overlapping intervals.
///
/// # Safety
///
/// `iter` must contain a valid first interval in `nodes[1]`. The caller must
/// serialize tree access.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
unsafe fn span_iter_next_gap(iter: *mut SpanIter) {
    // SAFETY: The caller guarantees a valid interval.
    let mut current = unsafe { (*iter).nodes[1] };
    // SAFETY: The interval pointer is writable.
    unsafe { (*iter).nodes[0] = current };

    loop {
        // SAFETY: Both current interval pointers are valid.
        if unsafe { (*current).last > (*(*iter).nodes[0]).last } {
            // SAFETY: The interval pointer is writable.
            unsafe { (*iter).nodes[0] = current };
        }
        // SAFETY: The iterator contains a valid query range.
        current = unsafe { iter_next(current, (*iter).first_index, (*iter).last_index) };
        if current.is_null() {
            break;
        }
        // SAFETY: Both interval pointers are valid.
        let current_max = unsafe { (*(*iter).nodes[0]).last };
        // SAFETY: `current` points to a valid interval.
        let current_start = unsafe { (*current).start };
        if current_max < current_start && current_max.wrapping_add(1) != current_start {
            break;
        }
    }

    // SAFETY: The iterator field is writable.
    unsafe { (*iter).nodes[1] = current };
}

/// Implements `interval_tree_insert()`.
///
/// # Safety
///
/// The arguments must satisfy the public interval-tree contract.
#[export]
unsafe extern "C" fn rust_interval_tree_insert(node: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { insert(node, root) };
}

/// Implements `interval_tree_remove()`.
///
/// # Safety
///
/// The arguments must satisfy the public interval-tree contract.
#[export]
unsafe extern "C" fn rust_interval_tree_remove(node: *mut Node, root: *mut Root) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { remove(node, root) };
}

/// Implements `interval_tree_subtree_search()`.
///
/// # Safety
///
/// The arguments must satisfy the public interval-tree contract.
#[export]
unsafe extern "C" fn rust_interval_tree_subtree_search(
    node: *mut Node,
    start: Endpoint,
    last: Endpoint,
) -> *mut Node {
    // SAFETY: Guaranteed by the public contract.
    unsafe { subtree_search(node, start, last) }
}

/// Implements `interval_tree_iter_first()`.
///
/// # Safety
///
/// The arguments must satisfy the public interval-tree contract.
#[export]
unsafe extern "C" fn rust_interval_tree_iter_first(
    root: *mut Root,
    start: Endpoint,
    last: Endpoint,
) -> *mut Node {
    // SAFETY: Guaranteed by the public contract.
    unsafe { iter_first(root, start, last) }
}

/// Implements `interval_tree_iter_next()`.
///
/// # Safety
///
/// The arguments must satisfy the public interval-tree contract.
#[export]
unsafe extern "C" fn rust_interval_tree_iter_next(
    node: *mut Node,
    start: Endpoint,
    last: Endpoint,
) -> *mut Node {
    // SAFETY: Guaranteed by the public contract.
    unsafe { iter_next(node, start, last) }
}

/// Implements `interval_tree_span_iter_first()`.
///
/// # Safety
///
/// The arguments must satisfy the public span iterator contract.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[export]
unsafe extern "C" fn rust_interval_tree_span_iter_first(
    iter: *mut SpanIter,
    tree: *mut Root,
    first: Endpoint,
    last: Endpoint,
) {
    // SAFETY: The caller guarantees valid iterator and tree objects.
    unsafe {
        (*iter).first_index = first;
        (*iter).last_index = last;
        (*iter).nodes[0] = ptr::null_mut();
        (*iter).nodes[1] = iter_first(tree, first, last);
    }

    // SAFETY: The iterator fields are writable.
    let first_node = unsafe { (*iter).nodes[1] };
    if first_node.is_null() {
        // SAFETY: The iterator fields are writable.
        unsafe {
            set_span_start(iter, first);
            set_span_last(iter, last);
            (*iter).is_hole = 1;
        }
        return;
    }

    // SAFETY: `first_node` is a valid interval.
    if unsafe { (*first_node).start } > first {
        // SAFETY: The iterator fields are writable.
        unsafe {
            set_span_start(iter, first);
            set_span_last(iter, (*first_node).start - 1);
            (*iter).is_hole = 1;
            span_iter_next_gap(iter);
        }
        return;
    }

    // SAFETY: The iterator fields are writable.
    unsafe {
        set_span_start(iter, first);
        (*iter).is_hole = 0;
        span_iter_next_gap(iter);
        set_span_last(iter, (*(*iter).nodes[0]).last);
    }
    // SAFETY: The iterator fields are writable.
    if unsafe { span_last(iter) } >= last {
        // SAFETY: The iterator fields are writable.
        unsafe {
            set_span_last(iter, last);
            (*iter).nodes[0] = ptr::null_mut();
            (*iter).nodes[1] = ptr::null_mut();
        }
    }
}

/// Implements `interval_tree_span_iter_next()`.
///
/// # Safety
///
/// `iter` must be a live span iterator created by the public interface.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[export]
unsafe extern "C" fn rust_interval_tree_span_iter_next(iter: *mut SpanIter) {
    // SAFETY: The caller guarantees a valid iterator.
    if unsafe { (*iter).nodes[0] }.is_null() && unsafe { (*iter).nodes[1] }.is_null() {
        // SAFETY: The iterator field is writable.
        unsafe { (*iter).is_hole = -1 };
        return;
    }

    // SAFETY: The caller guarantees a valid iterator.
    if unsafe { (*iter).is_hole } != 0 {
        // SAFETY: A hole iterator has a valid first and last index.
        unsafe {
            set_span_start(iter, span_last(iter).wrapping_add(1));
            set_span_last(iter, (*(*iter).nodes[0]).last);
        }
        // SAFETY: The iterator fields are writable.
        if unsafe { span_last(iter) } >= unsafe { (*iter).last_index } {
            // SAFETY: The iterator fields are writable.
            unsafe {
                set_span_last(iter, (*iter).last_index);
                (*iter).nodes[0] = ptr::null_mut();
                (*iter).nodes[1] = ptr::null_mut();
            }
        }
        // SAFETY: The iterator field is writable.
        unsafe { (*iter).is_hole = 0 };
        return;
    }

    // SAFETY: The caller guarantees a valid iterator.
    if unsafe { (*iter).nodes[1] }.is_null() {
        // SAFETY: A used iterator has a valid current interval.
        unsafe {
            set_span_start(iter, (*(*iter).nodes[0]).last.wrapping_add(1));
            set_span_last(iter, (*iter).last_index);
            (*iter).nodes[0] = ptr::null_mut();
            (*iter).is_hole = 1;
        }
        return;
    }

    // SAFETY: Both interval pointers are valid.
    unsafe {
        set_span_start(iter, (*(*iter).nodes[0]).last.wrapping_add(1));
        set_span_last(iter, (*(*iter).nodes[1]).start - 1);
        (*iter).is_hole = 1;
        span_iter_next_gap(iter);
    }
}

/// Implements `interval_tree_span_iter_advance()`.
///
/// # Safety
///
/// `iter` must be a live span iterator. `tree` must remain initialized while
/// the iterator is live.
#[cfg(CONFIG_INTERVAL_TREE_SPAN_ITER)]
#[export]
unsafe extern "C" fn rust_interval_tree_span_iter_advance(
    iter: *mut SpanIter,
    tree: *mut Root,
    index: Endpoint,
) {
    // SAFETY: The caller guarantees a valid iterator.
    if unsafe { (*iter).is_hole } == -1 {
        return;
    }

    // SAFETY: The caller guarantees valid objects.
    unsafe { (*iter).first_index = index };
    // SAFETY: The iterator contains the query end.
    if index > unsafe { (*iter).last_index } {
        // SAFETY: The iterator field is writable.
        unsafe { (*iter).is_hole = -1 };
        return;
    }

    // The public structure aliases hole and used start or end fields.
    // SAFETY: The caller guarantees a live iterator.
    if unsafe { span_start(iter) <= index && index <= span_last(iter) } {
        // SAFETY: The iterator field is writable.
        unsafe { set_span_start(iter, index) };
        return;
    }

    // SAFETY: The caller guarantees valid objects.
    if index == unsafe { span_last(iter).wrapping_add(1) } {
        // SAFETY: Advancing an iterator is safe under the public contract.
        unsafe { rust_interval_tree_span_iter_next(iter) };
    } else {
        // SAFETY: The iterator query end is valid.
        unsafe {
            rust_interval_tree_span_iter_first(iter, tree, index, (*iter).last_index);
        }
    }
}
