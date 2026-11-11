// SPDX-License-Identifier: GPL-2.0-only

//! Native read-write semaphore count and owner protocol.
//!
//! Rust owns fast acquisition, optimistic reader stealing, contended writer
//! count transitions, owner encoding, release transitions, and the decision
//! to wake waiters. C helpers retain architecture atomics, intrusive waiter
//! lists, scheduling, wake queues, optimistic spinning, debugging, tracing,
//! lockdep, and public ABI adapters.

use crate::{
    bindings,
    ffi::{c_long, c_uint, c_ulong},
};
use macros::export;

type Rwsem = bindings::rw_semaphore;
type Task = bindings::task_struct;

const READER_OWNED: c_ulong = bindings::RWSEM_READER_OWNED as c_ulong;
const NONSPINNABLE: c_ulong = bindings::RWSEM_NONSPINNABLE as c_ulong;
const OWNER_FLAGS_MASK: c_ulong = bindings::RWSEM_OWNER_FLAGS_MASK as c_ulong;

const WRITER_LOCKED: c_long = bindings::RWSEM_WRITER_LOCKED as c_long;
const WAITERS: c_long = bindings::RWSEM_FLAG_WAITERS as c_long;
const HANDOFF: c_long = bindings::RWSEM_FLAG_HANDOFF as c_long;
const READER_SHIFT: c_long = bindings::RWSEM_READER_SHIFT as c_long;
const READER_BIAS: c_long = bindings::RWSEM_READER_BIAS as c_long;
const READER_MASK: c_long = !(READER_BIAS - 1);
const LOCK_MASK: c_long = WRITER_LOCKED | READER_MASK;
const READ_FAILED_MASK: c_long = WRITER_LOCKED | WAITERS | HANDOFF | c_long::MIN;

const STOLE_LOCK: c_uint = bindings::RWSEM_RUST_STOLE_LOCK as c_uint;
const STEAL_WAKE_READERS: c_uint = bindings::RWSEM_RUST_STEAL_WAKE_READERS as c_uint;

const WRITE_FAILED: c_uint = bindings::RWSEM_RUST_WRITE_FAILED;
const WRITE_ACQUIRED: c_uint = bindings::RWSEM_RUST_WRITE_ACQUIRED;
const WRITE_HANDOFF: c_uint = bindings::RWSEM_RUST_WRITE_HANDOFF;

const WAKE_NONE: c_uint = bindings::RWSEM_RUST_WAKE_NONE;
const WAKE_ANY: c_uint = bindings::RWSEM_RUST_WAKE_ANY;
const WAKE_CLASS_READERS: c_uint = bindings::RWSEM_RUST_WAKE_CLASS_READERS;

/// Reads the count atomically.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn read_count(sem: *mut Rwsem) -> c_long {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_read_count(sem) }
}

/// Adds to the count with acquire ordering and returns the new value.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn add_return_acquire(sem: *mut Rwsem, value: c_long) -> c_long {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_add_return_acquire(sem, value) }
}

/// Adds to the count with release ordering and returns the new value.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn add_return_release(sem: *mut Rwsem, value: c_long) -> c_long {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_add_return_release(sem, value) }
}

/// Adds to the count with release ordering and returns the old value.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn fetch_add_release(sem: *mut Rwsem, value: c_long) -> c_long {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_fetch_add_release(sem, value) }
}

/// Attempts an acquire-ordered count compare-exchange.
///
/// # Safety
///
/// `sem` must be valid and `count` must point to writable storage.
#[inline]
unsafe fn try_cmpxchg_count_acquire(sem: *mut Rwsem, count: &mut c_long, new: c_long) -> bool {
    // SAFETY: The caller guarantees validity of `sem` and `count`.
    unsafe { bindings::rust_rwsem_try_cmpxchg_count_acquire(sem, count, new) }
}

/// Reads the owner atomically.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn read_owner(sem: *mut Rwsem) -> c_ulong {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_read_owner(sem) }
}

/// Writes the owner atomically.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[inline]
unsafe fn set_owner(sem: *mut Rwsem, owner: c_ulong) {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_set_owner_value(sem, owner) };
}

/// Returns the current task as an encoded-owner address.
///
/// # Safety
///
/// The caller must be in a valid kernel task context.
#[inline]
unsafe fn current() -> c_ulong {
    // SAFETY: The caller guarantees a current task.
    unsafe { bindings::rust_rwsem_current() }
}

/// Records the current task as the writer owner.
///
/// # Safety
///
/// `sem` must be valid and the caller must be in a task.
#[inline]
unsafe fn set_writer_owner(sem: *mut Rwsem) {
    // SAFETY: The caller guarantees a valid semaphore and current task.
    unsafe { set_owner(sem, current()) };
}

/// Records the current task as a reader owner and preserves nonspinnable.
///
/// # Safety
///
/// `sem` must be valid and the caller must be in a task.
#[inline]
unsafe fn set_reader_owner(sem: *mut Rwsem) {
    // SAFETY: The caller guarantees that `sem` is valid.
    let old_owner = unsafe { read_owner(sem) };
    // SAFETY: The caller guarantees a current task.
    let owner = unsafe { current() } | READER_OWNED | (old_owner & NONSPINNABLE);
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { set_owner(sem, owner) };
}

/// Attempts the reader add-return fast path.
///
/// # Safety
///
/// `sem` must be valid, `count` must be writable, and the caller must have
/// disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_read_fast(sem: *mut Rwsem, count: *mut c_long) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    let after = unsafe { add_return_acquire(sem, READER_BIAS) };
    // SAFETY: The caller guarantees that `count` is writable.
    unsafe { count.write(after) };

    // SAFETY: The caller guarantees that `sem` is valid.
    if unsafe { bindings::rust_rwsem_warn_negative_count(sem, after) } {
        // SAFETY: The caller guarantees that `sem` is valid.
        unsafe { bindings::rust_rwsem_set_nonspinnable(sem) };
    }

    if after & READ_FAILED_MASK != 0 {
        return false;
    }

    // SAFETY: The caller guarantees a current task and a valid semaphore.
    unsafe { set_reader_owner(sem) };
    true
}

/// Attempts a non-blocking reader count acquisition.
///
/// # Safety
///
/// `sem` must be valid, and the caller must have disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_read_trylock(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    let mut count = unsafe { read_count(sem) };

    while count & READ_FAILED_MASK == 0 {
        let new = count.wrapping_add(READER_BIAS);
        // SAFETY: The caller guarantees that `sem` and the local count are valid.
        if unsafe { try_cmpxchg_count_acquire(sem, &mut count, new) } {
            // SAFETY: The caller guarantees a current task and a valid semaphore.
            unsafe { set_reader_owner(sem) };
            return true;
        }
    }

    false
}

/// Decides whether a reader slow path can optimistically steal the lock.
///
/// The return value contains `RWSEM_RUST_STOLE_LOCK` when acquisition
/// succeeds. `RWSEM_RUST_STEAL_WAKE_READERS` then requests a reader wakeup.
///
/// # Safety
///
/// `sem` must be valid, and the caller must have disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_reader_slow_steal(sem: *mut Rwsem, count: c_long) -> c_uint {
    // SAFETY: The caller guarantees that `sem` is valid.
    let owner = unsafe { read_owner(sem) };
    let readers = count >> READER_SHIFT;

    if owner & READER_OWNED != 0 && readers > 1 && count & WRITER_LOCKED == 0 {
        return 0;
    }
    if count & (WRITER_LOCKED | HANDOFF) != 0 {
        return 0;
    }

    // SAFETY: The caller guarantees a current task and a valid semaphore.
    unsafe { set_reader_owner(sem) };

    let mut result = STOLE_LOCK;
    if readers == 1 && count & WAITERS != 0 {
        result |= STEAL_WAKE_READERS;
    }
    result
}

/// Attempts writer acquisition of an unlocked count.
///
/// # Safety
///
/// `sem` must be valid, and the caller must have disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_write_fast(sem: *mut Rwsem) -> bool {
    let mut expected = 0;
    // SAFETY: The caller guarantees that `sem` is valid.
    if !unsafe { try_cmpxchg_count_acquire(sem, &mut expected, WRITER_LOCKED) } {
        return false;
    }

    // SAFETY: The caller guarantees a current task and a valid semaphore.
    unsafe { set_writer_owner(sem) };
    true
}

/// Attempts writer acquisition before a waiter is queued.
///
/// # Safety
///
/// `sem` must be valid, and preemption must remain disabled.
#[export]
unsafe extern "C" fn rust_rwsem_write_unqueued(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    let mut count = unsafe { read_count(sem) };

    while count & (LOCK_MASK | HANDOFF) == 0 {
        let new = count | WRITER_LOCKED;
        // SAFETY: The caller guarantees that `sem` and the local count are valid.
        if unsafe { try_cmpxchg_count_acquire(sem, &mut count, new) } {
            // SAFETY: The caller guarantees a current task and valid semaphore.
            unsafe { set_writer_owner(sem) };
            return true;
        }
    }

    false
}

/// Runs a contended writer count transition while the wait lock is held.
///
/// `allow_handoff` includes the scheduling and timeout policy evaluated by C.
/// `waiters_empty` describes the list after this waiter is removed.
///
/// # Safety
///
/// `sem` must be valid, its wait lock must be held, and the caller must have
/// disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_write_slow(
    sem: *mut Rwsem,
    allow_handoff: bool,
    waiter_is_first: bool,
    first_handoff_set: bool,
    waiters_empty: bool,
) -> c_uint {
    // SAFETY: The caller guarantees that `sem` is valid.
    let mut count = unsafe { read_count(sem) };

    loop {
        let has_handoff = count & HANDOFF != 0;

        if has_handoff && first_handoff_set && !waiter_is_first {
            return WRITE_FAILED;
        }

        let mut new = count;
        if count & LOCK_MASK != 0 {
            if has_handoff || !allow_handoff {
                return WRITE_FAILED;
            }
            new |= HANDOFF;
        } else {
            new |= WRITER_LOCKED;
            new &= !HANDOFF;
            if waiters_empty {
                new &= !WAITERS;
            }
        }

        // SAFETY: The caller guarantees that `sem` and the local count are valid.
        if unsafe { try_cmpxchg_count_acquire(sem, &mut count, new) } {
            if new & HANDOFF != 0 {
                return WRITE_HANDOFF;
            }

            return WRITE_ACQUIRED;
        }
    }
}

/// Records the current task as writer owner after waiter removal.
///
/// # Safety
///
/// `sem` must be valid, its wait lock must be held, and the caller must have
/// disabled preemption.
#[export]
unsafe extern "C" fn rust_rwsem_set_writer_owner(sem: *mut Rwsem) {
    // SAFETY: The caller guarantees a current task and valid semaphore.
    unsafe { set_writer_owner(sem) };
}

/// Classifies waiters that can be woken after a count transition.
///
/// # Safety
///
/// `sem` must be valid and its wait lock must be held.
#[export]
unsafe extern "C" fn rust_rwsem_cond_wake(sem: *mut Rwsem, count: c_long) -> c_uint {
    if count & WRITER_LOCKED != 0 {
        WAKE_NONE
    } else if count & READER_MASK != 0 {
        WAKE_CLASS_READERS
    } else {
        // SAFETY: The caller guarantees that `sem` is valid.
        unsafe { bindings::rust_rwsem_clear_nonspinnable(sem) };
        WAKE_ANY
    }
}

/// Decodes the task pointer from the owner field.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[export]
unsafe extern "C" fn rust_rwsem_owner(sem: *mut Rwsem) -> *mut Task {
    // SAFETY: The caller guarantees that `sem` is valid.
    let owner = unsafe { read_owner(sem) };
    (owner & !OWNER_FLAGS_MASK) as *mut Task
}

/// Tests whether the lock is owned by readers.
///
/// # Safety
///
/// `sem` must point to a valid read-write semaphore.
#[export]
unsafe extern "C" fn rust_rwsem_is_reader_owned(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    if unsafe { read_count(sem) } & WRITER_LOCKED != 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `sem` is valid.
    let owner = unsafe { read_owner(sem) };
    owner & READER_OWNED != 0
}

/// Releases one reader and decides whether waiters must be woken.
///
/// # Safety
///
/// `sem` must be valid and reader-owned, and preemption must be disabled.
#[export]
unsafe extern "C" fn rust_rwsem_up_read(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is reader-owned.
    unsafe { bindings::rust_rwsem_clear_reader_owned(sem) };
    // SAFETY: The caller guarantees that `sem` is valid.
    let count = unsafe { add_return_release(sem, -READER_BIAS) };

    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_warn_release_count(sem, count) };
    if count & WAITERS != 0 {
        // SAFETY: The caller guarantees that `sem` is valid.
        unsafe { bindings::rust_rwsem_trace_release(sem) };
    }
    if count & (LOCK_MASK | WAITERS) != WAITERS {
        return false;
    }

    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_clear_nonspinnable(sem) };
    true
}

/// Releases a writer and decides whether waiters must be woken.
///
/// # Safety
///
/// `sem` must be valid and writer-owned by the current task, and preemption
/// must be disabled.
#[export]
unsafe extern "C" fn rust_rwsem_up_write(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { set_owner(sem, 0) };
    // SAFETY: The caller guarantees that `sem` is valid.
    let count = unsafe { fetch_add_release(sem, -WRITER_LOCKED) };
    if count & WAITERS == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_trace_release(sem) };
    true
}

/// Downgrades a writer lock to a reader lock.
///
/// # Safety
///
/// `sem` must be valid and writer-owned by the current task, and preemption
/// must be disabled.
#[export]
unsafe extern "C" fn rust_rwsem_downgrade_write(sem: *mut Rwsem) -> bool {
    // SAFETY: The caller guarantees that `sem` is valid.
    let count = unsafe { fetch_add_release(sem, -WRITER_LOCKED + READER_BIAS) };
    // SAFETY: The caller guarantees a current task and a valid semaphore.
    unsafe { set_reader_owner(sem) };
    if count & WAITERS == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_rwsem_trace_release(sem) };
    true
}
