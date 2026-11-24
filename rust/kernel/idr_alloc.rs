// SPDX-License-Identifier: GPL-2.0

//! Native IDR allocation.
//!
//! Rust directly defines the public entry points and owns base-relative range
//! normalization, first-fit free-slot selection, hole reuse, range exhaustion,
//! cyclic search and wrap, next-ID publication ordering, and cursor updates.
//! C retains XArray node allocation, slot publication, free-mark storage, node
//! layout, tagged-entry encoding, and export metadata.

use crate::bindings;
use core::{ffi::c_void, mem::MaybeUninit};
use macros::export;

type Gfp = bindings::gfp_t;
type Idr = bindings::idr;
type XaState = bindings::xa_state;

const EINVAL: i32 = bindings::rust_idr_constant_RUST_IDR_EINVAL;
const ENOMEM: i32 = bindings::rust_idr_constant_RUST_IDR_ENOMEM;
const ENOSPC: i32 = bindings::rust_idr_constant_RUST_IDR_ENOSPC;
const FREE_MARK: bindings::xa_mark_t =
    bindings::rust_idr_constant_RUST_IDR_FREE_MARK as bindings::xa_mark_t;
const INT_MAX: u32 = i32::MAX as u32;

#[inline(always)]
/// Restores the IDR marker and emits the existing diagnostic for a malformed
/// IDR whose marker was cleared.
///
/// # Safety
///
/// `idr` must point to a valid IDR.
unsafe fn prepare(idr: *mut Idr) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_idr_prepare(idr) };
}

#[inline(always)]
/// Reads the error stored in an XArray cursor.
///
/// # Safety
///
/// `state` must point to a valid XArray cursor.
unsafe fn error(state: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(state) }
}

#[inline(always)]
/// Reports whether an XArray cursor is in the restart state.
///
/// # Safety
///
/// `state` must point to a valid XArray cursor.
unsafe fn is_restart(state: *const XaState) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_is_restart(state) }
}

/// Allocates one base-relative ID while the caller holds the IDR lock.
///
/// # Safety
///
/// `idr`, `ptr`, and `nextid` must be valid. The caller must hold the IDR lock.
unsafe fn allocate_locked(
    idr: *mut Idr,
    ptr: *mut c_void,
    nextid: *mut u32,
    max: usize,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prepare(idr) };

    // SAFETY: The caller guarantees a valid IDR.
    let base = unsafe { (*idr).idr_base } as usize;
    if max < base {
        return ENOSPC;
    }

    // SAFETY: The caller guarantees a readable next-ID pointer.
    let requested = unsafe { *nextid };
    let start = (requested as usize).saturating_sub(base);
    let maximum = max - base;

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

    loop {
        state.xa_index = start;
        // SAFETY: The cursor and caller-owned IDR lock are valid.
        let _ = unsafe { crate::xarray_find_marked::find_external(&mut state, maximum, FREE_MARK) };
        // SAFETY: The search initialized the cursor outcome.
        let search_error = unsafe { error(&state) };
        if search_error != 0 {
            return search_error;
        }
        // SAFETY: The search leaves this sentinel when the range has no free slot.
        if unsafe { is_restart(&state) } {
            return ENOSPC;
        }

        let index = state.xa_index;
        // SAFETY: The caller guarantees a writable next-ID pointer.
        let old = unsafe { *nextid };
        // Publish the ID before the pointer becomes visible to RCU readers.
        // SAFETY: The caller guarantees a writable next-ID pointer.
        unsafe { *nextid = (index as u32).wrapping_add(base as u32) };

        // SAFETY: The caller-owned IDR lock excludes concurrent writers.
        let _ = unsafe { crate::xarray_create::create_external(&mut state, false) };
        // SAFETY: The search and creation operations produced a valid cursor.
        let ret = unsafe { error(&state) };
        if ret == ENOMEM {
            // SAFETY: A failed store did not publish the entry.
            unsafe { *nextid = old };
            // SAFETY: The cursor retains the failed state and caller-held lock.
            if unsafe { bindings::xas_nomem(&mut state, gfp) } {
                continue;
            }
            return ret;
        }
        if ret != 0 {
            // SAFETY: The failed store did not publish an entry.
            unsafe { *nextid = old };
            return ret;
        }

        // SAFETY: Creation selected the target slot and the caller holds the
        // IDR lock. The C boundary preserves IDR slot accounting.
        unsafe { bindings::rust_idr_commit(idr, &mut state, ptr) };

        return 0;
    }
}

/// Implements `idr_alloc_u32()`.
///
/// # Safety
///
/// `idr`, `ptr`, and `nextid` must be valid. The caller must hold the IDR lock.
unsafe fn alloc_u32(
    idr: *mut Idr,
    ptr: *mut c_void,
    nextid: *mut u32,
    max: usize,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { allocate_locked(idr, ptr, nextid, max, gfp) }
}

/// Implements `idr_alloc()`.
///
/// # Safety
///
/// `idr` and `ptr` must be valid. The caller must hold the IDR lock.
unsafe fn alloc(idr: *mut Idr, ptr: *mut c_void, start: i32, end: i32, gfp: Gfp) -> i32 {
    if start < 0 {
        // SAFETY: The helper only emits the retained diagnostic.
        unsafe { bindings::rust_idr_warn_invalid_start() };
        return EINVAL;
    }

    let mut id = start as u32;
    let max = if end > 0 {
        end as usize - 1
    } else {
        INT_MAX as usize
    };
    // SAFETY: The range was normalized and the caller holds the IDR lock.
    let ret = unsafe { allocate_locked(idr, ptr, &mut id, max, gfp) };
    if ret != 0 {
        ret
    } else {
        id as i32
    }
}

/// Implements `idr_alloc_cyclic()`.
///
/// # Safety
///
/// `idr` and `ptr` must be valid. The caller must hold the IDR lock.
unsafe fn alloc_cyclic(idr: *mut Idr, ptr: *mut c_void, start: i32, end: i32, gfp: Gfp) -> i32 {
    // SAFETY: The caller guarantees a valid IDR.
    let mut id = unsafe { (*idr).idr_next };
    let max = if end > 0 {
        end as usize - 1
    } else {
        INT_MAX as usize
    };

    if (id as i32) < start {
        id = start as u32;
    }

    // SAFETY: The normalized ID and caller-held lock satisfy the allocator.
    let mut ret = unsafe { allocate_locked(idr, ptr, &mut id, max, gfp) };
    if ret == ENOSPC && id > start as u32 {
        id = start as u32;
        // SAFETY: The second attempt uses the caller's lower bound.
        ret = unsafe { allocate_locked(idr, ptr, &mut id, max, gfp) };
    }
    if ret != 0 {
        return ret;
    }

    // SAFETY: A successful allocation initialized the ID.
    unsafe { (*idr).idr_next = id.wrapping_add(1) };
    id as i32
}

/// Implements `idr_alloc_u32()`.
///
/// # Safety
///
/// The public IDR allocation contract applies.
#[export]
unsafe extern "C" fn idr_alloc_u32(
    idr: *mut Idr,
    ptr: *mut c_void,
    nextid: *mut u32,
    max: usize,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc_u32(idr, ptr, nextid, max, gfp) }
}

/// Implements `idr_alloc()`.
///
/// # Safety
///
/// The public IDR allocation contract applies.
#[export]
unsafe extern "C" fn idr_alloc(
    idr: *mut Idr,
    ptr: *mut c_void,
    start: i32,
    end: i32,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc(idr, ptr, start, end, gfp) }
}

/// Implements `idr_alloc_cyclic()`.
///
/// # Safety
///
/// The public IDR allocation contract applies.
#[export]
unsafe extern "C" fn idr_alloc_cyclic(
    idr: *mut Idr,
    ptr: *mut c_void,
    start: i32,
    end: i32,
    gfp: Gfp,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { alloc_cyclic(idr, ptr, start, end, gfp) }
}
