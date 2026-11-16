// SPDX-License-Identifier: GPL-2.0

//! Native XArray range conflict search.
//!
//! Rust owns root start, child descent, range-boundary checks, sibling
//! skipping, locked slot access, and parent retreat. C retains the XArray node
//! and state layouts and tagged pointer primitives.

use crate::{bindings, xarray_load};
use core::ffi::c_void;
use core::ptr;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: u8 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as u8;

#[inline(always)]
fn top(node: *mut XaNode) -> bool {
    // SAFETY: Cursor classification does not dereference the pointer.
    unsafe { bindings::xarray_top(node) }
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
/// Reports whether the cursor contains an error.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
}

/// Loads one slot while the caller holds the XArray lock.
///
/// # Safety
///
/// `xa` and `node` must be valid and `offset` must be in range.
unsafe fn entry_locked(xa: *const XArray, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_entry_locked(xa, node, offset) }
}

/// Loads the parent while the caller holds the XArray lock.
///
/// # Safety
///
/// `xa` and `node` must be valid.
unsafe fn parent_locked(xa: *const XArray, node: *mut XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent_locked(xa, node) }
}

/// Finds the next present entry in the range described by the cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
pub(crate) unsafe fn find_conflict(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { error(xas) } != 0 {
        return ptr::null_mut();
    }

    // SAFETY: Guaranteed by this function's contract.
    let mut node = unsafe { (*xas).xa_node };
    if node.is_null() {
        return ptr::null_mut();
    }

    if top(node) {
        // SAFETY: The top cursor is valid under the caller's lock.
        let mut current = unsafe { xarray_load::start(xas) };
        if current.is_null() {
            return ptr::null_mut();
        }

        while is_node(current) {
            node = to_node(current);
            // SAFETY: The selected node is valid under the caller's lock.
            current = unsafe { xarray_load::descend(xas, node) };
        }
        if !current.is_null() {
            return current;
        }

        // SAFETY: Guaranteed by this function's contract and the descent.
        node = unsafe { (*xas).xa_node };
    }

    // SAFETY: A non-top, non-error cursor identifies a live node.
    if unsafe { (*node).shift > (*xas).xa_shift } {
        return ptr::null_mut();
    }

    loop {
        // SAFETY: Guaranteed by this function's contract.
        node = unsafe { (*xas).xa_node };
        if node.is_null() {
            break;
        }

        // SAFETY: The non-null cursor identifies a live node.
        if unsafe { (*node).shift } == unsafe { (*xas).xa_shift } {
            // SAFETY: Guaranteed by this function's contract.
            let offset = unsafe { (*xas).xa_offset };
            // SAFETY: Guaranteed by this function's contract.
            let siblings = unsafe { (*xas).xa_sibs };
            if offset & siblings == siblings {
                break;
            }
        } else if
        // SAFETY: The cursor identifies a live node.
        unsafe { (*xas).xa_offset } == CHUNK_MASK {
            // SAFETY: The cursor identifies a live node.
            unsafe { (*xas).xa_offset = (*node).offset };
            // SAFETY: The cursor identifies a live node under the lock.
            unsafe { (*xas).xa_node = parent_locked((*xas).xa, node) };
            // SAFETY: Guaranteed by this function's contract.
            if unsafe { (*xas).xa_node }.is_null() {
                break;
            }
            continue;
        }

        // SAFETY: The live node has no offset above the chunk mask.
        let offset = unsafe { (*xas).xa_offset }.wrapping_add(1);
        // SAFETY: The cursor remains writable under the caller's lock.
        unsafe { (*xas).xa_offset = offset };
        // SAFETY: The offset is in range and the caller holds the lock.
        let mut current = unsafe { entry_locked((*xas).xa, node, u32::from(offset)) };
        if is_sibling(current) {
            continue;
        }

        while is_node(current) {
            let child = to_node(current);
            // SAFETY: The child is selected by a valid node entry.
            unsafe { (*xas).xa_node = child };
            // SAFETY: The cursor remains writable under the caller's lock.
            unsafe { (*xas).xa_offset = 0 };
            // SAFETY: The first slot of a live node and the lock are valid.
            current = unsafe { entry_locked((*xas).xa, child, 0) };
        }

        if !current.is_null() {
            return current;
        }
    }

    // SAFETY: The cursor remains writable under the caller's lock.
    unsafe {
        (*xas).xa_offset = (*xas).xa_offset.wrapping_sub((*xas).xa_sibs);
    }
    ptr::null_mut()
}

/// Finds the next range conflict through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_find_conflict(xas: *mut XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_conflict(xas) }
}
