// SPDX-License-Identifier: GPL-2.0

//! Native XArray load traversal.
//!
//! Rust owns root entry selection, node descent, sibling expansion, and the
//! RCU retry loop. C retains RCU and tagged-entry primitives, slot loads, and
//! the XArray data layout.

use crate::{bindings, sync::rcu};
use core::ffi::c_void;
use core::{mem::MaybeUninit, ptr};
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;

/// Loads the current XArray head under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn head(xa: *mut XArray) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_head(xa) }
}

/// Loads one non-sibling slot under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` and `node` must be valid, and `offset` must be in range.
unsafe fn entry(xa: *mut XArray, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_entry(xa, node, offset) }
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
fn is_sibling(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_sibling(entry) }
}

#[inline(always)]
fn to_sibling(entry: *mut c_void) -> u32 {
    // SAFETY: The caller checks that `entry` is a sibling entry first.
    unsafe { bindings::xarray_to_sibling(entry) as u32 }
}

#[inline(always)]
fn is_retry(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_retry(entry) }
}

#[inline(always)]
fn is_zero(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_zero(entry) }
}

/// Loads the entry at the current valid cursor state.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
unsafe fn reload(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let node = state.xa_node;

    if node.is_null() {
        // SAFETY: The state points to a valid XArray.
        return unsafe { head(state.xa) };
    }

    // SAFETY: A valid state with a non-NULL node points to a real node.
    let shift = unsafe { (*node).shift as usize };
    let mut offset = ((state.xa_index >> shift) & CHUNK_MASK) as u32;
    // SAFETY: `offset` is masked to the node slot count.
    let loaded = unsafe { entry(state.xa, node, offset) };

    if !is_sibling(loaded) {
        return loaded;
    }

    offset = to_sibling(loaded);
    // SAFETY: Sibling entries contain a valid slot offset.
    unsafe { entry(state.xa, node, offset) }
}

/// Starts a load, preserving an existing cursor when it is valid.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
unsafe fn start(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };

    if state.xa_node.addr() & 3 == 0 {
        // SAFETY: A valid cursor satisfies `reload`'s contract.
        return unsafe { reload(xas) };
    }

    // SAFETY: The state points to a valid XArray.
    if unsafe { bindings::xarray_error(xas) } != 0 {
        return ptr::null_mut();
    }

    // SAFETY: The state points to a valid XArray.
    let loaded = unsafe { head(state.xa) };
    if !is_node(loaded) {
        if state.xa_index != 0 {
            // SAFETY: The state is writable by this function's contract.
            unsafe { bindings::xarray_set_bounds(xas) };
            return ptr::null_mut();
        }
    } else {
        let node = to_node(loaded);
        // SAFETY: A node entry decodes to a valid node.
        if state.xa_index >> unsafe { (*node).shift as usize } > CHUNK_MASK {
            // SAFETY: The state is writable by this function's contract.
            unsafe { bindings::xarray_set_bounds(xas) };
            return ptr::null_mut();
        }
    }

    state.xa_node = ptr::null_mut();
    loaded
}

/// Descends one level, resolving sibling entries in the selected node.
///
/// # Safety
///
/// `xas` must be valid and `node` must be the node selected for its index.
unsafe fn descend(xas: *mut XaState, node: *mut XaNode) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    // SAFETY: `node` is valid by this function's contract.
    let shift = unsafe { (*node).shift as usize };
    let mut offset = ((state.xa_index >> shift) & CHUNK_MASK) as u32;
    // SAFETY: `offset` is masked to the node slot count.
    let mut loaded = unsafe { entry(state.xa, node, offset) };

    state.xa_node = node;
    while is_sibling(loaded) {
        offset = to_sibling(loaded);
        // SAFETY: Sibling entries contain a valid slot offset.
        loaded = unsafe { entry(state.xa, node, offset) };
        if shift != 0 && is_node(loaded) {
            // SAFETY: This helper obtains the fixed retry-entry encoding.
            loaded = unsafe { bindings::xarray_retry_entry() };
        }
    }

    state.xa_offset = offset as u8;
    loaded
}

/// Implements the advanced XArray load walk.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
unsafe fn load_advanced(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let mut loaded = unsafe { start(xas) };

    while is_node(loaded) {
        let node = to_node(loaded);
        // SAFETY: `xas` and `node` are valid by this function's contract.
        if unsafe { (*xas).xa_shift as usize } > unsafe { (*node).shift as usize } {
            break;
        }

        // SAFETY: The node was selected by `start` or the previous descent.
        loaded = unsafe { descend(xas, node) };
        // SAFETY: The node remains valid while the caller holds RCU or lock.
        if unsafe { (*node).shift } == 0 {
            break;
        }
    }

    loaded
}

/// Loads an entry through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_load_advanced(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { load_advanced(xas) }
}

/// Loads an entry while holding an RCU read-side critical section.
///
/// # Safety
///
/// `xa` must point to a valid, initialized XArray.
#[export]
unsafe extern "C" fn rust_xarray_load(xa: *mut XArray, index: usize) -> *mut c_void {
    let _rcu = rcu::read_lock();
    let mut state = MaybeUninit::<XaState>::uninit();
    let xas = state.as_mut_ptr();

    // SAFETY: `xas` points to writable storage, and `xa` is valid.
    unsafe { bindings::xarray_state_init(xas, xa, index) };

    loop {
        // SAFETY: The state was initialized, and this function holds RCU.
        let loaded = unsafe { load_advanced(xas) };
        let loaded = if is_zero(loaded) {
            ptr::null_mut()
        } else {
            loaded
        };

        if !is_retry(loaded) {
            return loaded;
        }

        // SAFETY: A retry resets the existing state for another root walk.
        unsafe { bindings::xarray_reset(xas) };
    }
}
