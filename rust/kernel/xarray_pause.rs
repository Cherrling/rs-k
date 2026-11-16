// SPDX-License-Identifier: GPL-2.0

//! Native XArray pause and cursor recovery.
//!
//! Rust owns restart selection, sibling-run scanning, index advancement, and
//! bounds selection for `xas_pause()`. C retains cursor and tagged-entry
//! layouts and RCU slot access.

use crate::bindings;
use core::ffi::c_void;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_SIZE: usize = 1usize << bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;

#[inline(always)]
/// Reports whether the cursor cannot be used for an XArray operation.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn invalid(xas: *const XaState) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_invalid(xas) }
}

#[inline(always)]
/// Moves the cursor to the restart sentinel.
///
/// # Safety
///
/// `xas` must point to a writable XArray state.
unsafe fn set_restart(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_restart(xas) }
}

#[inline(always)]
/// Moves the cursor to the bounds sentinel.
///
/// # Safety
///
/// `xas` must point to a writable XArray state.
unsafe fn set_bounds(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_bounds(xas) }
}

/// Loads one slot under the caller's XArray lock.
///
/// # Safety
///
/// `xa` and `node` must be valid and `offset` must be in range.
unsafe fn entry(xa: *const XArray, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_entry(xa, node, offset) }
}

#[inline(always)]
fn is_sibling(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_sibling(entry) }
}

/// Pauses a walk and positions the cursor for the following search.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock or the RCU read lock while this function accesses slots.
pub(crate) unsafe fn pause(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let node = state.xa_node;

    // SAFETY: The state remains addressable for the complete operation.
    if unsafe { invalid(xas) } {
        return;
    }

    // SAFETY: The state is writable by this function's contract.
    unsafe { set_restart(xas) };

    if node.is_null() {
        state.xa_index = state.xa_index.wrapping_add(1);
        return;
    }

    let base = usize::from(state.xa_offset);
    let mut offset = base;

    loop {
        offset += 1;
        if offset >= CHUNK_SIZE {
            break;
        }

        // SAFETY: A valid non-null cursor identifies a live node and the
        // offset is below the chunk size.
        let loaded = unsafe { entry(state.xa, node, offset as u32) };
        if !is_sibling(loaded) {
            break;
        }
    }

    // SAFETY: The cursor identifies a live node.
    let shift = u32::from(unsafe { (*node).shift });
    state.xa_index &= usize::MAX << shift;
    state.xa_index = state
        .xa_index
        .wrapping_add((offset - base).wrapping_shl(shift));

    if state.xa_index == 0 {
        // SAFETY: The state is writable and no reference is used afterwards.
        unsafe { set_bounds(xas) };
    }
}

/// Pauses an XArray walk through the advanced API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock or the RCU read lock while slots are read.
#[export]
unsafe extern "C" fn rust_xarray_pause(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { pause(xas) }
}
