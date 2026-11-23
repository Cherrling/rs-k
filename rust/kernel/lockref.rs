// SPDX-License-Identifier: GPL-2.0-only

//! Native `lockref` state machine.
//!
//! Rust owns the bounded compare-exchange loop, count transitions, dead-state
//! checks, and conditional-locking decisions. C helpers retain the packed
//! spinlock representation, architecture atomics, and spinlock primitives.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type LockRef = bindings::lockref;

const FAST_PATH_RETRIES: usize = 100;
const DEAD_COUNT: c_int = -128;

enum Decision<T> {
    Apply { count: c_int, value: T },
    Return(T),
    Fallback,
}

enum Outcome<T> {
    Applied(T),
    Returned(T),
    Fallback,
}

/// Runs the bounded lockless update loop while the packed spinlock is free.
fn fast_path<T>(lockref: *mut LockRef, decide: impl Fn(c_int) -> Decision<T>) -> Outcome<T> {
    // SAFETY: The caller guarantees that `lockref` is valid.
    let mut old = unsafe { bindings::rust_lockref_read_raw(lockref) };

    for _ in 0..FAST_PATH_RETRIES {
        // SAFETY: `old` contains only bits read from this lockref.
        if !unsafe { bindings::rust_lockref_raw_unlocked(old) } {
            break;
        }

        // SAFETY: `old` contains only bits read from this lockref.
        let count = unsafe { bindings::rust_lockref_raw_count(old) };
        match decide(count) {
            Decision::Apply {
                count: new_count,
                value,
            } => {
                // SAFETY: The caller guarantees that `lockref` is valid and
                // `old` is writable.
                if unsafe { bindings::rust_lockref_try_update(lockref, &mut old, new_count) } {
                    return Outcome::Applied(value);
                }
            }
            Decision::Return(value) => return Outcome::Returned(value),
            Decision::Fallback => break,
        }
    }

    Outcome::Fallback
}

/// Increments a live reference unconditionally.
///
/// # Safety
///
/// `lockref` must point to a valid initialized lockref.
#[export]
unsafe extern "C" fn lockref_get(lockref: *mut LockRef) {
    let outcome = fast_path(lockref, |count| Decision::Apply {
        count: count.wrapping_add(1),
        value: (),
    });

    if matches!(outcome, Outcome::Fallback) {
        // SAFETY: The caller guarantees that `lockref` is valid and lockable.
        unsafe { bindings::rust_lockref_lock(lockref) };
        // SAFETY: This function holds the spinlock.
        let count = unsafe { bindings::rust_lockref_locked_read(lockref) };
        // SAFETY: This function holds the spinlock.
        unsafe { bindings::rust_lockref_locked_write(lockref, count.wrapping_add(1)) };
        // SAFETY: This function acquired the spinlock.
        unsafe { bindings::rust_lockref_unlock(lockref) };
    }
}

/// Increments a reference unless its count is zero or negative.
///
/// # Safety
///
/// `lockref` must point to a valid initialized lockref.
#[export]
unsafe extern "C" fn lockref_get_not_zero(lockref: *mut LockRef) -> bool {
    let outcome = fast_path(lockref, |count| {
        if count <= 0 {
            Decision::Return(false)
        } else {
            Decision::Apply {
                count: count.wrapping_add(1),
                value: true,
            }
        }
    });

    match outcome {
        Outcome::Applied(value) | Outcome::Returned(value) => value,
        Outcome::Fallback => {
            // SAFETY: The caller guarantees that `lockref` is valid and lockable.
            unsafe { bindings::rust_lockref_lock(lockref) };
            // SAFETY: This function holds the spinlock.
            let count = unsafe { bindings::rust_lockref_locked_read(lockref) };
            if count > 0 {
                // SAFETY: This function holds the spinlock.
                unsafe { bindings::rust_lockref_locked_write(lockref, count.wrapping_add(1)) };
            }
            // SAFETY: This function acquired the spinlock.
            unsafe { bindings::rust_lockref_unlock(lockref) };
            count > 0
        }
    }
}

/// Decrements a positive reference and returns the new count.
///
/// Returns `-1` if the lockref is locked, non-positive, or remains contended
/// after the bounded lockless attempt.
///
/// # Safety
///
/// `lockref` must point to a valid initialized lockref.
#[export]
unsafe extern "C" fn lockref_put_return(lockref: *mut LockRef) -> c_int {
    let outcome = fast_path(lockref, |count| {
        if count <= 0 {
            Decision::Return(-1)
        } else {
            let new_count = count.wrapping_sub(1);
            Decision::Apply {
                count: new_count,
                value: new_count,
            }
        }
    });

    match outcome {
        Outcome::Applied(value) | Outcome::Returned(value) => value,
        Outcome::Fallback => -1,
    }
}

/// Decrements a positive reference above one, or returns with the spinlock
/// held when the caller must perform the final release.
///
/// # Safety
///
/// `lockref`, and the spinlock it contains, must be valid and lockable.
#[export]
unsafe extern "C" fn lockref_put_or_lock(lockref: *mut LockRef) -> bool {
    let outcome = fast_path(lockref, |count| {
        if count <= 1 {
            Decision::Fallback
        } else {
            Decision::Apply {
                count: count.wrapping_sub(1),
                value: true,
            }
        }
    });

    match outcome {
        Outcome::Applied(value) | Outcome::Returned(value) => value,
        Outcome::Fallback => {
            // SAFETY: The caller guarantees that `lockref` is valid and lockable.
            unsafe { bindings::rust_lockref_lock(lockref) };
            // SAFETY: This function holds the spinlock.
            let count = unsafe { bindings::rust_lockref_locked_read(lockref) };
            if count <= 1 {
                return false;
            }

            // SAFETY: This function holds the spinlock.
            unsafe { bindings::rust_lockref_locked_write(lockref, count.wrapping_sub(1)) };
            // SAFETY: This function acquired the spinlock.
            unsafe { bindings::rust_lockref_unlock(lockref) };
            true
        }
    }
}

/// Marks a lockref dead while the caller holds its spinlock.
///
/// # Safety
///
/// `lockref` must be valid and its spinlock must be held by the caller.
#[export]
unsafe extern "C" fn lockref_mark_dead(lockref: *mut LockRef) {
    // SAFETY: The caller guarantees the lock contract.
    unsafe { bindings::rust_lockref_mark_dead_under_lock(lockref, DEAD_COUNT) };
}

/// Increments a reference unless it has entered the dead state.
///
/// # Safety
///
/// `lockref` must point to a valid initialized lockref.
#[export]
unsafe extern "C" fn lockref_get_not_dead(lockref: *mut LockRef) -> bool {
    let outcome = fast_path(lockref, |count| {
        if count < 0 {
            Decision::Return(false)
        } else {
            Decision::Apply {
                count: count.wrapping_add(1),
                value: true,
            }
        }
    });

    match outcome {
        Outcome::Applied(value) | Outcome::Returned(value) => value,
        Outcome::Fallback => {
            // SAFETY: The caller guarantees that `lockref` is valid and lockable.
            unsafe { bindings::rust_lockref_lock(lockref) };
            // SAFETY: This function holds the spinlock.
            let count = unsafe { bindings::rust_lockref_locked_read(lockref) };
            if count >= 0 {
                // SAFETY: This function holds the spinlock.
                unsafe { bindings::rust_lockref_locked_write(lockref, count.wrapping_add(1)) };
            }
            // SAFETY: This function acquired the spinlock.
            unsafe { bindings::rust_lockref_unlock(lockref) };
            count >= 0
        }
    }
}
