// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree staged-node allocation.
//!
//! Rust owns the single-node fast path, sheaf refill and reuse policy, and
//! cleanup of nodes staged in a write state. C retains slab and sheaf
//! primitives, node zeroing, layout, and lock or RCU lifetime rules.

use super::maple_tree_core::{MaState, MapleNode};
use crate::bindings;
use macros::export;

/// Obtains the replacement node selected by the staging policy.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple state. A selected sheaf must
/// contain an available node.
pub(crate) unsafe fn pop_node(mas: *mut MaState) -> *mut MapleNode {
    // SAFETY: The caller supplies a valid Maple state.
    let staged = unsafe { (*mas).alloc };
    let node = if !staged.is_null() {
        // Consume the single-node fast path before consulting the sheaf.
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).alloc = core::ptr::null_mut() };
        staged
    } else {
        // SAFETY: The caller supplies a valid state with a usable sheaf.
        unsafe { bindings::rust_maple_tree_pool_pop_from_sheaf((*mas).sheaf) }
    };

    if !node.is_null() {
        // SAFETY: The pool primitive returned one complete node object.
        unsafe { bindings::rust_maple_tree_pool_clear_node(node) };
    }

    node
}

/// Stages the nodes requested by a Maple write operation.
///
/// # Safety
///
/// `mas` must identify a valid Maple state. The state must not race with
/// another user of its staged allocation fields.
pub(crate) unsafe fn alloc_nodes(mas: *mut MaState, gfp: bindings::gfp_t) {
    // SAFETY: The caller supplies a valid Maple state.
    let request = unsafe { (*mas).node_request };
    if request == 0 {
        return;
    }

    if request == 1 {
        // SAFETY: The caller supplies a valid state with stable staging fields.
        if !unsafe { (*mas).sheaf }.is_null() {
            // A sheaf can satisfy a one-node request without a fresh object.
        } else {
            // SAFETY: The caller supplies a valid writable state.
            if !unsafe { (*mas).alloc }.is_null() {
                return;
            }

            // SAFETY: The caller supplies valid allocation flags.
            let node = unsafe { bindings::rust_maple_tree_pool_alloc_one(gfp) };
            if node.is_null() {
                // SAFETY: The caller supplied a state that can hold an error.
                unsafe { bindings::rust_maple_tree_pool_set_nomem(mas) };
                return;
            }

            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).alloc = node;
                (*mas).node_request = 0;
            }
            return;
        }
    }

    // SAFETY: The caller supplies a valid writable state.
    let single = unsafe { (*mas).alloc };
    if !single.is_null() {
        // The multi-node path retains only the sheaf representation.
        // SAFETY: `single` is the staged node owned by this state.
        unsafe { bindings::rust_maple_tree_pool_free_one(single) };
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).alloc = core::ptr::null_mut() };
    }

    // SAFETY: The caller supplies a valid writable state.
    let sheaf = unsafe { (*mas).sheaf };
    if !sheaf.is_null() {
        // SAFETY: `sheaf` is the staged sheaf owned by this state.
        let size = unsafe { bindings::rust_maple_tree_pool_sheaf_size(sheaf) };
        if usize::try_from(size).unwrap() >= request {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).node_request = 0 };
            return;
        }

        // SAFETY: The caller supplies a valid writable state and refill size.
        let ret = unsafe {
            bindings::rust_maple_tree_pool_refill_sheaf(
                gfp,
                core::ptr::addr_of_mut!((*mas).sheaf),
                u32::try_from(request).unwrap(),
            )
        };
        if ret != 0 {
            // SAFETY: The caller supplied a state that can hold an error.
            unsafe { bindings::rust_maple_tree_pool_set_nomem(mas) };
            return;
        }

        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).node_request = 0 };
        return;
    }

    let count = i32::try_from(request).unwrap();
    // SAFETY: The caller supplies valid allocation flags and request count.
    let sheaf = unsafe { bindings::rust_maple_tree_pool_get_sheaf(gfp, count) };
    if sheaf.is_null() {
        // SAFETY: The caller supplied a state that can hold an error.
        unsafe { bindings::rust_maple_tree_pool_set_nomem(mas) };
        return;
    }

    // SAFETY: The newly allocated sheaf is transferred to the Maple state.
    unsafe {
        (*mas).sheaf = sheaf;
        (*mas).node_request = 0;
    }
}

/// Releases all staged nodes and resets the request count.
///
/// # Safety
///
/// `mas` must identify a valid Maple state. The caller must hold the tree
/// write lock when staged nodes can be shared with publication.
pub(crate) unsafe fn empty_nodes(mas: *mut MaState) {
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).node_request = 0 };

    // SAFETY: The caller supplies a valid writable state.
    let sheaf = unsafe { (*mas).sheaf };
    if !sheaf.is_null() {
        // SAFETY: `sheaf` is owned by the staged state.
        unsafe { bindings::rust_maple_tree_pool_return_sheaf(sheaf) };
        // SAFETY: Clear the released staged pointer.
        unsafe { (*mas).sheaf = core::ptr::null_mut() };
    }

    // SAFETY: The caller supplies a valid writable state.
    let single = unsafe { (*mas).alloc };
    if !single.is_null() {
        // SAFETY: `single` is owned by the staged state.
        unsafe { bindings::rust_maple_tree_pool_free_one(single) };
        // SAFETY: Clear the released staged pointer.
        unsafe { (*mas).alloc = core::ptr::null_mut() };
    }
}

/// Obtains one staged Maple node for a write operation.
///
/// # Safety
///
/// The C caller must supply a valid locked state with staged storage.
#[export]
unsafe extern "C" fn rust_maple_tree_stage_pop_node(mas: *mut MaState) -> *mut MapleNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { pop_node(mas) }
}

/// Stages nodes for a Maple write operation.
///
/// # Safety
///
/// The C caller must supply a valid state and allocation flags.
#[export]
unsafe extern "C" fn rust_maple_tree_stage_alloc_nodes(mas: *mut MaState, gfp: bindings::gfp_t) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc_nodes(mas, gfp) }
}

/// Releases nodes staged in a Maple state.
///
/// # Safety
///
/// The C caller must supply a valid state.
#[export]
unsafe extern "C" fn rust_maple_tree_stage_empty_nodes(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { empty_nodes(mas) }
}
