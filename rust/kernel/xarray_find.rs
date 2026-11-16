// SPDX-License-Identifier: GPL-2.0

//! Native XArray forward search.
//!
//! Rust owns cursor advancement, empty-subtree jumps, parent traversal, index
//! and offset updates, and maximum-index boundaries. C retains node and cursor
//! layouts, RCU slot access, parent lookup, and error classification.

use crate::bindings;
use core::ffi::c_void;
use core::ptr;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SIZE: usize = 1usize << bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
const BOUNDS_ADDRESS: usize = 1;
const RESTART_ADDRESS: usize = 3;

#[inline(always)]
fn is_node(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_node(entry) }
}

#[inline(always)]
fn to_node(entry: *mut c_void) -> *mut XaNode {
    // SAFETY: The caller checks that `entry` is a node entry first.
    unsafe { bindings::xarray_to_node(entry) }
}

#[inline(always)]
fn is_sibling(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_sibling(entry) }
}

#[inline(always)]
fn not_node(node: *mut XaNode) -> bool {
    node.addr() & 3 != 0 || node.is_null()
}

#[inline(always)]
/// Reports whether the cursor contains an error.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
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

/// Loads one slot under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` and `node` must be valid and `offset` must be in range.
unsafe fn entry(xa: *const XArray, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_entry(xa, node, offset) }
}

/// Loads the parent under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` and `node` must be valid.
unsafe fn parent(xa: *const XArray, node: *mut XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent(xa, node) }
}

/// Loads the entry at the current top cursor.
///
/// # Safety
///
/// `xas` must be valid under RCU or lock protection.
unsafe fn load(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { crate::xarray_load::load_advanced(xas) }
}

/// Advances the offset and updates the index from that offset.
///
/// # Safety
///
/// The cursor must identify a live node.
unsafe fn next_offset(state: &mut XaState) {
    state.xa_offset = state.xa_offset.wrapping_add(1);
    let node = state.xa_node;
    // SAFETY: The cursor identifies a live node.
    let shift = u32::from(unsafe { (*node).shift });
    let offset = usize::from(state.xa_offset);

    state.xa_index &= (!CHUNK_MASK).wrapping_shl(shift);
    state.xa_index = state.xa_index.wrapping_add(offset.wrapping_shl(shift));
}

/// Finds the next present entry at or after the cursor index.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
pub(crate) unsafe fn find(xas: *mut XaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };

    // SAFETY: The state remains addressable for the complete operation.
    if unsafe { error(xas) } != 0 || state.xa_node.addr() == BOUNDS_ADDRESS {
        return ptr::null_mut();
    }
    if state.xa_index > max {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
        return ptr::null_mut();
    }

    if state.xa_node.is_null() {
        state.xa_index = 1;
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
        return ptr::null_mut();
    } else if state.xa_node.addr() == RESTART_ADDRESS {
        // SAFETY: The top cursor is valid under RCU or lock protection.
        let loaded = unsafe { load(xas) };
        if !loaded.is_null() || not_node(state.xa_node) {
            return loaded;
        }
    } else if
    // SAFETY: The cursor was checked to reference a live node.
    unsafe { (*state.xa_node).shift } == 0
        && state.xa_offset != (state.xa_index & CHUNK_MASK) as u8
    {
        state.xa_offset = (((state.xa_index.wrapping_sub(1)) & CHUNK_MASK) + 1) as u8;
    }

    // SAFETY: A valid non-restart cursor identifies a live node.
    unsafe { next_offset(state) };

    while !state.xa_node.is_null() && state.xa_index <= max {
        if usize::from(state.xa_offset) == CHUNK_SIZE {
            // SAFETY: The cursor identifies a live node.
            state.xa_offset = unsafe { (*state.xa_node).offset }.wrapping_add(1);
            // SAFETY: The node is live under RCU or lock protection.
            state.xa_node = unsafe { parent(state.xa, state.xa_node) };
            continue;
        }

        // SAFETY: The cursor offset identifies a slot in the live node.
        let loaded = unsafe { entry(state.xa, state.xa_node, u32::from(state.xa_offset)) };
        if is_node(loaded) {
            state.xa_node = to_node(loaded);
            state.xa_offset = 0;
            continue;
        }
        if !loaded.is_null() && !is_sibling(loaded) {
            return loaded;
        }

        // SAFETY: The cursor still identifies a live node.
        unsafe { next_offset(state) };
    }

    if state.xa_node.is_null() {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
    }
    ptr::null_mut()
}

/// Finds the next present entry through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_find(xas: *mut XaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find(xas, max) }
}
