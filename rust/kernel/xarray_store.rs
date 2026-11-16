// SPDX-License-Identifier: GPL-2.0

//! Native XArray store mutation.
//!
//! Rust owns slot replacement, multi-index sibling updates, node accounting,
//! empty-node deletion, and root shrinking. C retains slot creation, node
//! allocation and release, marks, RCU publication, and the XArray data layout.

use crate::bindings;
use core::ffi::c_void;
use macros::export;

type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK;

#[inline(always)]
fn is_invalid(node: *mut XaNode) -> bool {
    node.addr() & 3 != 0
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
fn is_value(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_value(entry) }
}

#[inline(always)]
fn is_zero(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_zero(entry) }
}

#[inline(always)]
fn sibling(offset: u32) -> *mut c_void {
    // SAFETY: The encoding accepts every slot offset.
    unsafe { bindings::xarray_mk_sibling(offset) }
}

#[inline(always)]
fn as_i32(value: bool) -> i32 {
    if value {
        1
    } else {
        0
    }
}

#[cfg(CONFIG_RUST_XARRAY_CREATE)]
/// Creates or selects the slot needed by a store.
///
/// # Safety
///
/// `xas` must be valid and the caller must hold the XArray lock.
unsafe fn create_for_store(xas: *mut XaState, allow_root: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { crate::xarray_create::create(xas, allow_root) }
}

#[cfg(not(CONFIG_RUST_XARRAY_CREATE))]
/// Creates or selects the slot through the retained C adapter.
///
/// # Safety
///
/// `xas` must be valid and the caller must hold the XArray lock.
unsafe fn create_for_store(xas: *mut XaState, allow_root: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_create_for_store(xas, allow_root) }
}

#[cfg(CONFIG_RUST_XARRAY_FREE_NODES)]
/// Removes a detached subtree with the native Rust implementation.
///
/// # Safety
///
/// `top` must be detached from the XArray and the caller must hold the XArray
/// lock.
unsafe fn free_nodes(xas: *mut XaState, top: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { crate::xarray_free_nodes::free_nodes(xas, top) };
}

#[cfg(not(CONFIG_RUST_XARRAY_FREE_NODES))]
/// Removes a detached subtree through the retained C store adapter.
///
/// # Safety
///
/// `top` must be detached from the XArray and the caller must hold the XArray
/// lock.
unsafe fn free_nodes(xas: *mut XaState, top: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_free_nodes(xas, top) };
}

#[cfg(CONFIG_RUST_XARRAY_SQUASH_MARKS)]
/// Combines marks across the current multi-index range.
///
/// # Safety
///
/// `xas` must identify a non-empty sibling range and the caller must hold the
/// XArray lock.
unsafe fn squash_marks(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { crate::xarray_squash::squash_marks(xas) };
}

#[cfg(not(CONFIG_RUST_XARRAY_SQUASH_MARKS))]
/// Combines marks through the retained C store adapter.
///
/// # Safety
///
/// `xas` must identify a non-empty sibling range and the caller must hold the
/// XArray lock.
unsafe fn squash_marks(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_squash_marks(xas) };
}

#[cfg(CONFIG_RUST_XARRAY_DELETE_NODE)]
/// Applies a node accounting delta and enters the native deletion policy.
///
/// # Safety
///
/// `xas` must be valid, the XArray lock must be held, and `node` must be the
/// live node whose slots produced the deltas.
unsafe fn update_node(xas: *mut XaState, node: *mut XaNode, count: i32, values: i32) {
    if node.is_null() || (count == 0 && values == 0) {
        return;
    }

    // SAFETY: The caller guarantees that `node` is live and locked.
    unsafe {
        (*node).count = (*node).count.wrapping_add(count as u8);
        (*node).nr_values = (*node).nr_values.wrapping_add(values as u8);
    }
    // SAFETY: The accounting fields now reflect the completed slot updates.
    unsafe { bindings::xarray_node_check(node) };
    // SAFETY: The caller guarantees the lock and live node.
    unsafe { bindings::rust_xarray_notify_node(xas, node) };
    if count < 0 {
        // SAFETY: The caller guarantees a live cursor and the XArray lock.
        unsafe { crate::xarray_delete_node::delete_node(xas) };
    }
}

#[cfg(not(CONFIG_RUST_XARRAY_DELETE_NODE))]
/// Applies a node accounting delta through the retained C deletion policy.
///
/// # Safety
///
/// `xas` must be valid, the XArray lock must be held, and `node` must be the
/// live node whose slots produced the deltas.
unsafe fn update_node(xas: *mut XaState, node: *mut XaNode, count: i32, values: i32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_update_node(xas, node, count, values) };
}

/// Loads the next locked slot.
///
/// # Safety
///
/// `xas` must be valid, the XArray lock must be held, and `node` and `offset`
/// must identify a slot.
unsafe fn load_slot(xas: *mut XaState, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_locked(xas, node, offset) }
}

/// Publishes one slot under the XArray lock.
///
/// # Safety
///
/// `xas` must be valid, the XArray lock must be held, and a null `node` must
/// select the root slot.
unsafe fn store_slot(xas: *mut XaState, node: *mut XaNode, offset: u32, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_assign_slot(xas, node, offset, entry) }
}

/// Implements the advanced XArray store mutation loop.
///
/// # Safety
///
/// `xas` must be a valid XArray state and the caller must hold the XArray lock.
unsafe fn store(xas: *mut XaState, mut entry: *mut c_void) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let value = is_value(entry);
    let mut first = if entry.is_null() {
        // SAFETY: The state is valid and the XArray lock is held.
        unsafe { bindings::xas_load(xas) }
    } else {
        let allow_root = !is_node(entry) && !is_zero(entry);
        // SAFETY: The state is valid and the XArray lock is held.
        unsafe { create_for_store(xas, allow_root) }
    };

    if is_invalid(state.xa_node) {
        return first;
    }

    let node = state.xa_node;
    let shift = if node.is_null() {
        0
    } else {
        // SAFETY: The null check selects a valid node.
        unsafe { (*node).shift }
    };
    if !node.is_null() && state.xa_shift < shift {
        state.xa_sibs = 0;
    }
    if first == entry && state.xa_sibs == 0 {
        return first;
    }

    let mut next = first;
    let mut offset = u32::from(state.xa_offset);
    let max = offset + u32::from(state.xa_sibs);
    if !node.is_null() {
        if state.xa_sibs != 0 {
            // SAFETY: The cursor identifies a multi-index range in `node`.
            unsafe { squash_marks(xas) };
        }
    }
    if entry.is_null() {
        // SAFETY: Clearing a store initializes every mark for the target.
        unsafe { bindings::xas_init_marks(xas) };
    }

    let mut count = 0i32;
    let mut values = 0i32;
    loop {
        // SAFETY: The cursor identifies the slot being replaced.
        unsafe { store_slot(xas, node, offset, entry) };
        if is_node(next) && (node.is_null() || shift != 0) {
            let next_node = to_node(next);
            // SAFETY: `next` is a node entry removed from the selected slot.
            unsafe { free_nodes(xas, next_node) };
        }
        if node.is_null() {
            break;
        }

        count += as_i32(next.is_null()) - as_i32(entry.is_null());
        values += as_i32(!is_value(first)) - as_i32(!value);
        if !entry.is_null() {
            if offset == max {
                break;
            }
            if !is_sibling(entry) {
                entry = sibling(u32::from(state.xa_offset));
            }
        } else if offset == CHUNK_MASK {
            break;
        }

        offset += 1;
        // SAFETY: `offset` remains within the selected node.
        next = unsafe { load_slot(xas, node, offset) };
        if !is_sibling(next) {
            if entry.is_null() && offset > max {
                break;
            }
            first = next;
        }
    }

    // SAFETY: `node`, `count`, and `values` describe the updated slots.
    unsafe { update_node(xas, node, count, values) };
    first
}

/// Stores an entry through the advanced XArray API.
///
/// # Safety
///
/// `xas` must be valid and the caller must hold the XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_store(xas: *mut XaState, entry: *mut c_void) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { store(xas, entry) }
}
