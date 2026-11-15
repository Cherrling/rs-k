// SPDX-License-Identifier: GPL-2.0

//! Native XArray store mutation.
//!
//! Rust owns slot replacement, multi-index sibling updates, and node
//! accounting. C retains slot creation, node allocation and release, marks,
//! RCU publication, and the XArray data layout.

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
        unsafe { bindings::rust_xarray_create_for_store(xas, allow_root) }
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
            unsafe { bindings::rust_xarray_squash_marks(xas) };
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
            unsafe { bindings::rust_xarray_free_nodes(xas, next_node) };
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
    unsafe { bindings::rust_xarray_update_node(xas, node, count, values) };
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
