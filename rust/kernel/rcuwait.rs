// SPDX-License-Identifier: GPL-2.0-only

//! Native `rcuwait` single-task wait state machine.
//!
//! Rust owns task publication, wait-state transitions, condition and signal
//! decisions, timeout accounting, scheduling selection, completion cleanup,
//! and wake ordering. C retains condition-expression expansion and the
//! low-level RCU, task-state, scheduler, and wakeup primitives.

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint},
};
use macros::export;

type Wait = bindings::rcuwait;

/// Initializes a wait object.
///
/// # Safety
///
/// `w` must point to valid, writable wait storage.
#[export]
unsafe extern "C" fn rust_rcuwait_init(w: *mut Wait) {
    // SAFETY: The caller guarantees valid wait storage.
    unsafe { bindings::rcuwait_assign_null(w) };
}

/// Reports whether a task is published on a wait object.
///
/// # Safety
///
/// `w` must point to valid wait storage.
#[export]
unsafe extern "C" fn rust_rcuwait_active(w: *mut Wait) -> c_int {
    // SAFETY: The caller guarantees valid wait storage.
    c_int::from(unsafe { bindings::rcuwait_access(w) })
}

/// Publishes the current task on a wait object.
///
/// # Safety
///
/// `w` must point to valid wait storage and the caller must serialize writes
/// to `w->task`.
#[export]
unsafe extern "C" fn rust_rcuwait_prepare(w: *mut Wait) {
    // SAFETY: The caller guarantees valid wait storage and serialization.
    unsafe { bindings::rcuwait_assign_current(w) };
}

/// Clears the waiter and restores the current task state.
///
/// # Safety
///
/// `w` must point to valid wait storage and the caller must serialize writes
/// to `w->task`.
#[export]
unsafe extern "C" fn rust_rcuwait_finish(w: *mut Wait) {
    // SAFETY: The caller guarantees valid wait storage and serialization.
    unsafe { bindings::rcuwait_assign_null(w) };
    // SAFETY: The current task is completing an rcuwait operation.
    unsafe { bindings::rcuwait_set_current_running() };
}

/// Wakes the task published on a wait object.
///
/// # Safety
///
/// `w` must remain valid until the RCU read-side critical section completes.
#[export]
unsafe extern "C" fn rust_rcuwait_wake_up(w: *mut Wait) -> c_int {
    // SAFETY: The caller guarantees that the wait storage remains valid.
    unsafe { bindings::rcu_read_lock() };
    // This orders the wake condition before the waiter pointer load.
    //
    // SAFETY: This helper has no additional requirements.
    unsafe { bindings::smp_mb() };
    // SAFETY: The RCU read-side critical section is active.
    let task = unsafe { bindings::rcuwait_dereference(w) };
    let ret = if task.is_null() {
        0
    } else {
        // SAFETY: The RCU-protected pointer references a live task.
        unsafe { bindings::rcuwait_wake_up_process(task) }
    };
    // SAFETY: This function acquired the RCU read-side critical section.
    unsafe { bindings::rcu_read_unlock() };
    ret
}

/// Sets the current task state before one condition evaluation.
///
/// # Safety
///
/// The caller must be in a context that permits the requested scheduler state.
#[export]
unsafe extern "C" fn rust_rcuwait_wait_begin(state: c_uint) {
    // SAFETY: The caller guarantees a valid scheduler context.
    unsafe { bindings::rcuwait_set_current_state(state) };
}

/// Makes the finite-wait completion decision before task publication.
///
/// Returns true when the initial condition completes the wait. Returns false
/// when the caller must start a wait operation.
///
/// # Safety
///
/// `ret` must point to writable timeout or result storage.
#[export]
unsafe extern "C" fn rust_rcuwait_wait_initial(ret: *mut c_long, condition: bool) -> bool {
    // SAFETY: The caller guarantees writable result storage.
    let ret = unsafe { &mut *ret };

    if condition && *ret == 0 {
        *ret = 1;
    }

    condition || *ret == 0
}

/// Makes the wait decision after one condition evaluation.
///
/// Returns true when the wait is complete. Returns false after scheduling when
/// another condition evaluation is required.
///
/// # Safety
///
/// `w` must remain valid for the duration of the wait. `ret` must point to
/// writable timeout or result storage. The caller must permit sleeping and
/// must serialize writes to `w->task`.
#[export]
unsafe extern "C" fn rust_rcuwait_wait_step(
    _w: *mut Wait,
    condition: bool,
    state: c_uint,
    ret: *mut c_long,
    timed: bool,
) -> bool {
    // SAFETY: The caller guarantees writable result storage.
    let ret = unsafe { &mut *ret };

    if condition {
        if timed && *ret == 0 {
            *ret = 1;
        }
        return true;
    }

    if timed && *ret == 0 {
        return true;
    }

    // SAFETY: The caller guarantees a valid current task and scheduler state.
    if unsafe { bindings::rcuwait_signal_pending_state(state) } != 0 {
        *ret = -(bindings::EINTR as c_long);
        return true;
    }

    if timed {
        // SAFETY: The caller permits sleeping and provided a nonzero timeout.
        *ret = unsafe { bindings::rcuwait_schedule_timeout(*ret) };
    } else {
        // SAFETY: The caller permits sleeping and owns the task state.
        unsafe { bindings::rcuwait_schedule() };
    }

    false
}
