// SPDX-License-Identifier: GPL-2.0

//! Native XArray adjacent-index cursor movement.
//!
//! Rust owns index wrapping, offset adjustment, parent retreat, child descent,
//! and the restart and error dispatch for `xas_next()` and `xas_prev()`. C
//! retains cursor layouts, tagged classification, RCU slot access, and load.

use crate::bindings;
use core::ffi::c_void;
use core::ptr;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SIZE: usize = 1usize << bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;

#[inline(always)]
fn frozen(node: *mut XaNode) -> bool {
    // SAFETY: Frozen-state classification does not dereference the pointer.
    unsafe { bindings::xarray_frozen(node) }
}

#[inline(always)]
fn not_node(node: *mut XaNode) -> bool {
    // SAFETY: Node-state classification does not dereference the pointer.
    unsafe { bindings::xarray_not_node(node) }
}

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

/// Returns the offset selected by the current index.
///
/// # Safety
///
/// `state` must identify a live node.
unsafe fn get_offset(state: &XaState) -> u8 {
    let node = state.xa_node;
    // SAFETY: Guaranteed by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });
    ((state.xa_index >> shift) & CHUNK_MASK) as u8
}

/// Sets the offset from the current index.
///
/// # Safety
///
/// `state` must identify a live node.
unsafe fn set_offset(state: &mut XaState) {
    let node = state.xa_node;
    // SAFETY: Guaranteed by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });
    state.xa_offset = ((state.xa_index >> shift) & CHUNK_MASK) as u8;
}

/// Moves to the previous index and returns its entry.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
pub(crate) unsafe fn prev(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };

    if !frozen(state.xa_node) {
        state.xa_index = state.xa_index.wrapping_sub(1);
    }
    if state.xa_node.is_null() {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
        return ptr::null_mut();
    }
    if not_node(state.xa_node) {
        // SAFETY: The top cursor is valid under RCU or lock protection.
        return unsafe { load(xas) };
    }

    // SAFETY: The cursor identifies a live node.
    if state.xa_offset != unsafe { get_offset(state) } {
        state.xa_offset = state.xa_offset.wrapping_sub(1);
    }

    while state.xa_offset == u8::MAX {
        // SAFETY: The cursor identifies a live node.
        state.xa_offset = unsafe { (*state.xa_node).offset }.wrapping_sub(1);
        // SAFETY: The node is live under RCU or lock protection.
        state.xa_node = unsafe { parent(state.xa, state.xa_node) };
        if state.xa_node.is_null() {
            // SAFETY: The state is writable under RCU or lock protection.
            unsafe { set_bounds(xas) };
            return ptr::null_mut();
        }
    }

    loop {
        // SAFETY: The cursor offset identifies a slot in the live node.
        let loaded = unsafe { entry(state.xa, state.xa_node, u32::from(state.xa_offset)) };
        if !is_node(loaded) {
            return loaded;
        }

        state.xa_node = to_node(loaded);
        // SAFETY: The cursor identifies the selected live node.
        unsafe { set_offset(state) };
    }
}

/// Moves to the next index and returns its entry.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
pub(crate) unsafe fn next(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };

    if !frozen(state.xa_node) {
        state.xa_index = state.xa_index.wrapping_add(1);
    }
    if state.xa_node.is_null() {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
        return ptr::null_mut();
    }
    if not_node(state.xa_node) {
        // SAFETY: The top cursor is valid under RCU or lock protection.
        return unsafe { load(xas) };
    }

    // SAFETY: The cursor identifies a live node.
    if state.xa_offset != unsafe { get_offset(state) } {
        state.xa_offset = state.xa_offset.wrapping_add(1);
    }

    while usize::from(state.xa_offset) == CHUNK_SIZE {
        // SAFETY: The cursor identifies a live node.
        state.xa_offset = unsafe { (*state.xa_node).offset }.wrapping_add(1);
        // SAFETY: The node is live under RCU or lock protection.
        state.xa_node = unsafe { parent(state.xa, state.xa_node) };
        if state.xa_node.is_null() {
            // SAFETY: The state is writable under RCU or lock protection.
            unsafe { set_bounds(xas) };
            return ptr::null_mut();
        }
    }

    loop {
        // SAFETY: The cursor offset identifies a slot in the live node.
        let loaded = unsafe { entry(state.xa, state.xa_node, u32::from(state.xa_offset)) };
        if !is_node(loaded) {
            return loaded;
        }

        state.xa_node = to_node(loaded);
        // SAFETY: The cursor identifies the selected live node.
        unsafe { set_offset(state) };
    }
}

/// Moves to the previous index through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_prev(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prev(xas) }
}

/// Moves to the next index through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_next(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { next(xas) }
}
