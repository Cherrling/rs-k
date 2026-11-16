// SPDX-License-Identifier: GPL-2.0

//! Native XArray range creation.
//!
//! Rust owns range-boundary calculation, backward chunk traversal, ancestor
//! cursor repositioning, and error-state restoration. C retains node and
//! state layouts, parent lookup, error classification, and node creation.

use crate::bindings;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SIZE: usize = 1usize << bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;

#[inline(always)]
fn is_node(state: &XaState) -> bool {
    state.xa_node.addr() & 3 == 0 && !state.xa_node.is_null()
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
/// Returns the parent of a live node under the XArray lock.
///
/// # Safety
///
/// `xa` must be valid, `node` must be live, and the XArray lock must be held.
unsafe fn parent_locked(xa: *const XArray, node: *const XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent_locked(xa, node) }
}

/// Creates or selects the slot needed by the current cursor.
///
/// # Safety
///
/// `xas` must be valid. The caller must hold the XArray lock when the state
/// references an XArray.
unsafe fn create(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let _ = unsafe { crate::xarray_create::create(xas, true) };
}

/// Calculates the slot offset selected by an index.
///
/// # Safety
///
/// `node` must be live.
unsafe fn get_offset(index: usize, node: *const XaNode) -> u8 {
    // SAFETY: `node` is live by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });
    ((index >> shift) & CHUNK_MASK) as u8
}

/// Prepares every slot covered by the current multi-index range.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold the XArray
/// lock when the state references an XArray.
pub(crate) unsafe fn create_range(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let index = state.xa_index;
    let shift = state.xa_shift;
    let sibs = state.xa_sibs;

    state.xa_index |= ((usize::from(sibs) + 1) << u32::from(shift)) - 1;
    if is_node(state) {
        // SAFETY: The state check above establishes a live node.
        if unsafe { (*state.xa_node).shift } == shift {
            state.xa_offset |= sibs;
        }
    }
    state.xa_shift = 0;
    state.xa_sibs = 0;

    loop {
        // SAFETY: The cursor is valid and the caller holds the lock.
        unsafe { create(xas) };
        // SAFETY: The cursor remains addressable after creation.
        if unsafe { error(xas) } != 0 {
            state.xa_shift = shift;
            state.xa_sibs = sibs;
            state.xa_index = index;
            return;
        }
        if state.xa_index <= (index | CHUNK_MASK) {
            state.xa_index = index;
            if !state.xa_node.is_null() {
                // SAFETY: A successful creation leaves a live cursor node.
                state.xa_offset = unsafe { get_offset(index, state.xa_node) };
            }
            return;
        }

        state.xa_index -= CHUNK_SIZE;
        loop {
            let node = state.xa_node;
            // SAFETY: A range crossing this boundary has a live cursor node.
            if unsafe { (*node).shift } >= shift {
                break;
            }
            // SAFETY: The node is live and the caller holds the XArray lock.
            state.xa_node = unsafe { parent_locked(state.xa, node) };
            // SAFETY: `node` is live and its offset identifies its parent slot.
            state.xa_offset = unsafe { (*node).offset }.wrapping_sub(1);
            // SAFETY: `node` remains live because the XArray lock is held.
            if unsafe { (*node).offset } != 0 {
                break;
            }
        }
    }
}

/// Prepares all slots covered by an XArray multi-index range.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold the XArray
/// lock when the state references an XArray.
#[export]
unsafe extern "C" fn rust_xarray_create_range(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { create_range(xas) }
}
