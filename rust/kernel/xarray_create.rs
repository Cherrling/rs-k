// SPDX-License-Identifier: GPL-2.0

//! Native XArray node-chain creation.
//!
//! Rust owns root selection, expansion control, missing-node creation, node
//! descent, sibling resolution, and target-slot positioning. C retains slab
//! allocation, node layout, mark storage, RCU publication, and update
//! callbacks.

use crate::bindings;
use core::ffi::c_void;
use core::ptr;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SHIFT: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
const FREE_MARK: bindings::xa_mark_t = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;
const LAST_TOP_ADDRESS: usize = 3;

#[inline(always)]
fn is_top(node: *mut XaNode) -> bool {
    node.addr() <= LAST_TOP_ADDRESS
}

/// Reads the locked slot identified by `node` and `offset`.
///
/// A null `node` selects the array root.
///
/// # Safety
///
/// `xas` must point to a locked XArray state. `node` and `offset` must
/// identify a valid slot, or `node` must be null with a zero offset.
unsafe fn load_slot(xas: *mut XaState, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_locked(xas, node, offset) }
}

/// Publishes a locked slot with the required RCU assignment operation.
///
/// A null `node` selects the array root.
///
/// # Safety
///
/// `xas` must point to a locked XArray state. `node` and `offset` must
/// identify a valid slot, or `node` must be null with a zero offset. `entry`
/// must be fully initialized and must remain live after publication.
unsafe fn publish_slot(xas: *mut XaState, node: *mut XaNode, offset: u32, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_assign_slot(xas, node, offset, entry) }
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
fn retry_entry() -> *mut c_void {
    // SAFETY: This helper returns the fixed retry-entry encoding.
    unsafe { bindings::xarray_retry_entry() }
}

#[inline(always)]
fn mk_node(node: *const XaNode) -> *mut c_void {
    // SAFETY: The encoding accepts every live node pointer.
    unsafe { bindings::xarray_mk_node(node) }
}

#[inline(always)]
/// Allocates and initializes one XArray node.
///
/// # Safety
///
/// `xas` must point to a valid, locked XArray state.
unsafe fn alloc_node(xas: *mut XaState, shift: u32) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_alloc_node(xas, shift) }
}

#[inline(always)]
/// Reports whether the XArray tracks free entries.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn track_free(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

#[inline(always)]
/// Reports whether the root uses the zero-entry reservation flag.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn zero_busy(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_zero_busy(xa) }
}

#[inline(always)]
/// Returns the advanced zero-entry encoding.
fn zero_entry() -> *mut c_void {
    // SAFETY: This helper returns the fixed zero-entry encoding.
    unsafe { bindings::xarray_zero_entry() }
}

#[inline(always)]
/// Sets a mark on every slot in a live node.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn node_mark_all(node: *mut XaNode, mark: bindings::xa_mark_t) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_all(node, mark) }
}

/// Calculates the slot offset selected by the cursor index.
///
/// # Safety
///
/// `node` must be live.
unsafe fn get_offset(index: usize, node: *mut XaNode) -> u32 {
    // SAFETY: `node` is live by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });
    ((index >> shift) & CHUNK_MASK) as u32
}

/// Descends one level and resolves sibling entries.
///
/// # Safety
///
/// `xas` must be valid, the XArray lock must be held, and `node` must be the
/// live node selected for the cursor index.
unsafe fn descend(xas: *mut XaState, node: *mut XaNode) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let index = unsafe { (*xas).xa_index };
    // SAFETY: `node` is live by this function's contract.
    let mut offset = unsafe { get_offset(index, node) };
    // SAFETY: The selected slot is in range and protected by the XArray lock.
    let mut entry = unsafe { load_slot(xas, node, offset) };

    // SAFETY: The caller provides a writable state and holds the XArray lock.
    unsafe { (*xas).xa_node = node };
    while is_sibling(entry) {
        offset = to_sibling(entry);
        // SAFETY: A sibling entry contains a valid slot offset.
        entry = unsafe { load_slot(xas, node, offset) };
        // SAFETY: `node` is live by this function's contract.
        if unsafe { (*node).shift } != 0 && is_node(entry) {
            entry = retry_entry();
        }
    }

    // SAFETY: The caller provides a writable state and this is the resolved
    // target offset.
    unsafe { (*xas).xa_offset = offset as u8 };
    entry
}

/// Creates the node chain needed by the current cursor and selects its slot.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold the XArray
/// lock when the state references an XArray.
pub(crate) unsafe fn create(xas: *mut XaState, allow_root: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let xa = state.xa;
    let mut node = state.xa_node;
    let order = i32::from(state.xa_shift);
    let mut slot_node = ptr::null_mut();
    let mut slot_offset = 0;
    let mut entry;
    let mut shift;

    if is_top(node) {
        // SAFETY: The root slot is locked and writable.
        entry = unsafe { load_slot(xas, ptr::null_mut(), 0) };
        state.xa_node = ptr::null_mut();
        // SAFETY: `xa` is live and locked.
        if entry.is_null() && unsafe { zero_busy(xa) } {
            entry = zero_entry();
        }

        // SAFETY: The cursor is valid and the root entry is current.
        shift = unsafe { crate::xarray_expand::expand(xas, entry) };
        if shift < 0 {
            return ptr::null_mut();
        }
        if shift == 0 && !allow_root {
            shift = CHUNK_SHIFT as i32;
        }
        // SAFETY: Expansion may have published a new root.
        entry = unsafe { load_slot(xas, ptr::null_mut(), 0) };
    // SAFETY: The state was validated by this function's contract.
    } else if unsafe { bindings::xarray_error(xas) } != 0 {
        return ptr::null_mut();
    } else if !node.is_null() {
        slot_node = node;
        slot_offset = u32::from(state.xa_offset);
        // SAFETY: A non-null cursor node is live and locked.
        shift = i32::from(unsafe { (*node).shift });
        // SAFETY: The cursor identifies a locked slot.
        entry = unsafe { load_slot(xas, node, slot_offset) };
    } else {
        shift = 0;
        // SAFETY: The root slot is locked.
        entry = unsafe { load_slot(xas, ptr::null_mut(), 0) };
    }

    while shift > order {
        shift -= CHUNK_SHIFT as i32;
        if entry.is_null() {
            // SAFETY: The state is valid and locked.
            node = unsafe { alloc_node(xas, shift as u32) };
            if node.is_null() {
                break;
            }
            // SAFETY: `xa` is live and locked.
            if unsafe { track_free(xa) } {
                // SAFETY: `node` is newly allocated and exclusively owned.
                unsafe { node_mark_all(node, FREE_MARK) };
            }
            // SAFETY: The new node is fully initialized and the parent slot
            // is locked.
            unsafe { publish_slot(xas, slot_node, slot_offset, mk_node(node)) };
        } else if is_node(entry) {
            node = to_node(entry);
        } else {
            break;
        }

        // SAFETY: `node` is live and selected by the cursor path.
        entry = unsafe { descend(xas, node) };
        slot_node = node;
        // SAFETY: `descend` leaves the state at the selected slot.
        slot_offset = u32::from(unsafe { (*xas).xa_offset });
    }

    entry
}

/// Creates or selects the XArray slot for the current cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold the XArray
/// lock when the state references an XArray.
#[export]
unsafe extern "C" fn rust_xarray_create(xas: *mut XaState, allow_root: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { create(xas, allow_root) }
}
