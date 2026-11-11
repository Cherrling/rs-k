// SPDX-License-Identifier: GPL-2.0-only

//! Native counting semaphore state machine.
//!
//! Rust owns count transitions, fast and contended paths, FIFO waiter
//! membership, direct release handoff, timeout and signal results. C helpers
//! retain raw locking, task state, scheduling, intrusive list primitives,
//! wake queues, hung-task accounting, and tracepoint delivery.

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint, c_ulong},
};
use core::{mem::MaybeUninit, ptr};
use macros::export;

type Semaphore = bindings::semaphore;
type Waiter = bindings::semaphore_waiter;
type WakeQ = bindings::wake_q_head;
type ListHead = bindings::list_head;

const MAX_SCHEDULE_TIMEOUT: c_long = c_long::MAX;

/// Acquires the semaphore lock and disables interrupts.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[inline]
unsafe fn lock_irqsave(sem: *mut Semaphore) -> c_ulong {
    let mut flags = 0;
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_semaphore_lock_irqsave(sem, &mut flags) };
    flags
}

/// Releases the semaphore lock and restores interrupts.
///
/// # Safety
///
/// `sem` must be valid and its lock must be held.
#[inline]
unsafe fn unlock_irqrestore(sem: *mut Semaphore, flags: c_ulong) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_semaphore_unlock_irqrestore(sem, flags) };
}

/// Acquires the semaphore lock with interrupts disabled.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[inline]
unsafe fn lock_irq(sem: *mut Semaphore) {
    // SAFETY: The caller guarantees that `sem` is valid.
    unsafe { bindings::rust_semaphore_lock_irq(sem) };
}

/// Releases the semaphore lock and enables interrupts.
///
/// # Safety
///
/// `sem` must be valid and its lock must be held.
#[inline]
unsafe fn unlock_irq(sem: *mut Semaphore) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_semaphore_unlock_irq(sem) };
}

/// Returns the address of a waiter's list node.
///
/// # Safety
///
/// `waiter` must point to a valid semaphore waiter.
#[inline]
unsafe fn waiter_list(waiter: *mut Waiter) -> *mut ListHead {
    // SAFETY: The caller guarantees that `waiter` is valid.
    unsafe { ptr::addr_of_mut!((*waiter).list) }
}

/// Reports whether a list is empty.
///
/// # Safety
///
/// `list` must point to an initialized list head.
#[inline]
unsafe fn list_empty(list: *mut ListHead) -> bool {
    // SAFETY: The caller guarantees that `list` is valid.
    unsafe { bindings::rust_semaphore_list_empty(list) }
}

/// Acquires one available count and records the current holder.
///
/// # Safety
///
/// `sem` must be valid, its lock must be held, and its count must be positive.
#[inline]
unsafe fn acquire(sem: *mut Semaphore) {
    // SAFETY: The caller guarantees that the count is positive and locked.
    unsafe { (*sem).count -= 1 };
    // SAFETY: The caller guarantees that `sem` is valid and locked.
    unsafe { bindings::rust_semaphore_set_holder(sem) };
}

/// Inserts the current task at the tail of the semaphore wait list.
///
/// # Safety
///
/// `sem` and `waiter` must be valid, and the semaphore lock must be held.
#[inline]
unsafe fn insert_waiter(sem: *mut Semaphore, waiter: *mut Waiter) {
    // SAFETY: The caller guarantees that `waiter` is writable.
    unsafe {
        (*waiter).task = bindings::rust_semaphore_current_task();
        (*waiter).up = false;
    }

    // SAFETY: The caller guarantees that `sem` is valid and locked.
    let first = unsafe { (*sem).first_waiter };
    if first.is_null() {
        // SAFETY: The caller guarantees that the waiter list node is writable.
        unsafe { bindings::rust_semaphore_list_init(waiter_list(waiter)) };
        // SAFETY: The semaphore lock is held.
        unsafe { (*sem).first_waiter = waiter };
    } else {
        // SAFETY: The first waiter is queued and the lock is held.
        unsafe { bindings::rust_semaphore_list_add_tail(waiter_list(waiter), waiter_list(first)) };
    }
}

/// Removes a waiter from the semaphore wait list.
///
/// # Safety
///
/// `sem` and `waiter` must be valid, the waiter must be queued, and the
/// semaphore lock must be held.
#[inline]
unsafe fn remove_waiter(sem: *mut Semaphore, waiter: *mut Waiter) {
    // SAFETY: The caller guarantees that the waiter is valid.
    let list = unsafe { waiter_list(waiter) };
    // SAFETY: The caller guarantees that the waiter is initialized.
    if unsafe { list_empty(list) } {
        // SAFETY: The semaphore lock is held.
        unsafe { (*sem).first_waiter = ptr::null_mut() };
        return;
    }

    // SAFETY: The semaphore lock is held and the list is non-empty.
    if unsafe { (*sem).first_waiter == waiter } {
        // SAFETY: A non-empty multi-waiter list has a valid next node.
        let next = unsafe { (*list).next };
        let offset = core::mem::offset_of!(Waiter, list);
        // SAFETY: The next node is embedded in the following waiter.
        unsafe { (*sem).first_waiter = next.cast::<u8>().sub(offset).cast() };
    }

    // SAFETY: The caller guarantees that the waiter is queued.
    unsafe { bindings::rust_semaphore_list_del(list) };
}

/// Waits on a contended semaphore while the semaphore lock is held.
///
/// # Safety
///
/// `sem` must be valid, its lock must be held, and the caller must permit
/// sleeping. The lock remains held when this function returns.
unsafe fn wait_contended(sem: *mut Semaphore, state: c_uint, mut timeout: c_long) -> c_int {
    let mut waiter = MaybeUninit::<Waiter>::uninit();
    let waiter = waiter.as_mut_ptr();

    // SAFETY: The semaphore lock is held and `waiter` is writable.
    unsafe { insert_waiter(sem, waiter) };

    loop {
        // SAFETY: The caller guarantees that querying current is valid.
        if unsafe { bindings::rust_semaphore_signal_pending_state(state) } != 0 {
            // SAFETY: The waiter is queued and the lock is held.
            unsafe { remove_waiter(sem, waiter) };
            return -(bindings::EINTR as c_int);
        }
        if timeout <= 0 {
            // SAFETY: The waiter is queued and the lock is held.
            unsafe { remove_waiter(sem, waiter) };
            return -(bindings::ETIME as c_int);
        }

        // SAFETY: The caller guarantees that changing current is valid.
        unsafe { bindings::rust_semaphore_set_current_state(state) };
        // SAFETY: This function releases the lock across the scheduling action.
        unsafe { unlock_irq(sem) };
        // SAFETY: The waiter is queued and sleeping is permitted.
        timeout = unsafe { bindings::rust_semaphore_schedule_timeout(timeout) };
        // SAFETY: This function reacquires the lock before inspection.
        unsafe { lock_irq(sem) };

        // SAFETY: The wait list remains valid and the lock is held.
        if unsafe { (*waiter).up } {
            // SAFETY: The semaphore lock is held.
            unsafe { bindings::rust_semaphore_set_holder(sem) };
            return 0;
        }
    }
}

/// Runs the contended path with contention tracing and blocker accounting.
///
/// # Safety
///
/// `sem` must be valid, its lock must be held, and the caller must permit
/// sleeping. The lock remains held when this function returns.
unsafe fn slow_path(sem: *mut Semaphore, state: c_uint, timeout: c_long) -> c_int {
    // SAFETY: The caller guarantees that the semaphore is valid and contended.
    unsafe { bindings::rust_semaphore_slow_enter(sem) };
    // SAFETY: The caller guarantees that the lock is held.
    let ret = unsafe { wait_contended(sem, state, timeout) };
    // SAFETY: The caller guarantees that the semaphore is still valid.
    unsafe { bindings::rust_semaphore_slow_exit(sem, ret) };
    ret
}

/// Runs the common acquisition protocol.
///
/// # Safety
///
/// `sem` must be valid and the caller must permit sleeping.
unsafe fn down_common(sem: *mut Semaphore, state: c_uint, timeout: c_long) -> c_int {
    // SAFETY: The caller guarantees that `sem` is valid.
    let flags = unsafe { lock_irqsave(sem) };
    // SAFETY: The semaphore lock is held.
    let ret = if unsafe { (*sem).count > 0 } {
        // SAFETY: The count is positive and the lock is held.
        unsafe { acquire(sem) };
        0
    } else {
        // SAFETY: The lock is held and sleeping is permitted.
        unsafe { slow_path(sem, state, timeout) }
    };
    // SAFETY: This function acquired the lock above.
    unsafe { unlock_irqrestore(sem, flags) };
    ret
}

/// Acquires a semaphore without interruption.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[export]
unsafe extern "C" fn rust_semaphore_down(sem: *mut Semaphore) {
    // SAFETY: The caller guarantees that `sem` is valid and sleeping is
    // permitted.
    unsafe { down_common(sem, bindings::TASK_UNINTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT) };
}

/// Acquires a semaphore unless interrupted.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[export]
unsafe extern "C" fn rust_semaphore_down_interruptible(sem: *mut Semaphore) -> c_int {
    // SAFETY: The caller guarantees that `sem` is valid and sleeping is
    // permitted.
    unsafe { down_common(sem, bindings::TASK_INTERRUPTIBLE, MAX_SCHEDULE_TIMEOUT) }
}

/// Acquires a semaphore unless killed.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[export]
unsafe extern "C" fn rust_semaphore_down_killable(sem: *mut Semaphore) -> c_int {
    // SAFETY: The caller guarantees that `sem` is valid and sleeping is
    // permitted.
    unsafe { down_common(sem, bindings::TASK_KILLABLE, MAX_SCHEDULE_TIMEOUT) }
}

/// Tries to acquire a semaphore without waiting.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[export]
unsafe extern "C" fn rust_semaphore_down_trylock(sem: *mut Semaphore) -> c_int {
    // SAFETY: The caller guarantees that `sem` is valid.
    let flags = unsafe { lock_irqsave(sem) };
    // SAFETY: The semaphore lock is held.
    let count = unsafe { (*sem).count.wrapping_sub(1) } as c_int;
    if count >= 0 {
        // SAFETY: The decremented count is non-negative and the lock is held.
        unsafe { acquire(sem) };
    }
    // SAFETY: This function acquired the lock above.
    unsafe { unlock_irqrestore(sem, flags) };
    c_int::from(count < 0)
}

/// Acquires a semaphore within a timeout.
///
/// # Safety
///
/// `sem` must point to a valid semaphore.
#[export]
unsafe extern "C" fn rust_semaphore_down_timeout(sem: *mut Semaphore, timeout: c_long) -> c_int {
    // SAFETY: The caller guarantees that `sem` is valid and sleeping is
    // permitted.
    unsafe { down_common(sem, bindings::TASK_UNINTERRUPTIBLE, timeout) }
}

/// Releases one semaphore count or hands it directly to the first waiter.
///
/// # Safety
///
/// `sem` must be valid and `wake_q` must point to an initialized wake queue.
#[export]
unsafe extern "C" fn rust_semaphore_up(sem: *mut Semaphore, wake_q: *mut WakeQ) {
    // SAFETY: The caller guarantees that `sem` is valid.
    let flags = unsafe { lock_irqsave(sem) };
    // SAFETY: The semaphore lock is held.
    unsafe { bindings::rust_semaphore_clear_holder(sem) };

    // SAFETY: The semaphore lock is held.
    let waiter = unsafe { (*sem).first_waiter };
    if waiter.is_null() {
        // SAFETY: The semaphore lock is held.
        unsafe { (*sem).count = (*sem).count.wrapping_add(1) };
    } else {
        // SAFETY: The first waiter is queued and the lock is held.
        unsafe { remove_waiter(sem, waiter) };
        // SAFETY: The waiter remains on the caller's stack until it reacquires
        // the semaphore lock and observes this handoff.
        unsafe { (*waiter).up = true };
        // SAFETY: A queued waiter has a valid task pointer.
        unsafe { bindings::rust_semaphore_wake_q_add(wake_q, (*waiter).task) };
    }

    // SAFETY: The semaphore lock is held.
    unsafe { bindings::rust_semaphore_trace_release(sem, wake_q) };
    // SAFETY: This function acquired the lock above. The helper unlocks and
    // performs the queued wake after releasing the semaphore lock.
    unsafe { bindings::rust_semaphore_unlock_irqrestore_wake(sem, flags, wake_q) };
}
