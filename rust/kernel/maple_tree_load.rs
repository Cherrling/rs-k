// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree load traversal.
//!
//! Rust owns lookup state initialization, root selection, single-entry tree
//! handling, node descent, dead-node restart, and the RCU lifecycle. C keeps
//! the Maple node encoding, node layout, protected slot access, tracepoint,
//! and lockdep primitives.

use crate::bindings;
use crate::maple_tree_core::{
    is_leaf, is_none, is_ptr, is_start, new_state, node_is_dead, node_type, pivot_count, pivots,
    rcu_read_lock, slot, slots, start, to_node, MaState, MapleTree,
};
use core::{ffi::c_void, ptr};
use macros::export;

/// Descends a lookup from the active root node to a leaf entry.
///
/// # Safety
///
/// `mas` must be active and protected by RCU or the tree lock.
unsafe fn lookup_walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: The caller guarantees that the active state has an encoded node.
    let mut next = unsafe { (*mas).node };

    loop {
        // SAFETY: `next` is an encoded node reached from a protected tree.
        let node = unsafe { to_node(next) };
        // SAFETY: `next` is a live encoded node.
        let node_type = unsafe { node_type(next.cast_const()) };
        // SAFETY: `node` and `node_type` describe the same live node.
        let node_pivots = unsafe { pivots(node, node_type) };
        let pivot_count = pivot_count(node_type);

        let mut offset = 0u8;
        loop {
            // SAFETY: `offset` is below the type's pivot count.
            if unsafe { *node_pivots.add(usize::from(offset)) } >= unsafe { (*mas).index } {
                break;
            }
            offset += 1;
            if offset >= pivot_count {
                break;
            }
        }

        // SAFETY: `node` and `node_type` describe the same live node.
        let node_slots = unsafe { slots(node, node_type) };
        // SAFETY: The slot offset is within this node and the caller holds
        // RCU or the tree lock.
        next = unsafe { slot(mas, node_slots, offset).cast() };

        // SAFETY: The node was acquired under RCU or the tree lock.
        if unsafe { node_is_dead(node) } {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).status = bindings::maple_status_ma_start;
                (*mas).node = ptr::null_mut();
            }
            return ptr::null_mut();
        }

        if is_leaf(node_type) {
            return next.cast();
        }
    }
}

/// Implements the complete public Maple Tree load operation.
///
/// # Safety
///
/// `tree` must point to a valid initialized Maple tree.
unsafe fn load(tree: *mut MapleTree, index: usize) -> *mut c_void {
    let mut state = new_state(tree, index);

    // SAFETY: The complete state has been initialized.
    unsafe { bindings::rust_maple_tree_load_trace(&mut state) };

    let guard = rcu_read_lock();
    let entry = loop {
        // SAFETY: The state is valid and protected by RCU.
        let entry = unsafe { start(&mut state) };
        if is_none(&state) {
            break entry;
        }
        if is_ptr(&state) {
            break if index == 0 { entry } else { ptr::null_mut() };
        }

        // SAFETY: The active state was set up by `start`.
        let entry = unsafe { lookup_walk(&mut state) };
        if !entry.is_null() || !is_start(&state) {
            break entry;
        }
    };
    drop(guard);

    // SAFETY: Entry classification only compares the reserved pointer value.
    if unsafe { bindings::rust_maple_tree_is_zero(entry.cast_const()) } {
        ptr::null_mut()
    } else {
        entry
    }
}

/// Loads a Maple Tree entry through the production ABI.
///
/// # Safety
///
/// The caller must provide a valid initialized Maple tree.
#[export]
unsafe extern "C" fn mtree_load(tree: *mut MapleTree, index: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { load(tree, index) }
}
