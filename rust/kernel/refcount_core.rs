// SPDX-License-Identifier: GPL-2.0-only

//! Native saturation state machine for `refcount_t`.
//!
//! Rust owns every increment, decrement, saturation, limited-acquire, and
//! conditional-locking decision.  C helpers retain architecture atomics,
//! memory barriers, warning sites, and kernel locking primitives.

use crate::bindings;
use core::{
    ffi::{c_int, c_uint},
    ptr,
};
use macros::export;

type Ref = bindings::refcount_t;
type Saturation = bindings::refcount_saturation_type;

const REFCOUNT_SATURATED: c_int = c_int::MIN / 2;

const ADD_NOT_ZERO_OVF: Saturation = bindings::refcount_saturation_type_REFCOUNT_ADD_NOT_ZERO_OVF;
const ADD_OVF: Saturation = bindings::refcount_saturation_type_REFCOUNT_ADD_OVF;
const ADD_UAF: Saturation = bindings::refcount_saturation_type_REFCOUNT_ADD_UAF;
const SUB_UAF: Saturation = bindings::refcount_saturation_type_REFCOUNT_SUB_UAF;
const DEC_LEAK: Saturation = bindings::refcount_saturation_type_REFCOUNT_DEC_LEAK;

/// Reads a refcount atomically.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.
#[inline]
unsafe fn read(r: *mut Ref) -> c_int {
    // SAFETY: The caller guarantees that `r` is valid.
    unsafe { bindings::rust_refcount_atomic_read(r) }
}

/// Writes a refcount atomically.
///
/// # Safety
///
/// `r` must point to a valid, writable `refcount_t`.
#[inline]
unsafe fn set(r: *mut Ref, value: c_int) {
    // SAFETY: The caller guarantees that `r` is valid and writable.
    unsafe { bindings::rust_refcount_atomic_set(r, value) };
}

/// Attempts a relaxed compare-exchange.
///
/// # Safety
///
/// `r` and `old` must be valid pointers.
#[inline]
unsafe fn try_cmpxchg_relaxed(r: *mut Ref, old: *mut c_int, new: c_int) -> bool {
    // SAFETY: The caller guarantees that `r` and `old` are valid.
    unsafe { bindings::rust_refcount_try_cmpxchg_relaxed(r, old, new) }
}

/// Attempts an acquire compare-exchange.
///
/// # Safety
///
/// `r` and `old` must be valid pointers.
#[inline]
unsafe fn try_cmpxchg_acquire(r: *mut Ref, old: *mut c_int, new: c_int) -> bool {
    // SAFETY: The caller guarantees that `r` and `old` are valid.
    unsafe { bindings::rust_refcount_try_cmpxchg_acquire(r, old, new) }
}

/// Attempts a release compare-exchange.
///
/// # Safety
///
/// `r` and `old` must be valid pointers.
#[inline]
unsafe fn try_cmpxchg_release(r: *mut Ref, old: *mut c_int, new: c_int) -> bool {
    // SAFETY: The caller guarantees that `r` and `old` are valid.
    unsafe { bindings::rust_refcount_try_cmpxchg_release(r, old, new) }
}

/// Adds to a refcount with relaxed ordering.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.
#[inline]
unsafe fn fetch_add_relaxed(i: c_int, r: *mut Ref) -> c_int {
    // SAFETY: The caller guarantees that `r` is valid.
    unsafe { bindings::rust_refcount_fetch_add_relaxed(i, r) }
}

/// Subtracts from a refcount with release ordering.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.
#[inline]
unsafe fn fetch_sub_release(i: c_int, r: *mut Ref) -> c_int {
    // SAFETY: The caller guarantees that `r` is valid.
    unsafe { bindings::rust_refcount_fetch_sub_release(i, r) }
}

/// Stores the previous count when the optional output pointer is non-NULL.
///
/// # Safety
///
/// A non-NULL `oldp` must point to writable storage.
#[inline]
unsafe fn store_old(oldp: *mut c_int, old: c_int) {
    if !oldp.is_null() {
        // SAFETY: The caller guarantees that a non-NULL `oldp` is writable.
        unsafe { *oldp = old };
    }
}

/// Sets the refcount to its saturated value and emits the matching warning.
///
/// # Safety
///
/// `r` must point to a valid, writable `refcount_t`.
#[inline]
unsafe fn saturate(r: *mut Ref, kind: Saturation) {
    // SAFETY: The caller guarantees that `r` is valid and writable.
    unsafe { set(r, REFCOUNT_SATURATED) };
    // SAFETY: The integer warning type carries no pointer preconditions.
    unsafe { bindings::rust_refcount_warn(kind as c_int) };
}

/// Adds unless the refcount is zero.
///
/// Returns true when the refcount was non-zero before the operation.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
#[export]
unsafe extern "C" fn rust_refcount_add_not_zero(i: c_int, r: *mut Ref, oldp: *mut c_int) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    let mut old = unsafe { read(r) };

    loop {
        if old == 0 {
            break;
        }
        // SAFETY: The caller guarantees that `r` and `old` are valid.
        if unsafe { try_cmpxchg_relaxed(r, &mut old, old.wrapping_add(i)) } {
            break;
        }
    }

    // SAFETY: The caller guarantees that `oldp` is null or writable.
    unsafe { store_old(oldp, old) };
    if old < 0 || old.wrapping_add(i) < 0 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, ADD_NOT_ZERO_OVF) };
    }

    old != 0
}

/// Adds unless zero while enforcing a maximum count limit and acquire ordering.
///
/// Returns true when the addition succeeded.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
#[export]
unsafe extern "C" fn rust_refcount_add_not_zero_limited_acquire(
    i: c_int,
    r: *mut Ref,
    oldp: *mut c_int,
    limit: c_int,
) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    let mut old = unsafe { read(r) };

    loop {
        if old == 0 {
            break;
        }
        if i > limit.wrapping_sub(old) {
            // SAFETY: The caller guarantees that `oldp` is null or writable.
            unsafe { store_old(oldp, old) };
            return false;
        }
        // SAFETY: The caller guarantees that `r` and `old` are valid.
        if unsafe { try_cmpxchg_acquire(r, &mut old, old.wrapping_add(i)) } {
            break;
        }
    }

    // SAFETY: The caller guarantees that `oldp` is null or writable.
    unsafe { store_old(oldp, old) };
    if old < 0 || old.wrapping_add(i) < 0 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, ADD_NOT_ZERO_OVF) };
    }

    old != 0
}

/// Adds to a refcount with relaxed ordering.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
#[export]
unsafe extern "C" fn rust_refcount_add(i: c_int, r: *mut Ref, oldp: *mut c_int) {
    // SAFETY: The caller guarantees that `r` is valid.
    let old = unsafe { fetch_add_relaxed(i, r) };
    // SAFETY: The caller guarantees that `oldp` is null or writable.
    unsafe { store_old(oldp, old) };

    if old == 0 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, ADD_UAF) };
    } else if old < 0 || old.wrapping_add(i) < 0 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, ADD_OVF) };
    }
}

/// Subtracts and reports whether the result is zero.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
unsafe fn sub_and_test(i: c_int, r: *mut Ref, oldp: *mut c_int) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    let old = unsafe { fetch_sub_release(i, r) };
    // SAFETY: The caller guarantees that `oldp` is null or writable.
    unsafe { store_old(oldp, old) };

    if old > 0 && old == i {
        // SAFETY: This helper only provides a memory barrier.
        unsafe { bindings::rust_refcount_acquire_after_ctrl_dep() };
        return true;
    }

    if old <= 0 || old.wrapping_sub(i) < 0 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, SUB_UAF) };
    }

    false
}

/// Subtracts and reports whether the result is zero.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
#[export]
unsafe extern "C" fn rust_refcount_sub_and_test(i: c_int, r: *mut Ref, oldp: *mut c_int) -> bool {
    // SAFETY: The caller provides the same contract for `r` and `oldp`.
    unsafe { sub_and_test(i, r, oldp) }
}

/// Decrements a refcount without testing for zero.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.  A non-NULL `oldp` must point to
/// writable storage.
#[export]
unsafe extern "C" fn rust_refcount_dec(r: *mut Ref, oldp: *mut c_int) {
    // SAFETY: The caller guarantees that `r` is valid.
    let old = unsafe { fetch_sub_release(1, r) };
    // SAFETY: The caller guarantees that `oldp` is null or writable.
    unsafe { store_old(oldp, old) };

    if old <= 1 {
        // SAFETY: The caller guarantees that `r` is writable.
        unsafe { saturate(r, DEC_LEAK) };
    }
}

/// Sets a refcount to saturated state and emits a saturation warning.
///
/// # Safety
///
/// `r` must point to a valid, writable `refcount_t`.
#[export]
unsafe extern "C" fn rust_refcount_warn_saturate(r: *mut Ref, kind: c_int) {
    // SAFETY: The caller guarantees that `r` is valid and writable.
    unsafe { saturate(r, kind as c_uint) };
}

/// Decrements from one to zero with release ordering.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.
#[export]
unsafe extern "C" fn rust_refcount_dec_if_one(r: *mut Ref) -> bool {
    let mut old = 1;
    // SAFETY: The caller guarantees that `r` and `old` are valid.
    unsafe { try_cmpxchg_release(r, &mut old, 0) }
}

/// Decrements unless the refcount is one or saturated.
///
/// # Safety
///
/// `r` must point to a valid `refcount_t`.
#[export]
unsafe extern "C" fn rust_refcount_dec_not_one(r: *mut Ref) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    let mut old = unsafe { read(r) } as c_uint;

    loop {
        if old == REFCOUNT_SATURATED as c_uint {
            return true;
        }
        if old == 1 {
            return false;
        }

        let new = old.wrapping_sub(1);
        if new > old {
            // SAFETY: This helper only emits the warning site.
            unsafe { bindings::rust_refcount_warn_dec_not_one() };
            return true;
        }

        let mut expected = old as c_int;
        // SAFETY: The caller guarantees that `r` and `expected` are valid.
        if unsafe { try_cmpxchg_release(r, &mut expected, new as c_int) } {
            return true;
        }
        old = expected as c_uint;
    }
}

/// Decrements to zero and returns while holding a mutex.
///
/// # Safety
///
/// `r` and `lock` must be valid.  The caller must have permission to acquire
/// `lock` in the current context.
#[export]
unsafe extern "C" fn rust_refcount_dec_and_mutex_lock(
    r: *mut Ref,
    lock: *mut bindings::mutex,
) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    if unsafe { rust_refcount_dec_not_one(r) } {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid and lockable.
    unsafe { bindings::rust_refcount_mutex_lock(lock) };
    // SAFETY: The caller guarantees that `r` is valid.
    if !unsafe { sub_and_test(1, r, ptr::null_mut()) } {
        // SAFETY: This function acquired `lock` above.
        unsafe { bindings::rust_refcount_mutex_unlock(lock) };
        return false;
    }

    true
}

/// Decrements to zero and returns while holding a spinlock.
///
/// # Safety
///
/// `r` and `lock` must be valid.  The caller must have permission to acquire
/// `lock` in the current context.
#[export]
unsafe extern "C" fn rust_refcount_dec_and_lock(
    r: *mut Ref,
    lock: *mut bindings::spinlock_t,
) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    if unsafe { rust_refcount_dec_not_one(r) } {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid and lockable.
    unsafe { bindings::rust_refcount_spin_lock(lock) };
    // SAFETY: The caller guarantees that `r` is valid.
    if !unsafe { sub_and_test(1, r, ptr::null_mut()) } {
        // SAFETY: This function acquired `lock` above.
        unsafe { bindings::rust_refcount_spin_unlock(lock) };
        return false;
    }

    true
}

/// Decrements to zero and returns while holding an IRQ-safe spinlock.
///
/// # Safety
///
/// `r`, `lock`, and `flags` must be valid.  The caller must have permission to
/// acquire `lock` and disable interrupts in the current context.
#[export]
unsafe extern "C" fn rust_refcount_dec_and_lock_irqsave(
    r: *mut Ref,
    lock: *mut bindings::spinlock_t,
    flags: *mut usize,
) -> bool {
    // SAFETY: The caller guarantees that `r` is valid.
    if unsafe { rust_refcount_dec_not_one(r) } {
        return false;
    }

    let mut saved_flags = 0;
    // SAFETY: The caller guarantees that `lock` and `saved_flags` are valid.
    unsafe { bindings::rust_refcount_spin_lock_irqsave(lock, &mut saved_flags) };
    // SAFETY: The caller guarantees that `r` is valid.
    if !unsafe { sub_and_test(1, r, ptr::null_mut()) } {
        // SAFETY: This function acquired `lock` above.
        unsafe {
            bindings::rust_refcount_spin_unlock_irqrestore(lock, saved_flags);
        }
        return false;
    }

    // SAFETY: The caller guarantees that `flags` is writable.
    unsafe { *flags = saved_flags };
    true
}
