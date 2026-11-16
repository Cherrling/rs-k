// SPDX-License-Identifier: GPL-2.0

//! Native XArray operation-preallocation lifecycle.
//!
//! Rust owns ENOMEM detection, pending-node chain cleanup, cursor restart, and
//! allocation retention decisions. C retains slab allocation, node layout and
//! preparation, RCU parent access, invariant checks, and node reclamation.

use crate::bindings;
use macros::export;

type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const ENOMEM: i32 = -(bindings::rust_xarray_nomem_constant_RUST_XARRAY_NOMEM_ENOMEM as i32);

#[inline(always)]
/// Reads the errno stored in an XArray cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
}

#[inline(always)]
/// Resets an XArray cursor to restart at the root.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn reset(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_reset(xas) };
}

#[inline(always)]
/// Allocates one node for an operation preallocation.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn alloc_prealloc(xas: *mut XaState, gfp: bindings::gfp_t) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_alloc_prealloc(xas, gfp) }
}

#[inline(always)]
/// Reads the next node in a pending preallocation chain.
///
/// # Safety
///
/// `node` must be a live pending preallocation.
unsafe fn parent_prealloc(node: *const XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_parent_prealloc(node) }
}

#[inline(always)]
/// Initializes the fields needed before retaining a new preallocation.
///
/// # Safety
///
/// `node` must be a live node returned by the preallocation allocator.
unsafe fn prepare_prealloc(node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_prepare_prealloc(node) };
}

#[inline(always)]
/// Releases one detached preallocation.
///
/// # Safety
///
/// `node` must be detached from every XArray and from the pending chain.
unsafe fn free_prealloc(node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_free_prealloc(node) };
}

/// Releases every pending preallocation in an XArray cursor.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
pub(crate) unsafe fn destroy(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let mut node = unsafe { (*xas).xa_alloc };

    while !node.is_null() {
        // SAFETY: The pending chain contains live detached nodes.
        let next = unsafe { parent_prealloc(node) };
        // SAFETY: The node is detached and its parent is saved first.
        unsafe { free_prealloc(node) };
        // SAFETY: The caller provides a writable state.
        unsafe { (*xas).xa_alloc = next };
        node = next;
    }
}

/// Handles allocation after an XArray operation reported ENOMEM.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
pub(crate) unsafe fn nomem(xas: *mut XaState, gfp: bindings::gfp_t) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { error(xas) } != ENOMEM {
        // SAFETY: Every other cursor state releases stale preallocations.
        unsafe { destroy(xas) };
        return false;
    }

    // SAFETY: The allocator accepts the valid state and allocation flags.
    let node = unsafe { alloc_prealloc(xas, gfp) };
    // SAFETY: The caller provides a writable state.
    unsafe { (*xas).xa_alloc = node };
    if node.is_null() {
        return false;
    }

    // SAFETY: The allocator returned a live detached node.
    unsafe { prepare_prealloc(node) };
    // SAFETY: A successful preallocation restarts the cursor at the root.
    unsafe { reset(xas) };
    true
}

/// Handles XArray operation preallocation through the public ABI.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
#[export]
unsafe extern "C" fn rust_xarray_nomem(xas: *mut XaState, gfp: bindings::gfp_t) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { nomem(xas, gfp) }
}

/// Releases XArray operation preallocations through the public ABI.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
#[export]
unsafe extern "C" fn rust_xarray_destroy(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { destroy(xas) };
}
