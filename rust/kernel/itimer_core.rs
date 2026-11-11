// SPDX-License-Identifier: GPL-2.0-only

//! Native interval timer state machine.
//!
//! Rust owns interval validation results, real and CPU timer state
//! transitions, real timer cancellation retry, remaining-time normalization,
//! period updates, and alarm rounding. C retains user copies, the signal lock,
//! hrtimer operations, CPU-time sampling, tracepoints, and SIGALRM delivery.

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint, c_ulong},
};
use core::ptr;
use macros::export;

type Task = bindings::task_struct;
type Signal = bindings::signal_struct;
type Hrtimer = bindings::hrtimer;
type ItimerSpec = bindings::itimerspec64;
type Timespec = bindings::timespec64;

const NSEC_PER_SEC: i64 = 1_000_000_000;
const KTIME_SEC_MAX: i64 = i64::MAX / NSEC_PER_SEC;
const KTIME_SEC_MIN: i64 = i64::MIN / NSEC_PER_SEC;

/// Converts a validated timespec to nanoseconds with the same saturation as
/// `timespec64_to_ns()`.
fn timespec_to_ns(ts: Timespec) -> i64 {
    let sec = ts.tv_sec;

    if sec >= KTIME_SEC_MAX {
        return i64::MAX;
    }
    if sec <= KTIME_SEC_MIN {
        return i64::MIN;
    }

    sec * NSEC_PER_SEC + ts.tv_nsec as i64
}

/// Converts a validated timespec to ktime with the same saturation as
/// `timespec64_to_ktime()`.
fn timespec_to_ktime(ts: Timespec) -> i64 {
    if ts.tv_sec >= KTIME_SEC_MAX {
        return i64::MAX;
    }

    ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec as i64
}

/// Converts a nonnegative nanosecond value to a normalized timespec.
fn ns_to_timespec(ns: i64) -> Timespec {
    Timespec {
        tv_sec: ns / NSEC_PER_SEC,
        tv_nsec: (ns % NSEC_PER_SEC) as c_long,
    }
}

/// Returns the signal state owned by a task.
///
/// # Safety
///
/// `task` must point to a live task with a live signal structure.
#[inline]
unsafe fn signal(task: *mut Task) -> *mut Signal {
    // SAFETY: The caller guarantees that the task and its signal state are live.
    unsafe { (*task).signal }
}

/// Returns the process real-time interval timer.
///
/// # Safety
///
/// `sig` must point to a live signal structure.
#[inline]
unsafe fn real_timer(sig: *mut Signal) -> *mut Hrtimer {
    // SAFETY: The caller guarantees that the signal structure is live.
    unsafe { ptr::addr_of_mut!((*sig).real_timer) }
}

/// Acquires the task signal lock with interrupts disabled.
///
/// # Safety
///
/// `task` must point to a live task. The caller must permit interrupt
/// disabling and must not already hold the signal lock.
#[inline]
unsafe fn lock_siglock(task: *mut Task) {
    // SAFETY: The signal lock is valid and the caller permits acquisition.
    unsafe { bindings::rust_helper_itimer_lock(task) };
}

/// Releases the task signal lock and enables interrupts.
///
/// # Safety
///
/// `task` must point to a live task and the caller must hold its signal lock.
#[inline]
unsafe fn unlock_siglock(task: *mut Task) {
    // SAFETY: The caller holds the signal lock.
    unsafe { bindings::rust_helper_itimer_unlock(task) };
}

/// Returns the normalized remaining time of a real interval timer.
///
/// # Safety
///
/// `timer` must point to the process real timer. The signal lock must be held.
#[inline]
unsafe fn real_remaining(timer: *mut Hrtimer) -> Timespec {
    // SAFETY: The caller holds the signal lock and the timer remains valid.
    let mut remaining = unsafe { bindings::__hrtimer_get_remaining(timer, true) };

    // SAFETY: The caller holds the signal lock and the timer remains valid.
    if unsafe { bindings::hrtimer_active(timer) } {
        if remaining <= 0 {
            remaining = i64::from(bindings::NSEC_PER_USEC);
        }
    } else {
        remaining = 0;
    }

    ns_to_timespec(remaining)
}

/// Reads one CPU interval timer while the signal lock is held.
///
/// # Safety
///
/// `task` must point to a live task, and `value` must be writable.
#[inline]
unsafe fn get_cpu_itimer(task: *mut Task, clock_id: c_uint, value: *mut ItimerSpec) {
    // SAFETY: The caller guarantees that task and value are valid.
    let sig = unsafe { signal(task) };
    // SAFETY: The caller guarantees that the signal structure is live.
    let timer = unsafe { (*sig).it[clock_id as usize] };
    let mut remaining = timer.expires;

    if remaining != 0 {
        // SAFETY: The caller holds the signal lock and supplied a valid task.
        let now = unsafe { bindings::rust_helper_itimer_sample_cputime(task, clock_id) };

        if remaining <= now {
            remaining = u64::from(bindings::TICK_NSEC);
        } else {
            remaining -= now;
        }
    }

    // SAFETY: The caller guarantees that value is writable.
    unsafe {
        (*value).it_value = ns_to_timespec(remaining as i64);
        (*value).it_interval = ns_to_timespec(timer.incr as i64);
    }
}

/// Returns an interval timer through the native state machine.
///
/// # Safety
///
/// `task` must point to the current task and `value` must be writable.
#[export]
unsafe extern "C" fn rust_itimer_get(
    task: *mut Task,
    which: c_uint,
    value: *mut ItimerSpec,
) -> c_int {
    match which {
        bindings::ITIMER_REAL => {
            // SAFETY: The caller guarantees that the task and output are valid.
            unsafe { lock_siglock(task) };
            // SAFETY: The caller guarantees that the signal state is live.
            let sig = unsafe { signal(task) };
            // SAFETY: The caller guarantees that the signal state is live.
            let remaining = unsafe { real_remaining(real_timer(sig)) };
            // SAFETY: The signal lock is held.
            let interval = unsafe { (*sig).it_real_incr };
            // SAFETY: This function acquired the signal lock.
            unsafe { unlock_siglock(task) };

            // SAFETY: The caller guarantees that value is writable.
            unsafe {
                (*value).it_value = remaining;
                (*value).it_interval = ns_to_timespec(interval);
            }
            0
        }
        bindings::ITIMER_VIRTUAL => {
            // SAFETY: The caller guarantees that the task is current.
            unsafe { lock_siglock(task) };
            // SAFETY: The caller guarantees that task and value are valid.
            unsafe { get_cpu_itimer(task, bindings::CPUCLOCK_VIRT, value) };
            // SAFETY: This function acquired the signal lock.
            unsafe { unlock_siglock(task) };
            0
        }
        bindings::ITIMER_PROF => {
            // SAFETY: The caller guarantees that the task is current.
            unsafe { lock_siglock(task) };
            // SAFETY: The caller guarantees that task and value are valid.
            unsafe { get_cpu_itimer(task, bindings::CPUCLOCK_PROF, value) };
            // SAFETY: This function acquired the signal lock.
            unsafe { unlock_siglock(task) };
            0
        }
        _ => -(bindings::EINVAL as c_int),
    }
}

/// Sets the process real-time interval timer.
///
/// # Safety
///
/// `task` must point to the current task. `value` must be valid and
/// `old_value` must be writable when non-null.
unsafe fn set_real_itimer(task: *mut Task, value: *const ItimerSpec, old_value: *mut ItimerSpec) {
    // SAFETY: The caller guarantees that value is valid.
    let input = unsafe { *value };

    loop {
        // SAFETY: The caller guarantees that task is current.
        unsafe { lock_siglock(task) };
        // SAFETY: The caller guarantees that the signal state is live.
        let sig = unsafe { signal(task) };
        // SAFETY: The caller guarantees that the signal state is live.
        let timer = unsafe { real_timer(sig) };

        if !old_value.is_null() {
            // SAFETY: The signal lock is held and the output is writable.
            let remaining = unsafe { real_remaining(timer) };
            // SAFETY: The signal lock is held.
            let interval = unsafe { (*sig).it_real_incr };
            // SAFETY: The caller guarantees that old_value is writable.
            unsafe {
                (*old_value).it_value = remaining;
                (*old_value).it_interval = ns_to_timespec(interval);
            }
        }

        // SAFETY: The signal lock is held and the timer remains valid.
        if unsafe { bindings::hrtimer_try_to_cancel(timer) } < 0 {
            // SAFETY: This function acquired the signal lock.
            unsafe { unlock_siglock(task) };
            // SAFETY: The callback owns the timer lock. Waiting preserves the
            // retry contract used by the former C implementation.
            unsafe { bindings::rust_helper_itimer_cancel_wait_running(timer) };
            continue;
        }

        let expires = timespec_to_ktime(input.it_value);
        if expires != 0 {
            // SAFETY: The signal lock is held.
            unsafe { (*sig).it_real_incr = timespec_to_ktime(input.it_interval) };
            // SAFETY: The timer is cancelled and the signal lock is held.
            unsafe { bindings::rust_helper_itimer_start(timer, expires) };
        } else {
            // SAFETY: The signal lock is held.
            unsafe { (*sig).it_real_incr = 0 };
        }

        // SAFETY: The signal lock is held and value remains valid.
        unsafe {
            bindings::rust_helper_itimer_trace_state(bindings::ITIMER_REAL, value, 0);
        }
        // SAFETY: This function acquired the signal lock.
        unsafe { unlock_siglock(task) };
        return;
    }
}

/// Sets one CPU interval timer.
///
/// # Safety
///
/// `task` must point to the current task. `value` must be valid and
/// `old_value` must be writable when non-null.
unsafe fn set_cpu_itimer(
    task: *mut Task,
    clock_id: c_uint,
    which: c_uint,
    value: *const ItimerSpec,
    old_value: *mut ItimerSpec,
) {
    // SAFETY: The caller guarantees that value is valid.
    let input = unsafe { *value };
    let mut new_value = timespec_to_ns(input.it_value) as u64;
    let new_interval = timespec_to_ns(input.it_interval) as u64;

    // SAFETY: The caller guarantees that task is current.
    unsafe { lock_siglock(task) };
    // SAFETY: The caller guarantees that the signal state is live.
    let sig = unsafe { signal(task) };
    // SAFETY: The signal lock is held and clock_id selects one of two entries.
    let timer = unsafe { ptr::addr_of_mut!((*sig).it[clock_id as usize]) };
    // SAFETY: The signal lock is held.
    let (old_interval, mut old_value_ns) = unsafe { ((*timer).incr, (*timer).expires) };

    if old_value_ns != 0 || new_value != 0 {
        if new_value > 0 {
            new_value = new_value.wrapping_add(u64::from(bindings::TICK_NSEC));
        }
        // SAFETY: The signal lock is held and the stack variables are valid.
        unsafe {
            bindings::rust_helper_itimer_set_process_cpu_timer(
                task,
                clock_id,
                &mut new_value,
                &mut old_value_ns,
            );
        }
    }

    // SAFETY: The signal lock is held and the timer entry is live.
    unsafe {
        (*timer).expires = new_value;
        (*timer).incr = new_interval;
        bindings::rust_helper_itimer_trace_state(which, value, new_value);
    }
    // SAFETY: This function acquired the signal lock.
    unsafe { unlock_siglock(task) };

    if !old_value.is_null() {
        // SAFETY: The caller guarantees that old_value is writable.
        unsafe {
            (*old_value).it_value = ns_to_timespec(old_value_ns as i64);
            (*old_value).it_interval = ns_to_timespec(old_interval as i64);
        }
    }
}

/// Sets an interval timer through the native state machine.
///
/// # Safety
///
/// `task` must point to the current task. `value` must be valid and
/// `old_value` must be writable when non-null.
#[export]
unsafe extern "C" fn rust_itimer_set(
    task: *mut Task,
    which: c_uint,
    value: *const ItimerSpec,
    old_value: *mut ItimerSpec,
) -> c_int {
    match which {
        bindings::ITIMER_REAL => {
            // SAFETY: The caller guarantees the task, input, and output contract.
            unsafe { set_real_itimer(task, value, old_value) };
            0
        }
        bindings::ITIMER_VIRTUAL => {
            // SAFETY: The caller guarantees the task, input, and output contract.
            unsafe {
                set_cpu_itimer(
                    task,
                    bindings::CPUCLOCK_VIRT,
                    bindings::ITIMER_VIRTUAL,
                    value,
                    old_value,
                )
            };
            0
        }
        bindings::ITIMER_PROF => {
            // SAFETY: The caller guarantees the task, input, and output contract.
            unsafe {
                set_cpu_itimer(
                    task,
                    bindings::CPUCLOCK_PROF,
                    bindings::ITIMER_PROF,
                    value,
                    old_value,
                )
            };
            0
        }
        _ => -(bindings::EINVAL as c_int),
    }
}

/// Rearms a periodic real-time interval timer after SIGALRM delivery.
///
/// # Safety
///
/// `task` must point to a live task.
#[export]
unsafe extern "C" fn rust_itimer_rearm(task: *mut Task) {
    // SAFETY: The caller guarantees that the task and signal state are live.
    let sig = unsafe { signal(task) };
    // SAFETY: The caller guarantees that the signal state is live.
    let timer = unsafe { real_timer(sig) };
    // SAFETY: The caller guarantees that the signal state is live.
    let interval = unsafe { (*sig).it_real_incr };

    // SAFETY: The timer remains valid and the helper applies the queued test.
    if !unsafe { bindings::rust_helper_itimer_is_queued(timer) } && interval != 0 {
        // SAFETY: The timer is not queued and remains valid.
        unsafe {
            bindings::rust_helper_itimer_forward_now(timer, interval);
            bindings::rust_helper_itimer_restart(timer);
        }
    }
}

/// Implements the legacy alarm system call and its remaining-seconds rounding.
///
/// # Safety
///
/// `task` must point to the current task.
#[export]
unsafe extern "C" fn rust_itimer_alarm(task: *mut Task, seconds: c_uint) -> c_uint {
    let seconds = if core::mem::size_of::<c_ulong>() < core::mem::size_of::<i64>()
        && seconds > c_int::MAX as c_uint
    {
        c_int::MAX as c_uint
    } else {
        seconds
    };
    let input = ItimerSpec {
        it_interval: Timespec::default(),
        it_value: Timespec {
            tv_sec: i64::from(seconds),
            tv_nsec: 0,
        },
    };
    let mut old_value = ItimerSpec::default();

    // SAFETY: The caller guarantees that task is current.
    if unsafe { rust_itimer_set(task, bindings::ITIMER_REAL, &input, &mut old_value) } != 0 {
        return 0;
    }

    let mut old_seconds = old_value.it_value.tv_sec as c_uint;
    if (old_seconds == 0 && old_value.it_value.tv_nsec != 0)
        || old_value.it_value.tv_nsec >= (NSEC_PER_SEC / 2) as c_long
    {
        old_seconds = old_seconds.wrapping_add(1);
    }

    old_seconds
}
