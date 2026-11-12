// SPDX-License-Identifier: GPL-2.0-only

//! Native scheduler wake-state matching.
//!
//! Rust owns the task-state match result and the saved-state transition used
//! by task wakeups. C retains task-state memory accesses, warning delivery,
//! task locking, run-queue operations, and task selection.

use crate::{bindings, ffi::c_int};
use macros::export;

type Task = bindings::task_struct;
#[cfg(any(CONFIG_RUST_SCHED_TTWU_ACTIVATE, CONFIG_RUST_SCHED_TTWU_RUNNABLE))]
type Rq = bindings::rq;

/// Classifies a task state against a requested wake mask.
fn state_match(task_state: u32, saved_state: u32, requested_state: u32) -> c_int {
    if task_state & requested_state != 0 {
        1
    } else if saved_state & requested_state != 0 {
        -1
    } else {
        0
    }
}

/// Returns the task-state match result for the scheduler.
///
/// A positive result matches the task's current state. A negative result
/// matches its saved state and requires the caller to wait for a lock or
/// freezer restore. Zero means that the requested state did not match.
///
/// # Safety
///
/// `task` must remain live for the duration of the call.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
#[export]
unsafe extern "C" fn rust_sched_task_state_match(task: *const Task, state: u32) -> c_int {
    // SAFETY: The caller guarantees that the task remains live.
    let task_state = unsafe { bindings::rust_helper_sched_task_state(task) } as u32;
    // SAFETY: The caller guarantees that the task remains live.
    let saved_state = unsafe { bindings::rust_helper_sched_task_saved_state(task) } as u32;
    state_match(task_state, saved_state, state)
}

/// Runs the scheduler wake-state match and updates the saved state.
///
/// A current-state match wakes the task. A saved-state match reports success
/// without waking the task and clears the saved state so a later lock restore
/// does not lose visibility of the wakeup.
///
/// # Safety
///
/// `task` must remain live and `success` must point to writable storage. The
/// caller must serialize access as required by `try_to_wake_up()`.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
#[export]
unsafe extern "C" fn rust_sched_ttwu_state_match(
    task: *mut Task,
    state: u32,
    success: *mut c_int,
) -> bool {
    if cfg!(CONFIG_DEBUG_PREEMPT)
        && state & bindings::TASK_RTLOCK_WAIT != 0
        && state != bindings::TASK_RTLOCK_WAIT
    {
        // SAFETY: The helper only reports the invalid wake mask.
        unsafe { bindings::rust_helper_sched_warn_ttwu_state(state) };
    }

    // SAFETY: The caller guarantees that the task remains live.
    let matched = unsafe { rust_sched_task_state_match(task.cast_const(), state) };
    // SAFETY: The caller guarantees writable success storage.
    unsafe { *success = c_int::from(matched != 0) };

    if matched < 0 {
        // SAFETY: The caller provides the serialization required by the
        // scheduler's saved-state protocol.
        unsafe { bindings::rust_helper_sched_set_task_saved_state(task, bindings::TASK_RUNNING) };
    }

    matched > 0
}

/// Returns the state-match result for the wake-state KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_WAKE_STATE))]
#[export]
extern "C" fn rust_sched_state_match_select(
    task_state: u32,
    saved_state: u32,
    requested_state: u32,
) -> c_int {
    state_match(task_state, saved_state, requested_state)
}

/// Implements the task-runqueue-locked runnable wake path.
///
/// Rust owns the ordering of the queued check, clock update, delayed enqueue,
/// proxy return, preemption decision, and wake-state publication.
///
/// # Safety
///
/// `task` must remain live, `rq` must be its current runqueue, and the caller
/// must hold the task runqueue lock required by `ttwu_runnable()`.
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
#[export]
unsafe extern "C" fn rust_sched_ttwu_runnable_locked(
    rq: *mut Rq,
    task: *mut Task,
    wake_flags: c_int,
) -> c_int {
    // SAFETY: The caller holds the task runqueue lock.
    if !unsafe { bindings::rust_helper_sched_ttwu_rq_queued(task) } {
        return 0;
    }

    // SAFETY: The caller holds the task runqueue lock.
    unsafe { bindings::rust_helper_sched_ttwu_update_clock(rq) };

    // SAFETY: The caller holds the task runqueue lock.
    if unsafe { bindings::rust_helper_sched_ttwu_is_blocked(task) } {
        // SAFETY: The caller holds the task runqueue lock.
        if unsafe { bindings::rust_helper_sched_ttwu_is_delayed(task) } {
            // SAFETY: The task is queued on this runqueue.
            unsafe { bindings::rust_helper_sched_ttwu_enqueue_delayed(rq, task) };
        }

        // SAFETY: The caller holds the task runqueue lock.
        if unsafe { bindings::rust_helper_sched_ttwu_proxy_return(rq, task) } {
            return 0;
        }
    }

    // SAFETY: The caller holds the task runqueue lock.
    if !unsafe { bindings::rust_helper_sched_ttwu_on_cpu(rq, task) } {
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { bindings::rust_helper_sched_ttwu_preempt(rq, task, wake_flags) };
    }

    // SAFETY: The caller holds the task runqueue lock.
    unsafe { bindings::rust_helper_sched_ttwu_wake(task) };
    1
}

#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_DEC_LOAD: u32 = 0x01;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_RQ_SELECTED: u32 = 0x02;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_MIGRATED: u32 = 0x04;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_IO_WAIT: u32 = 0x08;

/// Selects the state-changing actions for a woke task activation.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
fn activate_plan(
    contributes_load: bool,
    rq_selected: bool,
    migrated: bool,
    in_iowait: bool,
) -> u32 {
    let mut plan = 0;
    if contributes_load {
        plan |= ACTIVATE_DEC_LOAD;
    }
    if rq_selected {
        plan |= ACTIVATE_RQ_SELECTED;
    }
    if migrated {
        plan |= ACTIVATE_MIGRATED;
    } else if in_iowait {
        plan |= ACTIVATE_IO_WAIT;
    }
    plan
}

/// Returns the activation action bits for the wake-state KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
extern "C" fn rust_sched_ttwu_activate_select(
    contributes_load: bool,
    rq_selected: bool,
    migrated: bool,
    in_iowait: bool,
) -> u32 {
    activate_plan(contributes_load, rq_selected, migrated, in_iowait)
}

/// Activates a woke task while its runqueue lock remains held.
///
/// Rust owns load accounting, I/O wait completion, enqueue-flag selection,
/// task activation, preemption, wake publication, and the optional
/// scheduling-class callback.
///
/// # Safety
///
/// `task` must remain live, `rq` must be its current runqueue, `rf` must be
/// the matching lock context, and the caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
#[export]
unsafe extern "C" fn rust_sched_ttwu_activate_locked(
    rq: *mut Rq,
    task: *mut Task,
    wake_flags: c_int,
    rf: *mut bindings::rq_flags,
) {
    // SAFETY: The caller holds the task runqueue lock.
    let plan = activate_plan(
        unsafe { bindings::rust_helper_sched_ttwu_contributes_load(task) },
        unsafe { bindings::rust_helper_sched_ttwu_rq_selected(wake_flags) },
        unsafe { bindings::rust_helper_sched_ttwu_migrated(wake_flags) },
        unsafe { bindings::rust_helper_sched_ttwu_in_iowait(task) },
    );

    if plan & ACTIVATE_DEC_LOAD != 0 {
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { bindings::rust_helper_sched_ttwu_dec_uninterruptible(rq) };
    }
    if plan & ACTIVATE_IO_WAIT != 0 {
        // SAFETY: The task and its current runqueue are locked and live.
        unsafe { bindings::rust_helper_sched_ttwu_io_wait_end(task) };
    }

    // SAFETY: The task is locked and ready to enter its runqueue.
    unsafe {
        bindings::rust_helper_sched_ttwu_activate(
            rq,
            task,
            plan & ACTIVATE_RQ_SELECTED != 0,
            plan & ACTIVATE_MIGRATED != 0,
        )
    };
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { bindings::rust_helper_sched_ttwu_preempt(rq, task, wake_flags) };
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { bindings::rust_helper_sched_ttwu_wake(task) };

    // SAFETY: The task and its scheduling class remain stable under the lock.
    if unsafe { bindings::rust_helper_sched_ttwu_has_task_woken(task) } {
        // SAFETY: The helper performs the class callback's required unpin and
        // repin around the callback.
        unsafe { bindings::rust_helper_sched_ttwu_task_woken(rq, task, rf) };
    }
}
