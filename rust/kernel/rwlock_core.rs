// SPDX-License-Identifier: GPL-2.0-or-later

//! Native queued read-write spinlock count state machine.
//!
//! Rust owns reader and writer fast acquisition, non-blocking acquisition,
//! slow-path count transitions, writer waiting and handoff, and unlock state
//! transitions. C helpers retain architecture atomics, the architecture
//! spinlock protecting the wait queue, interrupt-context detection, tracing,
//! and module-visible ABI adapters.

use crate::{bindings, ffi::c_int};
type Rwlock = bindings::qrwlock;

const WAITING: c_int = bindings::QRW_WAITING as c_int;
const LOCKED: c_int = bindings::QRW_LOCKED as c_int;
const WMASK: c_int = bindings::QRW_WMASK as c_int;
const BIAS: c_int = bindings::QRW_BIAS as c_int;

/// Reads the lock count atomically.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn read_count(lock: *mut Rwlock) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_read_count(lock) }
}

/// Atomically adds to the count with acquire ordering.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn add_return_acquire(lock: *mut Rwlock, value: c_int) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_add_return_acquire(lock, value) }
}

/// Atomically subtracts from the count with release ordering.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn sub_return_release(lock: *mut Rwlock, value: c_int) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_sub_return_release(lock, value) }
}

/// Attempts an acquire-ordered count compare-exchange.
///
/// # Safety
///
/// `lock` must be valid and `count` must point to writable storage.
#[inline]
unsafe fn try_cmpxchg_acquire(lock: *mut Rwlock, count: &mut c_int, new: c_int) -> bool {
    // SAFETY: The caller guarantees validity of `lock` and `count`.
    unsafe { bindings::rust_qrwlock_try_cmpxchg_acquire(lock, count, new) }
}

/// Atomically adds to the count.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn add(lock: *mut Rwlock, value: c_int) {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_add(lock, value) };
}

/// Atomically subtracts from the count.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn sub(lock: *mut Rwlock, value: c_int) {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_sub(lock, value) };
}

/// Sets the writer-waiting flag.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn set_waiting(lock: *mut Rwlock) {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_or(lock, WAITING) };
}

/// Waits until a writer no longer owns the count.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn wait_writer_release_acquire(lock: *mut Rwlock) {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_writer_release_acquire(lock) };
}

/// Waits until only the writer-waiting flag remains.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[inline]
unsafe fn wait_waiting_relaxed(lock: *mut Rwlock) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_waiting_relaxed(lock) }
}

/// Attempts the reader fast path.
///
/// Returns false when the caller must run `queued_read_lock_slowpath()`.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_read_fast(lock: *mut Rwlock) -> bool {
    // SAFETY: The caller guarantees that `lock` is valid.
    let count = unsafe { add_return_acquire(lock, BIAS) };
    count & WMASK == 0
}

/// Attempts a non-blocking reader acquisition.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_read_trylock_fast(lock: *mut Rwlock) -> bool {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut count = unsafe { read_count(lock) };
    if count & WMASK != 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    count = unsafe { add_return_acquire(lock, BIAS) };
    if count & WMASK == 0 {
        return true;
    }

    // Undo the provisional reader acquisition.
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { sub(lock, BIAS) };
    false
}

/// Attempts the writer fast path.
///
/// Returns false when the caller must run `queued_write_lock_slowpath()`.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_write_fast(lock: *mut Rwlock) -> bool {
    let mut expected = 0;
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_acquire(lock, &mut expected, LOCKED) }
}

/// Attempts a non-blocking writer acquisition.
///
/// # Safety
///
/// `lock` must point to a valid queued read-write lock.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_write_trylock_fast(lock: *mut Rwlock) -> bool {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut count = unsafe { read_count(lock) };
    if count != 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_acquire(lock, &mut count, LOCKED) }
}

/// Releases one reader.
///
/// # Safety
///
/// `lock` must be valid and reader-held.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_read_unlock_fast(lock: *mut Rwlock) {
    // SAFETY: The caller guarantees that `lock` is reader-held.
    unsafe { sub_return_release(lock, BIAS) };
}

/// Releases the writer.
///
/// # Safety
///
/// `lock` must be valid and writer-held.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_write_unlock_fast(lock: *mut Rwlock) {
    // SAFETY: The caller guarantees that `lock` is writer-held.
    unsafe { bindings::rust_qrwlock_store_release_unlock(lock) };
}

/// Runs the contended reader slow path.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit spinning.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_read_lock_slowpath(lock: *mut Rwlock) {
    // SAFETY: This helper only checks whether the caller is in interrupt context.
    if unsafe { bindings::rust_qrwlock_in_interrupt() } {
        // SAFETY: The caller guarantees that `lock` is valid.
        unsafe { wait_writer_release_acquire(lock) };
        return;
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { sub(lock, BIAS) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_trace_read_begin(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_lock(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { add(lock, BIAS) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { wait_writer_release_acquire(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_unlock(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_trace_end(lock) };
}

/// Runs the contended writer slow path.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit spinning.
#[no_mangle]
unsafe extern "C" fn rust_qrwlock_write_lock_slowpath(lock: *mut Rwlock) {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_trace_write_begin(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_lock(lock) };

    // SAFETY: The caller guarantees that `lock` is valid.
    let mut count = unsafe { read_count(lock) };
    let acquired = count == 0
        // SAFETY: The caller guarantees that `lock` is valid.
        && unsafe { try_cmpxchg_acquire(lock, &mut count, LOCKED) };
    if !acquired {
        // SAFETY: The caller guarantees that `lock` is valid.
        unsafe { set_waiting(lock) };

        loop {
            // SAFETY: The caller guarantees that `lock` is valid.
            count = unsafe { wait_waiting_relaxed(lock) };
            // SAFETY: The caller guarantees that `lock` is valid.
            if unsafe { try_cmpxchg_acquire(lock, &mut count, LOCKED) } {
                break;
            }
        }
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_wait_unlock(lock) };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_qrwlock_trace_end(lock) };
}
