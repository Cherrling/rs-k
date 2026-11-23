// SPDX-License-Identifier: GPL-2.0-only

//! Native generic radix tree implementation.
//!
//! Rust directly defines the public generic radix tree entry points. It owns
//! root growth, node traversal, forward and reverse sparse iteration,
//! preallocation, and recursive release. C retains node allocation,
//! tagged-root representation helpers, atomic publication, and root exchange.

use crate::bindings;
use core::ffi::{c_int, c_uint, c_void};
use core::ptr;
use macros::export;

type Radix = bindings::__genradix;
type Root = *mut bindings::genradix_root;
type Node = *mut bindings::genradix_node;
type Iter = bindings::genradix_iter;
type Gfp = bindings::gfp_t;

const NODE_SHIFT: c_uint = bindings::rust_genradix_constant_RUST_GENRADIX_NODE_SHIFT as c_uint;
const NODE_SIZE: usize = 1usize << NODE_SHIFT;
const ARY_SHIFT: c_uint = bindings::rust_genradix_constant_RUST_GENRADIX_ARY_SHIFT as c_uint;
const ARY_COUNT: usize = 1usize << ARY_SHIFT;
const ARY_MASK: usize = ARY_COUNT - 1;

#[inline(always)]
fn depth_shift(depth: c_uint) -> c_uint {
    NODE_SHIFT.wrapping_add(ARY_SHIFT.wrapping_mul(depth))
}

#[inline(always)]
fn depth_size(depth: c_uint) -> usize {
    1usize.checked_shl(depth_shift(depth)).unwrap_or(usize::MAX)
}

#[inline(always)]
fn offset_fits(offset: usize, depth: c_uint) -> bool {
    let shift = depth_shift(depth);

    shift >= usize::BITS || offset < (1usize << shift)
}

#[inline(always)]
fn round_down(value: usize, size: usize) -> usize {
    value & !(size.wrapping_sub(1))
}

#[inline(always)]
fn low_mask(shift: c_uint) -> usize {
    if shift >= usize::BITS {
        usize::MAX
    } else {
        (1usize << shift) - 1
    }
}

/// Loads a stable tagged root and decodes its node and depth.
///
/// # Safety
///
/// `radix` must point to a valid generic radix tree.
unsafe fn root_state(radix: *mut Radix) -> (Root, Node, c_uint) {
    // SAFETY: Guaranteed by this function's contract.
    let root = unsafe { bindings::genradix_root_load(radix) };
    // SAFETY: Decoding a valid tagged root does not dereference it.
    let node = unsafe { bindings::genradix_root_to_node(root) };
    // SAFETY: Decoding a valid tagged root does not dereference it.
    let depth = unsafe { bindings::genradix_root_to_depth(root) };

    (root, node, depth)
}

/// Loads one child pointer with the kernel's atomic read semantics.
///
/// # Safety
///
/// `node` must be an interior node at a level that contains `index`.
unsafe fn child_load(node: Node, index: usize) -> Node {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::genradix_node_child_load(node, index) }
}

/// Installs a child only if its slot remains empty.
///
/// Returns the previous child. A null result means that `child` was installed.
///
/// # Safety
///
/// `node` must be an interior node at a level that contains `index`.
unsafe fn child_install(node: Node, index: usize, child: Node) -> Node {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::genradix_node_child_install(node, index, child) }
}

/// Returns the data storage for a leaf node.
///
/// # Safety
///
/// `node` must point to an allocated genradix node.
unsafe fn node_data(node: Node) -> *mut u8 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::genradix_node_data(node).cast() }
}

/// Finds an existing entry without allocating nodes.
///
/// # Safety
///
/// `radix` must point to a valid generic radix tree.
#[export]
unsafe extern "C" fn __genradix_ptr(radix: *mut Radix, mut offset: usize) -> *mut c_void {
    // SAFETY: The function contract requires a valid radix tree.
    let (_, mut node, depth) = unsafe { root_state(radix) };
    let mut shift = depth_shift(depth);

    if !offset_fits(offset, depth) {
        return ptr::null_mut();
    }

    while !node.is_null() && shift > NODE_SHIFT {
        shift -= ARY_SHIFT;
        let index = (offset >> shift) & ARY_MASK;
        // SAFETY: Each traversal consumes one interior level of the decoded
        // tree, so `node` owns the selected child slot.
        node = unsafe { child_load(node, index) };
        offset &= low_mask(shift);
    }

    if node.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The traversal ended at an allocated leaf node.
    unsafe { node_data(node).add(offset & (NODE_SIZE - 1)).cast() }
}

/// Finds or allocates an entry.
///
/// # Safety
///
/// `radix` must point to a valid tree. If non-NULL, `preallocated` must be a
/// writable node pointer.
unsafe fn ptr_alloc(
    radix: *mut Radix,
    mut offset: usize,
    preallocated: *mut Node,
    gfp_mask: Gfp,
) -> *mut c_void {
    // SAFETY: The function contract requires a valid radix tree.
    let (mut root, mut node, mut depth) = unsafe { root_state(radix) };
    let mut new_node = if preallocated.is_null() {
        ptr::null_mut()
    } else {
        // SAFETY: The caller guarantees a writable preallocated pointer.
        let node = unsafe { *preallocated };
        // SAFETY: The caller guarantees a writable preallocated pointer.
        unsafe { *preallocated = ptr::null_mut() };
        node
    };

    loop {
        if !node.is_null() && offset_fits(offset, depth) {
            break;
        }

        if new_node.is_null() {
            // SAFETY: The allocator boundary accepts the caller's GFP mask.
            new_node = unsafe { bindings::genradix_node_alloc(gfp_mask) };
            if new_node.is_null() {
                return ptr::null_mut();
            }
        }

        // SAFETY: `new_node` is unpublished and has room for child zero.
        unsafe { bindings::genradix_node_child_set(new_node, 0, node) };
        let next_depth = if node.is_null() {
            0
        } else {
            depth.wrapping_add(1)
        };
        // SAFETY: Creating the tagged root does not dereference either input.
        let new_root = unsafe { bindings::genradix_make_root(new_node, next_depth) };
        // SAFETY: The caller guarantees a valid root slot.
        let observed = unsafe { bindings::genradix_root_cmpxchg(radix, root, new_root) };

        if observed == root {
            root = new_root;
            node = new_node;
            depth = next_depth;
            new_node = ptr::null_mut();
        } else {
            root = observed;
            // SAFETY: The failed root publication did not publish `new_node`.
            unsafe { bindings::genradix_node_child_set(new_node, 0, ptr::null_mut()) };
            // SAFETY: Decode the observed tagged root.
            node = unsafe { bindings::genradix_root_to_node(root) };
            // SAFETY: Decode the observed tagged root.
            depth = unsafe { bindings::genradix_root_to_depth(root) };
        }
    }

    while depth != 0 {
        depth -= 1;
        let index = (offset >> depth_shift(depth)) & ARY_MASK;
        // SAFETY: The current node is an interior node at this depth.
        let mut child = unsafe { child_load(node, index) };

        if child.is_null() {
            if new_node.is_null() {
                // SAFETY: The allocator boundary accepts the caller's GFP mask.
                new_node = unsafe { bindings::genradix_node_alloc(gfp_mask) };
                if new_node.is_null() {
                    return ptr::null_mut();
                }
            }

            // SAFETY: The current node is an interior node at this depth.
            let observed = unsafe { child_install(node, index, new_node) };
            if observed.is_null() {
                child = new_node;
                new_node = ptr::null_mut();
            } else {
                child = observed;
            }
        }

        node = child;
        offset &= depth_size(depth).wrapping_sub(1);
    }

    if !new_node.is_null() {
        // SAFETY: This node was never published or is no longer needed.
        unsafe { bindings::genradix_node_free(new_node) };
    }

    // SAFETY: The traversal ended at an allocated leaf node.
    unsafe { node_data(node).add(offset & (NODE_SIZE - 1)).cast() }
}

/// Finds or allocates an entry.
///
/// # Safety
///
/// `radix` must point to a valid tree. If non-NULL, `preallocated` must be a
/// writable node pointer.
#[export]
unsafe extern "C" fn __genradix_ptr_alloc(
    radix: *mut Radix,
    offset: usize,
    preallocated: *mut Node,
    gfp_mask: Gfp,
) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { ptr_alloc(radix, offset, preallocated, gfp_mask) }
}

/// Returns the first allocated entry at or above the iterator position.
///
/// # Safety
///
/// `iter` must be writable and `radix` must point to a valid tree.
#[export]
unsafe extern "C" fn __genradix_iter_peek(
    iter: *mut Iter,
    radix: *mut Radix,
    objs_per_page: usize,
) -> *mut c_void {
    // SAFETY: The caller guarantees an exclusive mutable iterator.
    let iter = unsafe { &mut *iter };

    if iter.offset == usize::MAX {
        return ptr::null_mut();
    }

    'restart: loop {
        // SAFETY: The function contract requires a valid tree.
        let (root, mut node, mut depth) = unsafe { root_state(radix) };
        if root.is_null() {
            return ptr::null_mut();
        }
        if !offset_fits(iter.offset, depth) {
            return ptr::null_mut();
        }

        while depth != 0 {
            depth -= 1;
            let mut index = (iter.offset >> depth_shift(depth)) & ARY_MASK;

            loop {
                // SAFETY: The current node is an interior node at this depth.
                let child = unsafe { child_load(node, index) };
                if !child.is_null() {
                    node = child;
                    break;
                }

                let objects_per_pointer = depth_size(depth);
                let next = iter.offset.wrapping_add(objects_per_pointer);
                if next < iter.offset {
                    iter.offset = usize::MAX;
                    iter.pos = usize::MAX;
                    return ptr::null_mut();
                }

                index += 1;
                iter.offset = round_down(next, objects_per_pointer);
                iter.pos = (iter.offset >> NODE_SHIFT).wrapping_mul(objs_per_page);
                if index == ARY_COUNT {
                    continue 'restart;
                }
            }
        }

        // SAFETY: The traversal ended at an allocated leaf node.
        return unsafe { node_data(node).add(iter.offset & (NODE_SIZE - 1)).cast() };
    }
}

/// Returns the first allocated entry at or below the iterator position.
///
/// # Safety
///
/// `iter` must be writable and `radix` must point to a valid tree.
#[export]
unsafe extern "C" fn __genradix_iter_peek_prev(
    iter: *mut Iter,
    radix: *mut Radix,
    objs_per_page: usize,
    obj_size_plus_page_remainder: usize,
) -> *mut c_void {
    // SAFETY: The caller guarantees an exclusive mutable iterator.
    let iter = unsafe { &mut *iter };

    if iter.offset == usize::MAX {
        return ptr::null_mut();
    }

    'restart: loop {
        // SAFETY: The function contract requires a valid tree.
        let (root, mut node, mut depth) = unsafe { root_state(radix) };
        if root.is_null() {
            return ptr::null_mut();
        }

        if !offset_fits(iter.offset, depth) {
            iter.offset = depth_size(depth);
            iter.pos = (iter.offset >> NODE_SHIFT).wrapping_mul(objs_per_page);
            iter.offset = iter.offset.wrapping_sub(obj_size_plus_page_remainder);
            iter.pos = iter.pos.wrapping_sub(1);
        }

        while depth != 0 {
            depth -= 1;
            let mut index = (iter.offset >> depth_shift(depth)) & ARY_MASK;

            loop {
                // SAFETY: The current node is an interior node at this depth.
                let child = unsafe { child_load(node, index) };
                if !child.is_null() {
                    node = child;
                    break;
                }

                let objects_per_pointer = depth_size(depth);
                iter.offset = round_down(iter.offset, objects_per_pointer);
                iter.pos = (iter.offset >> NODE_SHIFT).wrapping_mul(objs_per_page);

                if iter.offset == 0 {
                    return ptr::null_mut();
                }

                iter.offset = iter.offset.wrapping_sub(obj_size_plus_page_remainder);
                iter.pos = iter.pos.wrapping_sub(1);
                if index == 0 {
                    continue 'restart;
                }
                index -= 1;
            }
        }

        // SAFETY: The traversal ended at an allocated leaf node.
        return unsafe { node_data(node).add(iter.offset & (NODE_SIZE - 1)).cast() };
    }
}

/// Allocates every page that intersects the byte range.
///
/// # Safety
///
/// `radix` must point to a valid generic radix tree.
unsafe fn prealloc(radix: *mut Radix, size: usize, gfp_mask: Gfp) -> c_int {
    let mut offset = 0;

    while offset < size {
        // SAFETY: The radix is valid and no preallocated node is supplied.
        if unsafe { ptr_alloc(radix, offset, ptr::null_mut(), gfp_mask) }.is_null() {
            return -(bindings::ENOMEM as c_int);
        }

        let next = offset.wrapping_add(NODE_SIZE);
        if next <= offset {
            break;
        }
        offset = next;
    }

    0
}

/// Preallocates pages.
///
/// # Safety
///
/// `radix` must point to a valid generic radix tree.
#[export]
unsafe extern "C" fn __genradix_prealloc(radix: *mut Radix, size: usize, gfp_mask: Gfp) -> c_int {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prealloc(radix, size, gfp_mask) }
}

/// Releases a detached node subtree.
///
/// # Safety
///
/// `node` must be a detached subtree root at `depth`.
unsafe fn free_node(node: Node, depth: c_uint) {
    if node.is_null() {
        return;
    }

    if depth != 0 {
        for index in 0..ARY_COUNT {
            // SAFETY: The detached node is an interior node at this depth.
            let child = unsafe { child_load(node, index) };
            if !child.is_null() {
                // SAFETY: The child is a detached subtree at the next depth.
                unsafe { free_node(child, depth - 1) };
            }
        }
    }

    // SAFETY: All descendants have been released, so the node can be freed.
    unsafe { bindings::genradix_node_free(node) };
}

/// Releases a generic radix tree.
///
/// # Safety
///
/// `radix` must point to a valid generic radix tree with exclusive access.
#[export]
unsafe extern "C" fn __genradix_free(radix: *mut Radix) {
    // SAFETY: The function contract guarantees a valid root slot.
    let root = unsafe { bindings::genradix_root_xchg_null(radix) };
    // SAFETY: Decode the detached tagged root.
    let node = unsafe { bindings::genradix_root_to_node(root) };
    // SAFETY: Decode the detached tagged root.
    let depth = unsafe { bindings::genradix_root_to_depth(root) };

    // SAFETY: The subtree is detached from all concurrent access.
    unsafe { free_node(node, depth) };
}
