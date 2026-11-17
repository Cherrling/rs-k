// SPDX-License-Identifier: GPL-2.0-only

//! Native decrement-and-lock protocol.
//!
//! Rust owns the fast decrement, the transition to the lock-backed slow path,
//! the second decrement, and selection of the caller that releases the object.
//! C helpers retain the architecture atomics and lock primitives.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type Atomic = bindings::atomic_t;
type Spinlock = bindings::spinlock_t;
type RawSpinlock = bindings::raw_spinlock_t;
type Mutex = bindings::mutex;

/// Decrements an atomic unless its value is one.
///
/// Returns true when this helper decremented the value.
///
/// # Safety
///
/// `atomic` must point to valid atomic storage.
#[inline]
unsafe fn decrement_unless_one(atomic: *mut Atomic) -> bool {
    // SAFETY: The caller guarantees that `atomic` is valid.
    unsafe { bindings::rust_dec_and_lock_atomic_add_unless(atomic) != 0 }
}

/// Decrements an atomic and reports whether the new value is zero.
///
/// # Safety
///
/// `atomic` must point to valid atomic storage.
#[inline]
unsafe fn decrement_and_test(atomic: *mut Atomic) -> bool {
    // SAFETY: The caller guarantees that `atomic` is valid.
    unsafe { bindings::rust_dec_and_lock_atomic_dec_and_test(atomic) }
}

/// Decrements a count and returns with a spinlock held when it reaches zero.
///
/// # Safety
///
/// `atomic` and `lock` must point to valid storage. The caller must hold a
/// reference for the decrement.
#[export]
unsafe extern "C" fn rust_dec_and_lock(atomic: *mut Atomic, lock: *mut Spinlock) -> c_int {
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_unless_one(atomic) } {
        return 0;
    }

    // SAFETY: The caller guarantees valid lock storage.
    unsafe { bindings::rust_dec_and_lock_spin_lock(lock) };
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_and_test(atomic) } {
        return 1;
    }

    // SAFETY: The slow path acquired `lock`.
    unsafe { bindings::rust_dec_and_lock_spin_unlock(lock) };
    0
}

/// Decrements a count and returns with a spinlock held and IRQs disabled when
/// the count reaches zero.
///
/// # Safety
///
/// `atomic`, `lock`, and `flags` must point to valid storage. The caller must
/// hold a reference for the decrement.
#[export]
unsafe extern "C" fn rust_dec_and_lock_irqsave(
    atomic: *mut Atomic,
    lock: *mut Spinlock,
    flags: *mut usize,
) -> c_int {
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_unless_one(atomic) } {
        return 0;
    }

    // SAFETY: The caller guarantees valid lock and flags storage.
    unsafe { bindings::rust_dec_and_lock_spin_lock_irqsave(lock, flags) };
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_and_test(atomic) } {
        return 1;
    }

    // SAFETY: The slow path acquired `lock` and initialized `flags`.
    unsafe { bindings::rust_dec_and_lock_spin_unlock_irqrestore(lock, *flags) };
    0
}

/// Decrements a count and returns with a raw spinlock held when it reaches zero.
///
/// # Safety
///
/// `atomic` and `lock` must point to valid storage. The caller must hold a
/// reference for the decrement.
#[export]
unsafe extern "C" fn rust_dec_and_raw_lock(atomic: *mut Atomic, lock: *mut RawSpinlock) -> c_int {
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_unless_one(atomic) } {
        return 0;
    }

    // SAFETY: The caller guarantees valid lock storage.
    unsafe { bindings::rust_dec_and_lock_raw_lock(lock) };
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_and_test(atomic) } {
        return 1;
    }

    // SAFETY: The slow path acquired `lock`.
    unsafe { bindings::rust_dec_and_lock_raw_unlock(lock) };
    0
}

/// Decrements a count and returns with a raw spinlock held and IRQs disabled
/// when the count reaches zero.
///
/// # Safety
///
/// `atomic`, `lock`, and `flags` must point to valid storage. The caller must
/// hold a reference for the decrement.
#[export]
unsafe extern "C" fn rust_dec_and_raw_lock_irqsave(
    atomic: *mut Atomic,
    lock: *mut RawSpinlock,
    flags: *mut usize,
) -> c_int {
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_unless_one(atomic) } {
        return 0;
    }

    // SAFETY: The caller guarantees valid lock and flags storage.
    unsafe { bindings::rust_dec_and_lock_raw_lock_irqsave(lock, flags) };
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_and_test(atomic) } {
        return 1;
    }

    // SAFETY: The slow path acquired `lock` and initialized `flags`.
    unsafe { bindings::rust_dec_and_lock_raw_unlock_irqrestore(lock, *flags) };
    0
}

/// Decrements a count and returns with a mutex held when it reaches zero.
///
/// # Safety
///
/// `atomic` and `lock` must point to valid storage. The caller must hold a
/// reference for the decrement.
#[export]
unsafe extern "C" fn rust_dec_and_mutex_lock(atomic: *mut Atomic, lock: *mut Mutex) -> c_int {
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_unless_one(atomic) } {
        return 0;
    }

    // SAFETY: The caller guarantees valid lock storage.
    unsafe { bindings::rust_dec_and_lock_mutex_lock(lock) };
    // SAFETY: The caller guarantees valid atomic storage.
    if unsafe { decrement_and_test(atomic) } {
        return 1;
    }

    // SAFETY: The slow path acquired `lock`.
    unsafe { bindings::rust_dec_and_lock_mutex_unlock(lock) };
    0
}
