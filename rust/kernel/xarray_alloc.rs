// SPDX-License-Identifier: GPL-2.0

//! Native XArray ID allocation.
//!
//! Rust owns free-mark search, first-fit selection, allocation retries,
//! range-exhaustion handling, next-ID updates, and cyclic wrap reporting.
//! C retains XArray locking, operation preallocation, mark storage, node
//! layout, and tagged-entry encoding.

use crate::bindings;
use core::{cmp, ffi::c_void, mem::MaybeUninit};
use macros::export;

type Gfp = bindings::gfp_t;
type Limit = bindings::xa_limit;
type XArray = bindings::xarray;
type XaState = bindings::xa_state;

const EBUSY: i32 = bindings::rust_xarray_alloc_constant_RUST_XARRAY_ALLOC_EBUSY;
const EINVAL: i32 = bindings::rust_xarray_alloc_constant_RUST_XARRAY_ALLOC_EINVAL;
const FREE_MARK: bindings::xa_mark_t = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;

#[inline(always)]
/// Returns whether a normal XArray API caller supplied an entry that requires
/// the advanced API.
///
/// # Safety
///
/// Entry classification does not dereference the pointer.
unsafe fn is_advanced(entry: *const c_void) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_is_advanced(entry) }
}

#[inline(always)]
/// Emits the existing diagnostic for an advanced entry.
///
/// # Safety
///
/// The caller must have classified `entry` as advanced.
unsafe fn warn_advanced(entry: *const c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_alloc_warn_advanced(entry) };
}

#[inline(always)]
/// Reports whether the XArray tracks free entries.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn track_free(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

#[inline(always)]
/// Emits the existing diagnostic for an XArray without free tracking.
///
/// # Safety
///
/// The caller must have classified `xa` as lacking free tracking.
unsafe fn warn_track_free(xa: *const XArray) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_alloc_warn_track_free(xa) };
}

#[inline(always)]
/// Returns the advanced zero-entry encoding.
fn zero_entry() -> *mut c_void {
    // SAFETY: This helper returns a fixed tagged-entry encoding.
    unsafe { bindings::xarray_zero_entry() }
}

#[inline(always)]
/// Reports whether the cursor exhausted its allocation range.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn is_restart(xas: *const XaState) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_is_restart(xas) }
}

#[inline(always)]
/// Stores an errno in the cursor.
///
/// # Safety
///
/// `xas` must point to a valid, writable XArray state.
unsafe fn set_error(xas: *mut XaState, error: i32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_err(xas, error as isize) };
}

#[inline(always)]
/// Reads the errno stored in the cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
}

#[inline(always)]
/// Retries an allocation after dropping the XArray lock if the allocation
/// flags permit blocking.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
unsafe fn nomem(xas: *mut XaState, gfp: Gfp) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_alloc_nomem(xas, gfp) }
}

#[inline(always)]
/// Reports a pending cyclic-wrap notification.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn wrapped(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_alloc_wrapped(xa) }
}

#[inline(always)]
/// Clears a pending cyclic-wrap notification.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn clear_wrapped(xa: *mut XArray) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_alloc_clear_wrapped(xa) };
}

#[inline(always)]
/// Sets a cyclic-wrap notification for the next allocation.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn set_wrapped(xa: *mut XArray) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_alloc_set_wrapped(xa) };
}

/// Allocates one entry through the native XArray primitives.
///
/// # Safety
///
/// `xa` must point to a valid XArray with free tracking and the caller must
/// hold the XArray lock. `entry` must be a normal-API entry.
unsafe fn allocate(
    xa: *mut XArray,
    id: *mut u32,
    mut entry: *mut c_void,
    limit: Limit,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Entry validation is guaranteed by this function's contract.
    if unsafe { is_advanced(entry.cast_const()) } {
        // SAFETY: The validation result selects the retained diagnostic.
        unsafe { warn_advanced(entry.cast_const()) };
        return EINVAL;
    }
    // SAFETY: The caller guarantees that the XArray is valid.
    if !unsafe { track_free(xa) } {
        // SAFETY: The validation result selects the retained diagnostic.
        unsafe { warn_track_free(xa) };
        return EINVAL;
    }

    if entry.is_null() {
        entry = zero_entry();
    }

    let mut state = MaybeUninit::<XaState>::uninit();
    // SAFETY: The output points to writable storage for one full state.
    unsafe { bindings::xarray_state_init(state.as_mut_ptr(), xa, 0) };
    // SAFETY: The helper initialized the complete state.
    let mut state = unsafe { state.assume_init() };

    loop {
        state.xa_index = limit.min as usize;
        // SAFETY: The state and XArray lock are valid.
        let _ =
            unsafe { crate::xarray_find_marked::find(&mut state, limit.max as usize, FREE_MARK) };

        // SAFETY: The search initialized the cursor outcome.
        if unsafe { is_restart(&state) } {
            // SAFETY: The state remains writable and locked.
            unsafe { set_error(&mut state, EBUSY) };
        } else {
            // SAFETY: A successful search leaves a valid bounded index.
            unsafe { *id = state.xa_index as u32 };
        }

        // SAFETY: The search positioned the cursor at the allocation slot or
        // left it in an error state.
        let _ = unsafe { crate::xarray_store::store(&mut state, entry) };
        // SAFETY: The free mark belongs to the selected slot or the store did
        // not change an error state.
        unsafe { crate::xarray_marks::mark_clear(&state, FREE_MARK) };

        // SAFETY: The cursor and XArray lock belong to this operation.
        if !unsafe { nomem(&mut state, gfp) } {
            // SAFETY: The cursor remains valid after the retry decision.
            return unsafe { error(&state) };
        }
    }
}

/// Implements the locked first-fit allocation API.
///
/// # Safety
///
/// `xa` and `id` must be valid, `limit` must describe the caller's requested
/// ID range, and the caller must hold the XArray lock.
unsafe fn alloc(xa: *mut XArray, id: *mut u32, entry: *mut c_void, limit: Limit, gfp: Gfp) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { allocate(xa, id, entry, limit, gfp) }
}

/// Implements the locked cyclic allocation API.
///
/// # Safety
///
/// `xa`, `id`, and `next` must be valid, `limit` must describe the caller's
/// requested ID range, and the caller must hold the XArray lock.
unsafe fn alloc_cyclic(
    xa: *mut XArray,
    id: *mut u32,
    entry: *mut c_void,
    mut limit: Limit,
    next: *mut u32,
    gfp: Gfp,
) -> i32 {
    let min = limit.min;
    // SAFETY: The caller guarantees a readable next-ID pointer.
    limit.min = cmp::max(min, unsafe { *next });
    // SAFETY: The allocation arguments are valid and the lock is held.
    let mut ret = unsafe { allocate(xa, id, entry, limit, gfp) };

    // SAFETY: The caller guarantees the XArray lock.
    if unsafe { wrapped(xa) } && ret == 0 {
        // SAFETY: The caller guarantees the XArray lock.
        unsafe { clear_wrapped(xa) };
        ret = 1;
    }

    if ret < 0 && limit.min > min {
        limit.min = min;
        // SAFETY: The retry uses the caller's minimum and the same lock.
        ret = unsafe { allocate(xa, id, entry, limit, gfp) };
        if ret == 0 {
            ret = 1;
        }
    }

    if ret >= 0 {
        // SAFETY: A successful allocation initialized the ID.
        let next_id = unsafe { *id }.wrapping_add(1);
        // SAFETY: The caller guarantees a writable next-ID pointer.
        unsafe { *next = next_id };
        if next_id == 0 {
            // SAFETY: The caller guarantees the XArray lock.
            unsafe { set_wrapped(xa) };
        }
    }

    ret
}

/// Allocates an ID through the production C ABI.
///
/// # Safety
///
/// The C caller must provide valid pointers and hold the XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_alloc(
    xa: *mut XArray,
    id: *mut u32,
    entry: *mut c_void,
    limit: Limit,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc(xa, id, entry, limit, gfp) }
}

/// Allocates a cyclic ID through the production C ABI.
///
/// # Safety
///
/// The C caller must provide valid pointers and hold the XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_alloc_cyclic(
    xa: *mut XArray,
    id: *mut u32,
    entry: *mut c_void,
    limit: Limit,
    next: *mut u32,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc_cyclic(xa, id, entry, limit, next, gfp) }
}
