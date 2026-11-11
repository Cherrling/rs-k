// SPDX-License-Identifier: GPL-2.0-only

//! Native mutex owner protocol.
//!
//! Rust owns the task-pointer encoding, fast acquisition and release,
//! contention trylock, and direct handoff state. C helpers retain atomic
//! operations, scheduler proxy state, intrusive wait lists, optimistic
//! spinning, task wakeups, lockdep, debugging, and public ABI adapters.

use crate::{bindings, ffi::c_ulong};
use core::ptr;
use macros::export;

type Mutex = bindings::mutex;
type Task = bindings::task_struct;

const FLAG_WAITERS: c_ulong = bindings::MUTEX_FLAG_WAITERS as c_ulong;
const FLAG_HANDOFF: c_ulong = bindings::MUTEX_FLAG_HANDOFF as c_ulong;
const FLAG_PICKUP: c_ulong = bindings::MUTEX_FLAG_PICKUP as c_ulong;
const FLAGS: c_ulong = bindings::MUTEX_FLAGS as c_ulong;

#[inline]
fn owner_task(owner: c_ulong) -> c_ulong {
    owner & !FLAGS
}

#[inline]
fn owner_flags(owner: c_ulong) -> c_ulong {
    owner & FLAGS
}

/// Reads the encoded owner.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
#[inline]
unsafe fn read_owner(lock: *mut Mutex) -> c_ulong {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_mutex_read_owner(lock) }
}

/// Attempts an acquire-ordered owner update.
///
/// # Safety
///
/// `lock` must point to a valid mutex and `owner` must be writable.
#[inline]
unsafe fn try_cmpxchg_acquire(lock: *mut Mutex, owner: &mut c_ulong, new: c_ulong) -> bool {
    // SAFETY: The caller guarantees validity of `lock` and `owner`.
    unsafe { bindings::rust_mutex_try_cmpxchg_owner_acquire(lock, owner, new) }
}

/// Attempts a release-ordered owner update.
///
/// # Safety
///
/// `lock` must point to a valid mutex and `owner` must be writable.
#[inline]
unsafe fn try_cmpxchg_release(lock: *mut Mutex, owner: &mut c_ulong, new: c_ulong) -> bool {
    // SAFETY: The caller guarantees validity of `lock` and `owner`.
    unsafe { bindings::rust_mutex_try_cmpxchg_owner_release(lock, owner, new) }
}

/// Returns the current task as an encoded-owner address.
///
/// # Safety
///
/// The caller must be in a valid kernel task context.
#[inline]
unsafe fn current() -> c_ulong {
    // SAFETY: The caller guarantees a current task.
    unsafe { bindings::rust_mutex_current() }
}

/// Runs the common contention trylock or handoff protocol.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
unsafe fn trylock_common(lock: *mut Mutex, handoff: bool) -> *mut Task {
    // SAFETY: The caller guarantees a current task.
    let curr = unsafe { current() };
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut owner = unsafe { read_owner(lock) };

    let failed_task = loop {
        let mut flags = owner_flags(owner);
        let mut task = owner_task(owner);

        if task != 0 {
            if flags & FLAG_PICKUP != 0 {
                if task != curr {
                    break task;
                }
                flags &= !FLAG_PICKUP;
            } else if handoff {
                if flags & FLAG_HANDOFF != 0 {
                    break task;
                }
                flags |= FLAG_HANDOFF;
            } else {
                break task;
            }
        } else {
            // SAFETY: The caller guarantees that `lock` is valid.
            unsafe { bindings::rust_mutex_warn_bad_trylock_flags(lock, flags) };
            task = curr;
        }

        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_acquire(lock, &mut owner, task | flags) } {
            if task == curr {
                return ptr::null_mut();
            }
            break task;
        }
    };

    failed_task as *mut Task
}

/// Transfers ownership to `task`, or unlocks when `task` is null.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task.
unsafe fn handoff(lock: *mut Mutex, task: *mut Task) {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut owner = unsafe { read_owner(lock) };

    loop {
        let mut new = owner & FLAG_WAITERS;
        new |= task as c_ulong;
        if !task.is_null() {
            new |= FLAG_PICKUP;
        }

        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_release(lock, &mut owner, new) } {
            break;
        }
    }
}

/// Decides whether a mutex owner is encoded as locked.
///
/// # Safety
///
/// `lock` must point to a valid mutex or be null.
#[export]
unsafe extern "C" fn rust_mutex_is_locked(lock: *mut Mutex) -> bool {
    if lock.is_null() {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    owner_task(unsafe { read_owner(lock) }) != 0
}

/// Returns the owning task address without protocol flags.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
#[export]
unsafe extern "C" fn rust_mutex_get_owner(lock: *mut Mutex) -> c_ulong {
    // SAFETY: The caller guarantees that `lock` is valid.
    owner_task(unsafe { read_owner(lock) })
}

/// Attempts the uncontended acquisition fast path.
///
/// # Safety
///
/// `lock` must point to a valid mutex and the caller must be in a task.
#[export]
unsafe extern "C" fn rust_mutex_trylock_fast(lock: *mut Mutex) -> bool {
    // SAFETY: The caller guarantees a current task.
    let curr = unsafe { current() };
    let mut expected = 0;
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_acquire(lock, &mut expected, curr) }
}

/// Attempts the uncontended release fast path.
///
/// # Safety
///
/// `lock` must point to a valid mutex and be owned by the current task.
#[export]
unsafe extern "C" fn rust_mutex_unlock_fast(lock: *mut Mutex) -> bool {
    // SAFETY: The caller guarantees a current task.
    let mut expected = unsafe { current() };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_release(lock, &mut expected, 0) }
}

/// Runs trylock or requests a direct handoff.
///
/// Returns null on success, otherwise the owner task address.
///
/// # Safety
///
/// `lock` must point to a valid mutex and the caller must be in a task.
#[export]
unsafe extern "C" fn rust_mutex_trylock_common(lock: *mut Mutex, handoff: bool) -> *mut Task {
    // SAFETY: The caller guarantees that `lock` is valid and a task exists.
    unsafe { trylock_common(lock, handoff) }
}

/// Prepares the slow unlock owner state.
///
/// Returns true when the mutex has been fully unlocked and the caller can
/// return without selecting or waking a waiter.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task, and `owner` must be
/// writable.
#[export]
unsafe extern "C" fn rust_mutex_unlock_prepare(lock: *mut Mutex, owner: *mut c_ulong) -> bool {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut observed = unsafe { read_owner(lock) };

    loop {
        // SAFETY: The caller guarantees that `lock` is valid and owned here.
        unsafe { bindings::rust_mutex_warn_bad_unlock(lock, observed) };
        // SAFETY: The scheduler helper reports whether proxy execution must
        // force a handoff.
        if unsafe { bindings::rust_mutex_force_handoff() } {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(FLAG_HANDOFF) };
            return false;
        }
        if observed & FLAG_HANDOFF != 0 {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(observed) };
            return false;
        }

        let new = owner_flags(observed) & FLAG_WAITERS;
        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_release(lock, &mut observed, new) } {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(observed) };
            return observed & FLAG_WAITERS == 0;
        }
    }
}

/// Applies a direct handoff when the prepared owner requested one.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task.
#[export]
unsafe extern "C" fn rust_mutex_handoff_if_requested(
    lock: *mut Mutex,
    owner: c_ulong,
    task: *mut Task,
) -> bool {
    if owner & FLAG_HANDOFF == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid and owned here.
    unsafe { handoff(lock, task) };
    true
}
