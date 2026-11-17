// SPDX-License-Identifier: GPL-2.0

//! Native IDR iteration.
//!
//! Rust owns base-relative cursor normalization, sparse traversal control,
//! retry recovery, and signed-ID overflow handling. C retains IDR and XArray
//! layouts, RCU slot access, entry classification, and the overflow diagnostic.

use crate::bindings;
use core::{ffi::c_void, mem::MaybeUninit};
use macros::export;

type Idr = bindings::idr;
type XaState = bindings::xa_state;

const INTERNAL_MASK: usize = 3;
const RESTART_ADDRESS: usize = 3;
const INT_MAX: usize = i32::MAX as usize;

#[inline(always)]
fn is_internal(entry: *const c_void) -> bool {
    entry.addr() & INTERNAL_MASK != 0
}

#[inline(always)]
/// Reports whether an entry asks the caller to retry under RCU protection.
///
/// # Safety
///
/// Entry classification does not dereference the pointer.
unsafe fn is_retry(entry: *const c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_retry(entry) }
}

/// Restarts a walk at the current index.
///
/// # Safety
///
/// `state` must point to a valid XArray cursor.
unsafe fn reset(state: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_reset(state) };
}

/// Finds the first visible IDR entry at or after `nextid`.
///
/// # Safety
///
/// `idr` and `nextid` must be valid. The caller must provide the synchronization
/// required by `idr_get_next_ul()`.
unsafe fn get_next_ul(idr: *mut Idr, nextid: *mut usize) -> *mut c_void {
    // SAFETY: The caller guarantees a valid IDR.
    let base = unsafe { (*idr).idr_base } as usize;
    // SAFETY: The caller guarantees a readable next-ID pointer.
    let requested = unsafe { *nextid };
    let start = requested.saturating_sub(base);

    let mut state = MaybeUninit::<XaState>::uninit();
    // SAFETY: The output points to writable storage for one complete state.
    unsafe {
        bindings::xarray_state_init(
            state.as_mut_ptr(),
            core::ptr::addr_of_mut!((*idr).idr_rt),
            start,
        )
    };
    // SAFETY: The helper initialized the complete state.
    let mut state = unsafe { state.assume_init() };

    let entry = loop {
        // SAFETY: The cursor is valid, and the public IDR API requires the
        // caller to hold RCU protection or exclude concurrent writers.
        let entry = unsafe { crate::xarray_find::find_external(&mut state, usize::MAX) };
        if entry.is_null() {
            return core::ptr::null_mut();
        }

        let entry_const = entry.cast_const();
        if !is_internal(entry_const) {
            break entry;
        }

        // A non-retry internal entry is visible only from a live node. This
        // matches the retained radix-tree iterator rule for value entries.
        // SAFETY: The classification helper does not dereference the entry.
        if !unsafe { is_retry(entry_const) } && state.xa_node.addr() > RESTART_ADDRESS {
            break entry;
        }

        // SAFETY: The cursor remains valid under the caller's protection.
        unsafe { reset(&mut state) };
    };

    // SAFETY: The caller guarantees a writable next-ID pointer.
    unsafe { *nextid = state.xa_index.wrapping_add(base) };
    entry
}

/// Finds the next signed ID for `idr_get_next()`.
///
/// # Safety
///
/// `idr` and `nextid` must be valid. The caller must provide the synchronization
/// required by `idr_get_next()`.
unsafe fn get_next(idr: *mut Idr, nextid: *mut i32) -> *mut c_void {
    // SAFETY: The caller guarantees a readable next-ID pointer.
    let mut id = unsafe { *nextid } as usize;
    // SAFETY: The cursor and caller-provided synchronization are valid.
    let entry = unsafe { get_next_ul(idr, &mut id) };

    if id > INT_MAX {
        // SAFETY: The helper only emits the retained diagnostic.
        unsafe { bindings::rust_idr_warn_id_overflow() };
        return core::ptr::null_mut();
    }

    // SAFETY: The caller guarantees a writable next-ID pointer.
    unsafe { *nextid = id as i32 };
    entry
}

/// Implements `idr_get_next_ul()`.
///
/// # Safety
///
/// `idr` and `nextid` must be valid. The caller must provide the synchronization
/// required by the public API.
#[export]
unsafe extern "C" fn rust_idr_get_next_ul(idr: *mut Idr, nextid: *mut usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { get_next_ul(idr, nextid) }
}

/// Implements `idr_get_next()`.
///
/// # Safety
///
/// `idr` and `nextid` must be valid. The caller must provide the synchronization
/// required by the public API.
#[export]
unsafe extern "C" fn rust_idr_get_next(idr: *mut Idr, nextid: *mut i32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { get_next(idr, nextid) }
}
