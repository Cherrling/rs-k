// SPDX-License-Identifier: GPL-2.0-only

//! Native state machine for completion synchronization.
//!
//! Rust owns completion counts, waiter loops, timeout and signal decisions,
//! single and broadcast completion, and non-blocking consumption. C helpers
//! retain locking, task state, scheduling, signal queries, and swait queue
//! primitives.

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint, c_ulong},
};
use core::{mem::MaybeUninit, ptr};
use macros::export;

type Completion = bindings::completion;
type Wait = bindings::swait_queue;

const DONE_MAX: c_uint = c_uint::MAX;
const MAX_SCHEDULE_TIMEOUT: c_long = c_long::MAX;

/// Reads the completion count with `READ_ONCE()` semantics.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[inline]
unsafe fn read_done(x: *const Completion) -> c_uint {
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { bindings::rust_completion_read_done(x) }
}

/// Writes the completion count.
///
/// # Safety
///
/// `x` must point to a valid, writable completion. The caller must hold the
/// completion wait lock.
#[inline]
unsafe fn write_done(x: *mut Completion, done: c_uint) {
    // SAFETY: The caller guarantees that `x` is valid and locked.
    unsafe { bindings::rust_completion_write_done(x, done) };
}

/// Returns the completion wait queue.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[inline]
unsafe fn wait_queue(x: *mut Completion) -> *mut bindings::swait_queue_head {
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { ptr::addr_of_mut!((*x).wait) }
}

/// Acquires the completion wait lock and disables interrupts.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[inline]
unsafe fn lock_irqsave(x: *mut Completion) -> usize {
    let mut flags = 0;
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { bindings::rust_completion_lock_irqsave(x, &mut flags) };
    flags
}

/// Releases the completion wait lock and restores interrupts.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must own its wait lock.
#[inline]
unsafe fn unlock_irqrestore(x: *mut Completion, flags: usize) {
    // SAFETY: The caller guarantees that `x` is valid and locked.
    unsafe { bindings::rust_completion_unlock_irqrestore(x, flags) };
}

/// Acquires the completion wait lock with interrupts disabled.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must permit interrupt
/// disabling.
#[inline]
unsafe fn lock_irq(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { bindings::rust_completion_lock_irq(x) };
}

/// Releases the completion wait lock and enables interrupts.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must own its wait lock.
#[inline]
unsafe fn unlock_irq(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid and locked.
    unsafe { bindings::rust_completion_unlock_irq(x) };
}

/// Wakes one waiter while the completion wait lock is held.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must hold its wait lock.
#[inline]
unsafe fn wake_locked(x: *mut Completion, wake_flags: c_int) {
    // SAFETY: The caller guarantees that the completion and its wait lock are valid.
    unsafe { bindings::swake_up_locked(wait_queue(x), wake_flags) };
}

/// Wakes all waiters while the completion wait lock is held.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must hold its wait lock.
#[inline]
unsafe fn wake_all_locked(x: *mut Completion) {
    // SAFETY: The caller guarantees that the completion and its wait lock are valid.
    unsafe { bindings::swake_up_all_locked(wait_queue(x)) };
}

/// Prepares a swait entry under the completion wait lock.
///
/// # Safety
///
/// `x` and `wait` must be valid and the caller must hold the completion wait
/// lock.
#[inline]
unsafe fn prepare_wait(x: *mut Completion, wait: *mut Wait) {
    // SAFETY: The caller guarantees that both pointers are valid and the lock is held.
    unsafe { bindings::__prepare_to_swait(wait_queue(x), wait) };
}

/// Finishes a swait entry under the completion wait lock.
///
/// # Safety
///
/// `x` and `wait` must be valid and the caller must hold the completion wait
/// lock.
#[inline]
unsafe fn finish_wait(x: *mut Completion, wait: *mut Wait) {
    // SAFETY: The caller guarantees that both pointers are valid and the lock is held.
    unsafe { bindings::__finish_swait(wait_queue(x), wait) };
}

/// Executes one scheduling action after releasing the completion lock.
///
/// # Safety
///
/// The completion lock must be held. This function drops it while blocked and
/// reacquires it before returning.
#[inline]
unsafe fn schedule_timeout(x: *mut Completion, timeout: c_long, io_wait: bool) -> c_long {
    // SAFETY: The caller guarantees that the completion lock is held.
    unsafe { unlock_irq(x) };
    // SAFETY: The caller permits sleeping and the wait entry is queued.
    let ret = unsafe {
        if io_wait {
            bindings::rust_completion_io_schedule_timeout(timeout)
        } else {
            bindings::rust_completion_schedule_timeout(timeout)
        }
    };
    // SAFETY: This function acquires the lock before returning.
    unsafe { lock_irq(x) };
    ret
}

/// Waits until completion, timeout, or signal while holding the wait lock.
///
/// # Safety
///
/// `x` must point to a valid completion and the caller must hold its wait lock.
/// The caller must permit sleeping.
unsafe fn do_wait(x: *mut Completion, mut timeout: c_long, state: c_uint, io_wait: bool) -> c_long {
    // SAFETY: The caller guarantees that `x` is valid.
    if unsafe { read_done(x) } == 0 {
        let mut wait = MaybeUninit::<Wait>::uninit();
        // SAFETY: The temporary wait entry is initialized in place.
        unsafe { bindings::rust_completion_init_swait_queue(wait.as_mut_ptr()) };
        let wait = wait.as_mut_ptr();

        loop {
            // SAFETY: The caller guarantees that querying current is valid.
            if unsafe { bindings::rust_completion_signal_pending_state(state) } != 0 {
                timeout = -(bindings::ERESTARTSYS as c_long);
                break;
            }

            // SAFETY: The wait entry is initialized and the lock is held.
            unsafe { prepare_wait(x, wait) };
            // SAFETY: The caller guarantees that changing current is valid.
            unsafe { bindings::rust_completion_set_current_state(state) };
            // SAFETY: This releases the lock across the scheduling action.
            timeout = unsafe { schedule_timeout(x, timeout, io_wait) };

            // SAFETY: The completion remains valid and the lock is held.
            if unsafe { read_done(x) } != 0 || timeout == 0 {
                break;
            }
        }

        // SAFETY: The wait entry is initialized and the lock is held.
        unsafe { finish_wait(x, wait) };
        // SAFETY: The completion remains valid and the lock is held.
        if unsafe { read_done(x) } == 0 {
            return timeout;
        }
    }

    // SAFETY: The completion remains valid and the lock is held.
    let done = unsafe { read_done(x) };
    if done != DONE_MAX {
        // SAFETY: The completion is locked and the count was just observed.
        unsafe { write_done(x, done - 1) };
    }

    if timeout == 0 {
        1
    } else {
        timeout
    }
}

/// Runs the common completion wait protocol.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
unsafe fn wait_common(x: *mut Completion, timeout: c_long, state: c_uint, io_wait: bool) -> c_long {
    // SAFETY: The caller guarantees that this is a sleepable context.
    unsafe { bindings::rust_completion_might_sleep() };
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { bindings::rust_completion_complete_acquire(x) };
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { lock_irq(x) };
    // SAFETY: The completion lock is held.
    let ret = unsafe { do_wait(x, timeout, state, io_wait) };
    // SAFETY: This function acquired the completion lock.
    unsafe { unlock_irq(x) };
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { bindings::rust_completion_complete_release(x) };
    ret
}

/// Completes one waiter or increments the completion count.
///
/// # Safety
///
/// `x` must point to a valid completion.
unsafe fn complete_with_flags(x: *mut Completion, current_cpu: bool) {
    let wake_flags = if current_cpu {
        // SAFETY: The scheduler helper has no additional requirements.
        unsafe { bindings::rust_completion_current_cpu_wake_flags() }
    } else {
        0
    };
    // SAFETY: The caller guarantees that `x` is valid.
    let flags = unsafe { lock_irqsave(x) };
    // SAFETY: The completion wait lock is held.
    let done = unsafe { read_done(x) };
    if done != DONE_MAX {
        // SAFETY: The completion wait lock is held.
        unsafe { write_done(x, done + 1) };
    }
    // SAFETY: The completion wait lock is held.
    unsafe { wake_locked(x, wake_flags) };
    // SAFETY: This function acquired the completion wait lock.
    unsafe { unlock_irqrestore(x, flags) };
}

/// Signals one waiter on the current CPU when possible.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[export]
unsafe extern "C" fn complete_on_current_cpu(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { complete_with_flags(x, true) };
}

/// Signals one waiter.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[export]
unsafe extern "C" fn complete(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid.
    unsafe { complete_with_flags(x, false) };
}

/// Permanently completes all current and future waiters.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[export]
unsafe extern "C" fn complete_all(x: *mut Completion) {
    // SAFETY: This helper checks the current locking context.
    unsafe { bindings::rust_completion_assert_rt_threaded_ctx() };
    // SAFETY: The caller guarantees that `x` is valid.
    let flags = unsafe { lock_irqsave(x) };
    // SAFETY: The completion wait lock is held.
    unsafe { write_done(x, DONE_MAX) };
    // SAFETY: The completion wait lock is held.
    unsafe { wake_all_locked(x) };
    // SAFETY: This function acquired the completion wait lock.
    unsafe { unlock_irqrestore(x, flags) };
}

/// Waits without interruption.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe {
        wait_common(
            x,
            MAX_SCHEDULE_TIMEOUT,
            bindings::TASK_UNINTERRUPTIBLE,
            false,
        )
    };
}

/// Waits without interruption or beyond the specified timeout.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_timeout(x: *mut Completion, timeout: c_ulong) -> c_ulong {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_common(x, timeout as c_long, bindings::TASK_UNINTERRUPTIBLE, false) as c_ulong }
}

/// Waits for I/O-accounted completion without interruption.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_io(x: *mut Completion) {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe {
        wait_common(
            x,
            MAX_SCHEDULE_TIMEOUT,
            bindings::TASK_UNINTERRUPTIBLE,
            true,
        )
    };
}

/// Waits for I/O-accounted completion without interruption or beyond the
/// specified timeout.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_io_timeout(
    x: *mut Completion,
    timeout: c_ulong,
) -> c_ulong {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_common(x, timeout as c_long, bindings::TASK_UNINTERRUPTIBLE, true) as c_ulong }
}

/// Converts a common wait result to the non-timeout wait API contract.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
unsafe fn wait_interruptible(
    x: *mut Completion,
    timeout: c_long,
    state: c_uint,
    io_wait: bool,
) -> c_int {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    let ret = unsafe { wait_common(x, timeout, state, io_wait) };
    if ret == -(bindings::ERESTARTSYS as c_long) {
        ret as c_int
    } else {
        0
    }
}

/// Waits interruptibly.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_interruptible(x: *mut Completion) -> c_int {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_interruptible(x, MAX_SCHEDULE_TIMEOUT, bindings::TASK_INTERRUPTIBLE, false) }
}

/// Waits interruptibly or until the specified timeout.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_interruptible_timeout(
    x: *mut Completion,
    timeout: c_ulong,
) -> c_long {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_common(x, timeout as c_long, bindings::TASK_INTERRUPTIBLE, false) }
}

/// Waits killably.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_killable(x: *mut Completion) -> c_int {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_interruptible(x, MAX_SCHEDULE_TIMEOUT, bindings::TASK_KILLABLE, false) }
}

/// Waits with an explicit task state.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_state(x: *mut Completion, state: c_uint) -> c_int {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_interruptible(x, MAX_SCHEDULE_TIMEOUT, state, false) }
}

/// Waits killably or until the specified timeout.
///
/// # Safety
///
/// `x` must point to a valid completion. The caller must permit sleeping.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn wait_for_completion_killable_timeout(
    x: *mut Completion,
    timeout: c_ulong,
) -> c_long {
    // SAFETY: The caller guarantees that `x` is valid and sleeping is permitted.
    unsafe { wait_common(x, timeout as c_long, bindings::TASK_KILLABLE, false) }
}

/// Attempts to consume one completion without blocking.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[export]
unsafe extern "C" fn try_wait_for_completion(x: *mut Completion) -> bool {
    // SAFETY: The caller guarantees that `x` is valid.
    if unsafe { read_done(x) } == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `x` is valid.
    let flags = unsafe { lock_irqsave(x) };
    // SAFETY: The completion wait lock is held.
    let done = unsafe { read_done(x) };
    let ret = done != 0;
    if done != 0 && done != DONE_MAX {
        // SAFETY: The completion wait lock is held.
        unsafe { write_done(x, done - 1) };
    }
    // SAFETY: This function acquired the completion wait lock.
    unsafe { unlock_irqrestore(x, flags) };
    ret
}

/// Reports whether the completion count is nonzero and synchronizes with
/// ``complete()``.
///
/// # Safety
///
/// `x` must point to a valid completion.
#[export]
unsafe extern "C" fn completion_done(x: *mut Completion) -> bool {
    // SAFETY: The caller guarantees that `x` is valid.
    if unsafe { read_done(x) } == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `x` is valid.
    let flags = unsafe { lock_irqsave(x) };
    // SAFETY: This function acquired the completion wait lock.
    unsafe { unlock_irqrestore(x, flags) };
    true
}
