// SPDX-License-Identifier: GPL-2.0-only

//! Native scheduler core state machines.
//!
//! Rust owns task-state matching, reschedule requests, idle CPU wakeups, wake
//! activation, wake preemption, and task blocking decisions. C retains task
//! and runqueue locking, warning delivery, scheduling-class implementations,
//! and task selection.

use crate::bindings;
#[cfg(any(
    CONFIG_RUST_SCHED_WAKE_STATE,
    CONFIG_RUST_SCHED_WAKE_PREEMPT,
    CONFIG_RUST_SCHED_RESCHED_CURR,
    CONFIG_RUST_SCHED_RESCHED_CPU,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_RQ_TRANSITION
))]
use crate::ffi::c_int;
#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_WAKE_PREEMPT,
    CONFIG_RUST_SCHED_RESCHED_CURR
))]
use crate::ffi::c_ulong;
#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_WAKE_PREEMPT
))]
use crate::sync::atomic::atomic_load;
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
use crate::sync::atomic::atomic_store;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
use crate::sync::atomic::Atomic;
#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_WAKE_PREEMPT
))]
use crate::sync::atomic::Relaxed;
use macros::export;

#[cfg(any(
    CONFIG_RUST_SCHED_WAKE_STATE,
    CONFIG_RUST_SCHED_WAKE_PREEMPT,
    CONFIG_RUST_SCHED_RESCHED_CURR,
    CONFIG_RUST_SCHED_WAKE_IF_IDLE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_BLOCK_TASK,
    CONFIG_RUST_SCHED_RQ_TRANSITION
))]
type Task = bindings::task_struct;
#[cfg(any(
    CONFIG_RUST_SCHED_BLOCK_TASK,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_WAKE_PREEMPT,
    CONFIG_RUST_SCHED_RESCHED_CURR,
    CONFIG_RUST_SCHED_WAKE_IF_IDLE,
    CONFIG_RUST_SCHED_RESCHED_CPU
))]
type Rq = bindings::rq;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
type TaskWokenCallback = unsafe extern "C" fn(rq: *mut Rq, task: *mut Task);
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
type WakeupPreemptCallback = unsafe extern "C" fn(rq: *mut Rq, task: *mut Task, wake_flags: c_int);

#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_WAKE_PREEMPT
))]
const TASK_ON_RQ_QUEUED: i8 = bindings::RUST_SCHED_TASK_ON_RQ_QUEUED as i8;
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
const TASK_ON_RQ_MIGRATING: i8 = bindings::RUST_SCHED_TASK_ON_RQ_MIGRATING as i8;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_CLASS_SAME: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_CLASS_SAME;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_CLASS_ABOVE: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_CLASS_ABOVE;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_CLASS_BELOW: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_CLASS_BELOW;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_CALL: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_CALL;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_RESCHED: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_RESCHED;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_UPDATE_NEXT: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_UPDATE_NEXT;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const PREEMPT_SKIP_CLOCK: u32 = bindings::RUST_SCHED_WAKE_PREEMPT_SKIP_CLOCK;
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
const RQCF_REQ_SKIP: u32 = bindings::RUST_SCHED_RQCF_REQ_SKIP;
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
const RESCHED_LOCAL_SET: u32 = bindings::RUST_SCHED_RESCHED_LOCAL_SET;
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
const RESCHED_LOCAL_PREEMPT: u32 = bindings::RUST_SCHED_RESCHED_LOCAL_PREEMPT;
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
const RESCHED_REMOTE_SET: u32 = bindings::RUST_SCHED_RESCHED_REMOTE_SET;
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
const RESCHED_SEND_IPI: u32 = bindings::RUST_SCHED_RESCHED_SEND_IPI;
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
const RESCHED_WAKE_IDLE: u32 = bindings::RUST_SCHED_RESCHED_WAKE_IDLE;
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
const WAKE_IF_IDLE_LOCK: u32 = bindings::RUST_SCHED_WAKE_IF_IDLE_LOCK;
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
const WAKE_IF_IDLE_RESCHED: u32 = bindings::RUST_SCHED_WAKE_IF_IDLE_RESCHED;

/// Reads the current task wake state.
///
/// # Safety
///
/// `task` must remain live while this function reads its state field.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
unsafe fn task_state(task: *const Task) -> u32 {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the single-read behavior of the C scalar access.
    unsafe { core::ptr::addr_of!((*task).__state).read_volatile() }
}

/// Reads the saved task wake state.
///
/// # Safety
///
/// `task` must remain live while this function reads its saved-state field.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
unsafe fn task_saved_state(task: *const Task) -> u32 {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the single-read behavior of the C scalar access.
    unsafe { core::ptr::addr_of!((*task).saved_state).read_volatile() }
}

/// Publishes the saved task wake state.
///
/// # Safety
///
/// `task` must remain live and the caller must provide the serialization
/// required by the scheduler's saved-state protocol.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
unsafe fn set_task_saved_state(task: *mut Task, state: u32) {
    // SAFETY: The caller guarantees that the task remains live and that the
    // saved-state update is serialized. Use a volatile store so this has the
    // single-store behavior of the C scalar access.
    unsafe { core::ptr::addr_of_mut!((*task).saved_state).write_volatile(state) };
}

/// Reads the scheduler's task runqueue membership state.
///
/// # Safety
///
/// `task` must remain live while this function reads its membership field.
#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_WAKE_PREEMPT
))]
unsafe fn task_on_rq_state(task: *mut Task) -> i8 {
    // SAFETY: The caller guarantees that the task remains live. The field has
    // one-byte alignment, and all other writers use the same atomic access.
    let state = unsafe { core::ptr::addr_of_mut!((*task).on_rq) }.cast::<i8>();
    // SAFETY: The pointer is valid, aligned, and all concurrent accesses are
    // atomic. Relaxed atomic load is the LKMM equivalent of READ_ONCE().
    unsafe { atomic_load(state, Relaxed) }
}

/// Returns whether a task is blocked on another task.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
unsafe fn task_is_blocked(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the single-read behavior of the C scalar access.
    unsafe { core::ptr::addr_of!((*task).is_blocked).read_volatile() != 0 }
}

/// Updates whether a task is blocked.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
unsafe fn set_task_is_blocked(task: *mut Task, blocked: bool) {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // store so this has single-store visibility.
    unsafe { core::ptr::addr_of_mut!((*task).is_blocked).write_volatile(u8::from(blocked)) };
}

/// Publishes the running task state.
///
/// # Safety
///
/// `task` must remain live and the caller must serialize the state update.
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
unsafe fn set_task_running(task: *mut Task) {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // store so this has the single-store behavior of WRITE_ONCE().
    unsafe { core::ptr::addr_of_mut!((*task).__state).write_volatile(bindings::TASK_RUNNING) };
}

/// Returns whether a task is currently running on a CPU.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
unsafe fn task_on_cpu_state(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the single-read behavior of the C scalar access.
    unsafe { core::ptr::addr_of!((*task).on_cpu).read_volatile() != 0 }
}

/// Returns whether a task has delayed scheduler state.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
unsafe fn task_sched_delayed(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the single-read behavior of the C scalar access.
    unsafe { core::ptr::addr_of!((*task).se.sched_delayed).read_volatile() != 0 }
}

/// Publishes the state update for a woken task.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
unsafe fn publish_task_wakeup_state(task: *mut Task) {
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { set_task_is_blocked(task, false) };
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { set_task_running(task) };
}

/// Returns whether a task contributes to uninterruptible load.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn task_contributes_to_load(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. The scheduler
    // serializes this bitfield with the task runqueue lock.
    unsafe { Task::sched_contributes_to_load_raw(task.cast_const()) != 0 }
}

/// Returns whether a task is waiting for I/O.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn task_in_iowait(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. The scheduler
    // serializes this bitfield with the task runqueue lock.
    unsafe { Task::in_iowait_raw(task.cast_const()) != 0 }
}

/// Updates whether a task contributes to uninterruptible load.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_BLOCK_TASK)]
unsafe fn set_task_contributes_to_load(task: *mut Task, contributes_load: bool) {
    // SAFETY: The caller guarantees that the task remains live. The scheduler
    // serializes this bitfield with the task runqueue lock.
    unsafe {
        Task::set_sched_contributes_to_load_raw(task, u32::from(contributes_load));
    }
}

/// Returns whether a task is queued on a runqueue.
///
/// # Safety
///
/// `task` must remain live during the state check.
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_WAKE_PREEMPT))]
unsafe fn task_on_rq_queued(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live.
    unsafe { task_on_rq_state(task) == TASK_ON_RQ_QUEUED }
}

/// Returns whether a task is migrating between runqueues.
///
/// # Safety
///
/// `task` must remain live during the state check.
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
unsafe fn task_on_rq_migrating(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live.
    unsafe { task_on_rq_state(task) == TASK_ON_RQ_MIGRATING }
}

/// Publishes scheduler task runqueue membership state.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
unsafe fn publish_task_on_rq(task: *mut Task, state: i8) {
    // SAFETY: The caller guarantees that the task remains live. The field has
    // one-byte alignment, and all other accesses use the same atomic access.
    let state_ptr = unsafe { core::ptr::addr_of_mut!((*task).on_rq) }.cast::<i8>();
    // SAFETY: The pointer is valid, aligned, and all concurrent accesses are
    // atomic. Relaxed atomic store is the LKMM equivalent of WRITE_ONCE().
    unsafe { atomic_store(state_ptr, state, Relaxed) };
    // SAFETY: The caller holds the runqueue lock and the task remains live.
    unsafe { bindings::rust_helper_sched_rq_assert_exclusive_writer(task) };
}

/// Classifies a task state against a requested wake mask.
#[cfg(CONFIG_RUST_SCHED_WAKE_STATE)]
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
    let task_state = unsafe { task_state(task) };
    // SAFETY: The caller guarantees that the task remains live.
    let saved_state = unsafe { task_saved_state(task) };
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
        unsafe { set_task_saved_state(task, bindings::TASK_RUNNING) };
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
    if !unsafe { task_on_rq_queued(task) } {
        return 0;
    }

    // SAFETY: The caller holds the task runqueue lock.
    unsafe { bindings::update_rq_clock(rq) };

    // SAFETY: The caller holds the task runqueue lock.
    if unsafe { task_is_blocked(task) } {
        // SAFETY: The caller holds the task runqueue lock.
        if unsafe { task_sched_delayed(task) } {
            // SAFETY: The task is queued on this runqueue and the caller holds
            // the runqueue lock.
            unsafe { bindings::enqueue_task(rq, task, ENQUEUE_NOCLOCK | ENQUEUE_DELAYED) };
        }

        // SAFETY: The caller holds the task runqueue lock.
        if unsafe { bindings::rust_helper_sched_ttwu_proxy_return(rq, task) } {
            return 0;
        }
    }

    // SAFETY: The caller holds the task runqueue lock.
    if !unsafe { task_on_cpu_state(task) } {
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { wakeup_preempt(rq, task, wake_flags) };
    }

    // SAFETY: The caller holds the task runqueue lock.
    unsafe { publish_task_wakeup_state(task) };
    // SAFETY: The task state is published and the task remains live.
    unsafe { bindings::rust_helper_sched_ttwu_trace_wakeup(task) };
    1
}

/// Returns the scalar task flags used by the runnable wake path.
///
/// # Safety
///
/// `task` must remain live. The KUnit caller uses an otherwise unused task.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_RUNNABLE))]
#[export]
unsafe extern "C" fn rust_sched_ttwu_task_flags_select(task: *mut Task) -> u32 {
    let mut flags = 0;

    // SAFETY: The caller provides a live task.
    if unsafe { task_is_blocked(task) } {
        flags |= bindings::RUST_SCHED_TTWU_TASK_IS_BLOCKED;
    }
    // SAFETY: The caller provides a live task.
    if unsafe { task_on_cpu_state(task) } {
        flags |= bindings::RUST_SCHED_TTWU_TASK_ON_CPU;
    }
    // SAFETY: The caller provides a live task.
    if unsafe { task_sched_delayed(task) } {
        flags |= bindings::RUST_SCHED_TTWU_TASK_DELAYED;
    }

    flags
}

/// Returns the flags used for delayed runnable enqueue.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_RUNNABLE))]
#[export]
extern "C" fn rust_sched_ttwu_delayed_enqueue_flags_select() -> c_int {
    ENQUEUE_NOCLOCK | ENQUEUE_DELAYED
}

/// Publishes the wake state for the scheduler KUnit test.
///
/// # Safety
///
/// `task` must remain live. The KUnit caller uses an otherwise unused task.
#[cfg(all(
    CONFIG_KUNIT,
    any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE)
))]
#[export]
unsafe extern "C" fn rust_sched_ttwu_wakeup_state_select(task: *mut Task) {
    // SAFETY: The caller provides a live task with exclusive access.
    unsafe { publish_task_wakeup_state(task) };
}

#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_DEC_LOAD: u32 = 0x01;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_RQ_SELECTED: u32 = 0x02;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_MIGRATED: u32 = 0x04;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ACTIVATE_IO_WAIT: u32 = 0x08;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const WF_MIGRATED: c_int = bindings::RUST_SCHED_WF_MIGRATED as c_int;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const WF_RQ_SELECTED: c_int = bindings::RUST_SCHED_WF_RQ_SELECTED as c_int;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ENQUEUE_WAKEUP: c_int = bindings::RUST_SCHED_ENQUEUE_WAKEUP as c_int;
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
const ENQUEUE_NOCLOCK: c_int = bindings::RUST_SCHED_ENQUEUE_NOCLOCK as c_int;
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
const ENQUEUE_DELAYED: c_int = bindings::RUST_SCHED_ENQUEUE_DELAYED as c_int;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
const ENQUEUE_RQ_SELECTED: c_int = bindings::RUST_SCHED_ENQUEUE_RQ_SELECTED as c_int;
#[cfg(any(CONFIG_RUST_SCHED_TTWU_ACTIVATE, CONFIG_RUST_SCHED_RQ_TRANSITION))]
const ENQUEUE_MIGRATED: c_int = bindings::RUST_SCHED_ENQUEUE_MIGRATED as c_int;

/// Returns a pointer to the runqueue's uninterruptible task count.
///
/// # Safety
///
/// `rq` must remain live and point to the start of a C `struct rq`.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn rq_uninterruptible_ptr(rq: *mut Rq) -> *mut c_ulong {
    // SAFETY: The generated offset comes from `offsetof(struct rq,
    // nr_uninterruptible)` and the caller provides the live base pointer.
    unsafe {
        rq.cast::<u8>()
            .add(bindings::RQ_nr_uninterruptible as usize)
            .cast()
    }
}

/// Decrements the runqueue's uninterruptible task count.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn dec_rq_uninterruptible(rq: *mut Rq) {
    // SAFETY: The caller guarantees that the runqueue remains live and locked.
    let count = unsafe { rq_uninterruptible_ptr(rq) };
    // SAFETY: The runqueue lock serializes this read-modify-write sequence.
    let value = unsafe { count.read_volatile() };
    // SAFETY: The runqueue lock remains held and the field is writable.
    unsafe { count.write_volatile(value.wrapping_sub(1)) };
}

/// Returns a pointer to the runqueue's I/O wait count.
///
/// # Safety
///
/// `rq` must remain live and point to the start of a C `struct rq`.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn rq_iowait_ptr(rq: *mut Rq) -> *mut i32 {
    // SAFETY: The generated offset comes from `offsetof(struct rq, nr_iowait)`
    // and the caller provides the live base pointer.
    unsafe { rq.cast::<u8>().add(bindings::RQ_nr_iowait as usize).cast() }
}

/// Decrements the runqueue's I/O wait count.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn dec_rq_iowait(rq: *mut Rq) {
    // SAFETY: The caller guarantees that the runqueue remains live and locked.
    let count = unsafe { rq_iowait_ptr(rq) };
    // SAFETY: The generated offset points to a live `atomic_t`, and all access
    // to the counter is atomic.
    unsafe { Atomic::from_ptr(count) }.fetch_sub(1, Relaxed);
}

/// Returns the task's optional scheduling-class wake callback.
///
/// # Safety
///
/// `task` must remain live and its scheduling class must remain valid.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
unsafe fn task_woken_callback(task: *mut Task) -> Option<TaskWokenCallback> {
    // SAFETY: The caller guarantees that the task and its class are live.
    let class = unsafe { core::ptr::addr_of!((*task).sched_class).read_volatile() };
    // SAFETY: The generated offset comes from
    // `offsetof(struct sched_class, task_woken)`.
    let callback = unsafe {
        class
            .cast::<u8>()
            .add(bindings::SCHED_CLASS_task_woken as usize)
            .cast::<Option<TaskWokenCallback>>()
    };
    // SAFETY: The generated offset points to the optional callback field.
    unsafe { callback.read_volatile() }
}

/// Reads a task pointer from the generated runqueue field offset.
///
/// # Safety
///
/// `rq` must remain live and the offset must name a live task pointer field.
#[cfg(any(
    CONFIG_RUST_SCHED_WAKE_PREEMPT,
    CONFIG_RUST_SCHED_RESCHED_CURR,
    CONFIG_RUST_SCHED_WAKE_IF_IDLE
))]
unsafe fn rq_task_pointer(rq: *mut Rq, offset: u32) -> *mut Task {
    // SAFETY: The generated offset identifies the requested pointer field in
    // the live runqueue. Use a volatile load to match the C field access.
    unsafe {
        rq.cast::<u8>()
            .add(offset as usize)
            .cast::<*mut Task>()
            .read_volatile()
    }
}

/// Reads the current thread-info flags of a task.
///
/// # Safety
///
/// `task` must remain live while this function reads its thread flags.
#[cfg(any(CONFIG_RUST_SCHED_WAKE_PREEMPT, CONFIG_RUST_SCHED_RESCHED_CURR))]
unsafe fn task_thread_flags(task: *mut Task) -> c_ulong {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the read behavior of the C thread-flag check.
    unsafe { core::ptr::addr_of!((*task).thread_info.flags).read_volatile() }
}

/// Reads the selected scheduling class from the runqueue.
///
/// # Safety
///
/// `rq` must remain live and the caller must provide the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn rq_next_class(rq: *mut Rq) -> *mut bindings::sched_class {
    // SAFETY: The generated offset identifies the selected class field in the
    // live runqueue. Use a volatile load to match the C field access.
    unsafe {
        rq.cast::<u8>()
            .add(bindings::RQ_next_class as usize)
            .cast::<*mut bindings::sched_class>()
            .read_volatile()
    }
}

/// Publishes the selected scheduling class on the runqueue.
///
/// # Safety
///
/// `rq` and `class` must remain live and the caller must hold the runqueue
/// lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn set_rq_next_class(rq: *mut Rq, class: *mut bindings::sched_class) {
    // SAFETY: The generated offset identifies the selected class field. The
    // caller holds the runqueue lock during the volatile store.
    unsafe {
        rq.cast::<u8>()
            .add(bindings::RQ_next_class as usize)
            .cast::<*mut bindings::sched_class>()
            .write_volatile(class);
    }
}

/// Reads the scheduling class of a task.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn task_sched_class(task: *mut Task) -> *mut bindings::sched_class {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load to match the C field access.
    unsafe { core::ptr::addr_of!((*task).sched_class).read_volatile() }
}

/// Reads a scheduling class's wakeup-preemption callback.
///
/// # Safety
///
/// `class` must remain live and refer to a complete `struct sched_class`.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn wakeup_preempt_callback(class: *mut bindings::sched_class) -> WakeupPreemptCallback {
    // SAFETY: The generated offset identifies the non-optional callback field.
    unsafe {
        class
            .cast::<u8>()
            .add(bindings::SCHED_CLASS_wakeup_preempt as usize)
            .cast::<WakeupPreemptCallback>()
            .read_volatile()
    }
}

/// Returns whether a task has the reschedule thread flag set.
///
/// # Safety
///
/// `task` must remain live and the caller must hold the task runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn task_need_resched(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live.
    let flags = unsafe { task_thread_flags(task) };
    let mask = (1 as c_ulong) << bindings::TIF_NEED_RESCHED;
    flags & mask != 0
}

/// Requests omission of the next redundant runqueue clock update.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn request_rq_clock_skip(rq: *mut Rq) {
    // SAFETY: The generated offset identifies the clock-update flags. The
    // runqueue lock serializes this read-modify-write sequence.
    let flags = unsafe {
        rq.cast::<u8>()
            .add(bindings::RQ_clock_update_flags as usize)
            .cast::<u32>()
    };
    // SAFETY: The caller holds the runqueue lock and the field is writable.
    unsafe { flags.write_volatile(flags.read_volatile() | RQCF_REQ_SKIP) };
}

/// Reads the CPU that owns a runqueue.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold its lock.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
unsafe fn rq_cpu(rq: *mut Rq) -> c_int {
    // SAFETY: The generated offset identifies the immutable CPU field in the
    // live runqueue.
    unsafe {
        rq.cast::<u8>()
            .add(bindings::RQ_cpu as usize)
            .cast::<c_int>()
            .read_volatile()
    }
}

/// Returns whether a task is an idle task.
///
/// # Safety
///
/// `task` must remain live while this function reads its flags.
#[cfg(any(CONFIG_RUST_SCHED_RESCHED_CURR, CONFIG_RUST_SCHED_WAKE_IF_IDLE))]
unsafe fn task_is_idle(task: *mut Task) -> bool {
    // SAFETY: The caller guarantees that the task remains live. Use a volatile
    // load so this has the read behavior of the C task-flag check.
    let flags = unsafe { core::ptr::addr_of!((*task).flags).read_volatile() };
    flags & bindings::PF_IDLE != 0
}

/// Applies the immediate-reschedule rule for an idle task.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
fn resched_tif(idle: bool, tif: c_int) -> c_int {
    if idle && tif == bindings::TIF_NEED_RESCHED_LAZY as c_int {
        bindings::TIF_NEED_RESCHED as c_int
    } else {
        tif
    }
}

/// Selects local or remote delivery for a non-duplicate reschedule request.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
fn resched_needed(tif: c_int, flags: c_ulong) -> bool {
    let requested = (1 as c_ulong) << (tif as u32);
    let urgent = (1 as c_ulong) << bindings::TIF_NEED_RESCHED;

    flags & (requested | urgent) == 0
}

/// Selects local or remote delivery for a required reschedule request.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
fn resched_target_plan(tif: c_int, local: bool) -> u32 {
    if !local {
        return RESCHED_REMOTE_SET;
    }

    let mut plan = RESCHED_LOCAL_SET;
    if tif == bindings::TIF_NEED_RESCHED as c_int {
        plan |= RESCHED_LOCAL_PREEMPT;
    }
    plan
}

/// Selects the actions for a reschedule request.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RESCHED_CURR))]
fn resched_plan(tif: c_int, flags: c_ulong, local: bool) -> u32 {
    if !resched_needed(tif, flags) {
        0
    } else {
        resched_target_plan(tif, local)
    }
}

/// Selects the action after an atomic remote thread-flag update.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
fn resched_remote_plan(atomic_set: bool, tif: c_int) -> u32 {
    if !atomic_set {
        RESCHED_WAKE_IDLE
    } else if tif == bindings::TIF_NEED_RESCHED as c_int {
        RESCHED_SEND_IPI
    } else {
        0
    }
}

/// Implements a runqueue-locked current-task reschedule request.
///
/// # Safety
///
/// `rq` and its current task must remain live. The caller must hold the
/// runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
unsafe fn resched_curr_locked(rq: *mut Rq, tif: c_int) {
    // SAFETY: The caller holds the runqueue lock.
    let curr = unsafe { rq_task_pointer(rq, bindings::RQ_curr) };
    // SAFETY: The caller holds the runqueue lock.
    let idle = unsafe { task_is_idle(curr) };
    let tif = resched_tif(idle, tif);
    // SAFETY: The caller holds the runqueue lock.
    let flags = unsafe { task_thread_flags(curr) };
    if !resched_needed(tif, flags) {
        return;
    }
    // SAFETY: The caller holds the runqueue lock.
    let cpu = unsafe { rq_cpu(rq) };

    // SAFETY: The task remains live under the runqueue lock.
    unsafe { bindings::rust_helper_sched_resched_trace(curr, cpu, tif) };

    // SAFETY: The runqueue lock disables migration.
    let current_cpu = unsafe { bindings::rust_helper_sched_resched_current_cpu() };
    let plan = resched_target_plan(tif, cpu == current_cpu);

    if plan & RESCHED_LOCAL_SET != 0 {
        // SAFETY: The task remains live and the caller holds the lock.
        unsafe { bindings::rust_helper_sched_resched_set_local(curr, tif) };
        if plan & RESCHED_LOCAL_PREEMPT != 0 {
            // SAFETY: The runqueue lock disables migration and preemption.
            unsafe { bindings::rust_helper_sched_resched_set_preempt() };
        }
        return;
    }

    // SAFETY: The task remains live under the runqueue lock.
    let atomic_set = unsafe { bindings::rust_helper_sched_resched_set_remote(curr, tif) };
    match resched_remote_plan(atomic_set, tif) {
        RESCHED_SEND_IPI => {
            // SAFETY: The target CPU owns the runqueue selected by the caller.
            unsafe { bindings::rust_helper_sched_resched_send_ipi(cpu) };
        }
        RESCHED_WAKE_IDLE => {
            // SAFETY: The target CPU owns the polling idle task.
            unsafe { bindings::rust_helper_sched_resched_trace_wake_idle(cpu) };
        }
        _ => {}
    }
}

/// Implements the production current-task reschedule request entry point.
///
/// # Safety
///
/// `rq` and its current task must remain live. The caller must hold the
/// runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CURR)]
#[export]
unsafe extern "C" fn rust_sched_resched_curr(rq: *mut Rq, tif: c_int) {
    // SAFETY: The caller satisfies the exported entry-point contract.
    unsafe { resched_curr_locked(rq, tif) };
}

/// Selects the effective reschedule thread flag for a task.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RESCHED_CURR))]
#[export]
extern "C" fn rust_sched_resched_tif_select(idle: bool, tif: c_int) -> c_int {
    resched_tif(idle, tif)
}

/// Selects local or remote delivery for the reschedule KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RESCHED_CURR))]
#[export]
extern "C" fn rust_sched_resched_plan_select(tif: c_int, flags: c_ulong, local: bool) -> u32 {
    resched_plan(tif, flags, local)
}

/// Selects the remote reschedule action for the KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RESCHED_CURR))]
#[export]
extern "C" fn rust_sched_resched_remote_select(atomic_set: bool, tif: c_int) -> u32 {
    resched_remote_plan(atomic_set, tif)
}

/// Returns whether a per-CPU reschedule request is required.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CPU)]
fn resched_cpu_needed(cpu_online: bool, cpu_local: bool) -> bool {
    cpu_online || cpu_local
}

/// Implements a runqueue-locked per-CPU reschedule request.
///
/// # Safety
///
/// `rq` must remain live, must belong to `cpu`, and the caller must hold its
/// runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CPU)]
unsafe fn resched_cpu_locked(rq: *mut Rq, cpu: c_int) {
    // SAFETY: The caller holds the target runqueue lock.
    let cpu_online = unsafe { bindings::rust_helper_sched_resched_cpu_online(cpu) };
    // Preserve the C short-circuit: query the current CPU only for an offline
    // target.
    // SAFETY: The runqueue lock disables migration and IRQs on this CPU.
    let cpu_local =
        !cpu_online && cpu == unsafe { bindings::rust_helper_sched_resched_cpu_current() };

    if resched_cpu_needed(cpu_online, cpu_local) {
        // SAFETY: The caller holds the target runqueue lock.
        unsafe { bindings::resched_curr(rq) };
    }
}

/// Implements the production per-CPU reschedule request entry point.
///
/// # Safety
///
/// `rq` must remain live, must belong to `cpu`, and the caller must hold its
/// runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RESCHED_CPU)]
#[export]
unsafe extern "C" fn rust_sched_resched_cpu(rq: *mut Rq, cpu: c_int) {
    // SAFETY: The caller satisfies the exported entry-point contract.
    unsafe { resched_cpu_locked(rq, cpu) };
}

/// Selects whether a per-CPU reschedule request is required for KUnit.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RESCHED_CPU))]
#[export]
extern "C" fn rust_sched_resched_cpu_select(cpu_online: bool, cpu_local: bool) -> bool {
    resched_cpu_needed(cpu_online, cpu_local)
}

/// Selects whether an idle CPU wakeup must lock and request rescheduling.
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
fn wake_if_idle_plan(curr_idle: bool, locked_idle: bool) -> u32 {
    if !curr_idle {
        return 0;
    }

    let mut plan = WAKE_IF_IDLE_LOCK;
    if locked_idle {
        plan |= WAKE_IF_IDLE_RESCHED;
    }
    plan
}

/// Checks whether the first idle-CPU wakeup gate requires the runqueue lock.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold the RCU read lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
unsafe fn wake_if_idle_needs_lock(rq: *mut Rq) -> bool {
    // SAFETY: The caller holds the RCU read lock and the helper performs the
    // required RCU dereference of the current-task pointer.
    let curr = unsafe { bindings::rust_helper_sched_wake_if_idle_curr(rq) };
    // SAFETY: RCU protects the current task from reclamation until it is
    // checked.
    let curr_idle = unsafe { task_is_idle(curr) };
    wake_if_idle_plan(curr_idle, false) & WAKE_IF_IDLE_LOCK != 0
}

/// Requests rescheduling if the locked recheck still observes an idle task.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold its runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
unsafe fn wake_if_idle_locked(rq: *mut Rq) {
    // SAFETY: The caller holds the runqueue lock.
    let curr = unsafe { rq_task_pointer(rq, bindings::RQ_curr) };
    // SAFETY: The caller holds the runqueue lock.
    let locked_idle = unsafe { task_is_idle(curr) };
    if wake_if_idle_plan(true, locked_idle) & WAKE_IF_IDLE_RESCHED != 0 {
        // SAFETY: The caller holds the runqueue lock.
        unsafe { bindings::resched_curr(rq) };
    }
}

/// Runs the initial idle-CPU wakeup gate.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold the RCU read lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
#[export]
unsafe extern "C" fn rust_sched_wake_if_idle_needs_lock(rq: *mut Rq) -> bool {
    // SAFETY: The caller satisfies the exported entry-point contract.
    unsafe { wake_if_idle_needs_lock(rq) }
}

/// Runs the runqueue-locked idle-CPU wakeup recheck.
///
/// # Safety
///
/// `rq` must remain live and the caller must hold its runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_IF_IDLE)]
#[export]
unsafe extern "C" fn rust_sched_wake_if_idle_locked(rq: *mut Rq) {
    // SAFETY: The caller satisfies the exported entry-point contract.
    unsafe { wake_if_idle_locked(rq) };
}

/// Selects the idle-CPU wakeup actions for KUnit.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_WAKE_IF_IDLE))]
#[export]
extern "C" fn rust_sched_wake_if_idle_select(curr_idle: bool, locked_idle: bool) -> u32 {
    wake_if_idle_plan(curr_idle, locked_idle)
}

/// Classifies the task scheduling class relative to the selected class.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
fn preempt_class_relation(
    task_class: *mut bindings::sched_class,
    next_class: *mut bindings::sched_class,
) -> u32 {
    if task_class == next_class {
        PREEMPT_CLASS_SAME
    } else if (task_class as usize) < (next_class as usize) {
        PREEMPT_CLASS_ABOVE
    } else {
        PREEMPT_CLASS_BELOW
    }
}

/// Selects the actions for a wakeup-preemption event.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
fn wakeup_preempt_plan(class_relation: u32, donor_queued: bool, need_resched: bool) -> u32 {
    let mut plan = match class_relation {
        PREEMPT_CLASS_SAME => PREEMPT_CALL,
        PREEMPT_CLASS_ABOVE => PREEMPT_CALL | PREEMPT_RESCHED | PREEMPT_UPDATE_NEXT,
        _ => 0,
    };

    if donor_queued && need_resched {
        plan |= PREEMPT_SKIP_CLOCK;
    }

    plan
}

/// Implements the runqueue-locked wakeup-preemption state machine.
///
/// # Safety
///
/// `rq`, `task`, and all scheduling classes involved must remain live. The
/// caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
unsafe fn wakeup_preempt_locked(rq: *mut Rq, task: *mut Task, wake_flags: c_int) {
    // SAFETY: The caller holds the runqueue lock.
    let donor = unsafe { rq_task_pointer(rq, bindings::RQ_donor) };
    // SAFETY: The caller holds the runqueue lock.
    let task_class = unsafe { task_sched_class(task) };
    // SAFETY: The caller holds the runqueue lock.
    let next_class = unsafe { rq_next_class(rq) };
    let relation = preempt_class_relation(task_class, next_class);
    let class_plan = wakeup_preempt_plan(relation, false, false);

    if class_plan & PREEMPT_CALL != 0 {
        // SAFETY: The selected class and task remain live under the lock.
        let callback = unsafe { wakeup_preempt_callback(next_class) };
        // SAFETY: The callback uses the runqueue lock contract of the caller.
        unsafe { callback(rq, task, wake_flags) };
    }
    if class_plan & PREEMPT_RESCHED != 0 {
        // SAFETY: The caller holds the runqueue lock.
        unsafe { bindings::resched_curr(rq) };
    }
    if class_plan & PREEMPT_UPDATE_NEXT != 0 {
        // SAFETY: The task class remains live and the caller holds the lock.
        unsafe { set_rq_next_class(rq, task_class) };
    }

    // SAFETY: The caller holds the runqueue lock.
    let donor_queued = unsafe { task_on_rq_queued(donor) };
    // SAFETY: The caller holds the runqueue lock.
    let curr = unsafe { rq_task_pointer(rq, bindings::RQ_curr) };
    // SAFETY: The caller holds the runqueue lock.
    let need_resched = unsafe { task_need_resched(curr) };
    if wakeup_preempt_plan(relation, donor_queued, need_resched) & PREEMPT_SKIP_CLOCK != 0 {
        // SAFETY: The caller holds the runqueue lock.
        unsafe { request_rq_clock_skip(rq) };
    }
}

/// Implements the production wakeup-preemption entry point.
///
/// # Safety
///
/// `rq`, `task`, and all scheduling classes involved must remain live. The
/// caller must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
#[export]
unsafe extern "C" fn rust_sched_wakeup_preempt(rq: *mut Rq, task: *mut Task, wake_flags: c_int) {
    // SAFETY: The caller satisfies the exported entry-point contract.
    unsafe { wakeup_preempt_locked(rq, task, wake_flags) };
}

/// Selects the wakeup-preemption actions for a class relation and clock state.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_WAKE_PREEMPT))]
#[export]
extern "C" fn rust_sched_wakeup_preempt_select(
    class_relation: u32,
    donor_queued: bool,
    need_resched: bool,
) -> u32 {
    wakeup_preempt_plan(class_relation, donor_queued, need_resched)
}

/// Calls the production wakeup-preemption implementation.
///
/// # Safety
///
/// The caller must hold the runqueue lock and satisfy `wakeup_preempt()`.
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
unsafe fn wakeup_preempt(rq: *mut Rq, task: *mut Task, wake_flags: c_int) {
    #[cfg(CONFIG_RUST_SCHED_WAKE_PREEMPT)]
    // SAFETY: The caller provides the runqueue lock contract.
    unsafe {
        wakeup_preempt_locked(rq, task, wake_flags);
    }

    #[cfg(not(CONFIG_RUST_SCHED_WAKE_PREEMPT))]
    // SAFETY: The caller provides the runqueue lock contract.
    unsafe {
        bindings::wakeup_preempt(rq, task, wake_flags);
    }
}

/// Constructs the enqueue flags for a woke task.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
fn activation_flags(rq_selected: bool, migrated: bool) -> c_int {
    let mut flags = ENQUEUE_WAKEUP | ENQUEUE_NOCLOCK;

    if rq_selected {
        flags |= ENQUEUE_RQ_SELECTED;
    }
    if migrated {
        flags |= ENQUEUE_MIGRATED;
    }

    flags
}

/// Decodes the wake flags used by the activation state machine.
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
fn decode_wake_flags(wake_flags: c_int) -> u32 {
    let mut flags = 0;

    if wake_flags & WF_RQ_SELECTED != 0 {
        flags |= ACTIVATE_RQ_SELECTED;
    }
    if wake_flags & WF_MIGRATED != 0 {
        flags |= ACTIVATE_MIGRATED;
    }

    flags
}

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

/// Returns the task flags used by woke-task activation.
///
/// # Safety
///
/// `task` must remain live. The KUnit caller uses an otherwise unused task.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
unsafe extern "C" fn rust_sched_ttwu_task_activate_flags_select(task: *mut Task) -> u32 {
    let mut flags = 0;

    // SAFETY: The caller provides a live task.
    if unsafe { task_contributes_to_load(task) } {
        flags |= ACTIVATE_DEC_LOAD;
    }
    // SAFETY: The caller provides a live task.
    if unsafe { task_in_iowait(task) } {
        flags |= ACTIVATE_IO_WAIT;
    }

    flags
}

/// Returns the decoded scheduler wake flags for the KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
extern "C" fn rust_sched_ttwu_wake_flags_select(wake_flags: c_int) -> u32 {
    decode_wake_flags(wake_flags)
}

/// Returns the activation enqueue flags for the KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
extern "C" fn rust_sched_ttwu_activate_flags_select(rq_selected: bool, migrated: bool) -> c_int {
    activation_flags(rq_selected, migrated)
}

/// Decrements a runqueue load counter for the scheduler KUnit test.
///
/// Returns the counter value after the decrement.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
extern "C" fn rust_sched_ttwu_dec_uninterruptible_select(initial: c_ulong) -> c_ulong {
    const OFFSET: usize = bindings::RQ_nr_uninterruptible as usize;
    const WORDS: usize =
        (OFFSET + core::mem::size_of::<c_ulong>()).div_ceil(core::mem::size_of::<u64>());
    let mut storage = [0_u64; WORDS];
    let rq = storage.as_mut_ptr().cast::<Rq>();
    // SAFETY: The storage contains the generated counter offset.
    let count = unsafe { rq_uninterruptible_ptr(rq) };

    // SAFETY: The aligned storage is valid for a raw write and the helper
    // accesses only the initialized counter field.
    unsafe { count.write(initial) };
    // SAFETY: The local storage remains valid and has exclusive access.
    unsafe { dec_rq_uninterruptible(rq) };
    // SAFETY: The helper initialized and updated the counter field.
    unsafe { count.read_volatile() }
}

/// Decrements a runqueue I/O wait counter for the scheduler KUnit test.
///
/// Returns the counter value after the decrement.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_TTWU_ACTIVATE))]
#[export]
extern "C" fn rust_sched_ttwu_dec_iowait_select(initial: c_int) -> c_int {
    const OFFSET: usize = bindings::RQ_nr_iowait as usize;
    const WORDS: usize =
        (OFFSET + core::mem::size_of::<c_int>()).div_ceil(core::mem::size_of::<u64>());
    let mut storage = [0_u64; WORDS];
    let rq = storage.as_mut_ptr().cast::<Rq>();
    // SAFETY: The storage contains the generated counter offset.
    let count = unsafe { rq_iowait_ptr(rq) };

    // SAFETY: The aligned storage contains the initialized atomic counter.
    unsafe { Atomic::from_ptr(count) }.store(initial, Relaxed);
    // SAFETY: The local storage remains valid and has exclusive access.
    unsafe { dec_rq_iowait(rq) };
    // SAFETY: The helper initialized and updated the atomic counter.
    unsafe { Atomic::from_ptr(count) }.load(Relaxed)
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
    let decoded_wake_flags = decode_wake_flags(wake_flags);
    let plan = activate_plan(
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { task_contributes_to_load(task) },
        decoded_wake_flags & ACTIVATE_RQ_SELECTED != 0,
        decoded_wake_flags & ACTIVATE_MIGRATED != 0,
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { task_in_iowait(task) },
    );

    if plan & ACTIVATE_DEC_LOAD != 0 {
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { dec_rq_uninterruptible(rq) };
    }
    if plan & ACTIVATE_IO_WAIT != 0 {
        // SAFETY: The task and its current runqueue are locked and live.
        unsafe { bindings::rust_helper_sched_ttwu_delayacct_blkio_end(task) };
        // SAFETY: The caller holds the task runqueue lock.
        unsafe { dec_rq_iowait(rq) };
    }

    let enqueue_flags = activation_flags(
        plan & ACTIVATE_RQ_SELECTED != 0,
        plan & ACTIVATE_MIGRATED != 0,
    );
    // SAFETY: The task is locked and ready to enter its runqueue.
    unsafe { bindings::activate_task(rq, task, enqueue_flags) };
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { wakeup_preempt(rq, task, wake_flags) };
    // SAFETY: The caller holds the task runqueue lock.
    unsafe { publish_task_wakeup_state(task) };
    // SAFETY: The task state is published and the task remains live.
    unsafe { bindings::rust_helper_sched_ttwu_trace_wakeup(task) };

    // SAFETY: The task and its scheduling class remain stable under the lock.
    if let Some(callback) = unsafe { task_woken_callback(task) } {
        // SAFETY: The callback lock contract requires a matching unpin and
        // repin around the class callback.
        unsafe { bindings::rust_helper_sched_rq_unpin_lock(rq, rf) };
        // SAFETY: The task is fully awake and both pointers remain valid for
        // the duration of the scheduling-class callback.
        unsafe { callback(rq, task) };
        // SAFETY: The callback returned and the runqueue remains live.
        unsafe { bindings::rust_helper_sched_rq_repin_lock(rq, rf) };
    }
}

#[cfg(CONFIG_RUST_SCHED_BLOCK_TASK)]
const BLOCK_CONTRIBUTES_LOAD: u32 = 0x01;
#[cfg(CONFIG_RUST_SCHED_BLOCK_TASK)]
const BLOCK_SPECIAL: u32 = 0x02;

/// Selects the state-changing actions for a task that is about to block.
#[cfg(CONFIG_RUST_SCHED_BLOCK_TASK)]
fn block_plan(task_state: u32) -> u32 {
    let special = bindings::__TASK_STOPPED
        | bindings::__TASK_TRACED
        | bindings::TASK_PARKED
        | bindings::TASK_DEAD
        | bindings::TASK_WAKING
        | bindings::TASK_FROZEN;
    let mut plan = 0;

    if task_state & bindings::TASK_UNINTERRUPTIBLE != 0
        && task_state & bindings::TASK_NOLOAD == 0
        && task_state & bindings::TASK_FROZEN == 0
    {
        plan |= BLOCK_CONTRIBUTES_LOAD;
    }
    if task_state & special != 0 {
        plan |= BLOCK_SPECIAL;
    }

    plan
}

/// Returns the blocking action bits for the scheduler KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_BLOCK_TASK))]
#[export]
extern "C" fn rust_sched_block_task_select(task_state: u32) -> u32 {
    block_plan(task_state)
}

/// Updates the contributes-to-load flag for the scheduler KUnit test.
///
/// # Safety
///
/// `task` must remain live. The KUnit caller uses an otherwise unused task.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_BLOCK_TASK))]
#[export]
unsafe extern "C" fn rust_sched_block_task_set_contributes_load_select(
    task: *mut Task,
    contributes_load: bool,
) {
    // SAFETY: The caller provides a live task and exclusive field access.
    unsafe { set_task_contributes_to_load(task, contributes_load) };
}

/// Blocks a task while its runqueue lock remains held.
///
/// Rust owns load and special-state classification, the contributes-to-load
/// transition, dequeue-flag selection, and the ordering of dequeue and block
/// accounting.
///
/// # Safety
///
/// `task` must remain live, `rq` must be its current runqueue, and the caller
/// must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_BLOCK_TASK)]
#[export]
unsafe extern "C" fn rust_sched_block_task_locked(rq: *mut Rq, task: *mut Task, task_state: u32) {
    let plan = block_plan(task_state);

    // SAFETY: The caller holds the task runqueue lock.
    unsafe { set_task_contributes_to_load(task, plan & BLOCK_CONTRIBUTES_LOAD != 0) };
    // SAFETY: The task is queued on the caller's locked runqueue.
    if unsafe { bindings::rust_helper_sched_block_dequeue(rq, task, plan & BLOCK_SPECIAL != 0) } {
        // SAFETY: Dequeue succeeded and the runqueue lock remains held.
        unsafe { bindings::rust_helper_sched_block_finish(rq, task) };
    }
}

#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
const RQ_ADD_MIGRATED: u32 = 0x01;
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
const RQ_WARN_DEQUEUE_SLEEP: u32 = 0x02;

#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
const DEQUEUE_SLEEP: c_int = bindings::RUST_SCHED_DEQUEUE_SLEEP as c_int;

/// Selects the actions for a task runqueue activation or deactivation.
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
fn rq_transition_plan(migrating: bool, deactivate_flags: c_int) -> u32 {
    let mut plan = 0;

    if migrating {
        plan |= RQ_ADD_MIGRATED;
    }
    if deactivate_flags & DEQUEUE_SLEEP != 0 {
        plan |= RQ_WARN_DEQUEUE_SLEEP;
    }

    plan
}

/// Returns the runqueue transition action bits for the scheduler KUnit table.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_SCHED_RQ_TRANSITION))]
#[export]
extern "C" fn rust_sched_rq_transition_select(migrating: bool, deactivate_flags: c_int) -> u32 {
    rq_transition_plan(migrating, deactivate_flags)
}

/// Activates a task while its runqueue lock remains held.
///
/// Rust owns migrated-enqueue selection, queue insertion, and queued-state
/// publication order.
///
/// # Safety
///
/// `task` must remain live, `rq` must be its current runqueue, and the caller
/// must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
#[export]
unsafe extern "C" fn rust_sched_activate_task_locked(rq: *mut Rq, task: *mut Task, flags: c_int) {
    // SAFETY: The caller holds the task runqueue lock.
    let migrating = unsafe { task_on_rq_migrating(task) };
    let plan = rq_transition_plan(migrating, 0);
    let flags = if plan & RQ_ADD_MIGRATED != 0 {
        flags | ENQUEUE_MIGRATED
    } else {
        flags
    };

    // SAFETY: The task is entering the caller's locked runqueue.
    unsafe { bindings::enqueue_task(rq, task, flags) };
    // SAFETY: The caller holds the runqueue lock and the task is live.
    unsafe { publish_task_on_rq(task, TASK_ON_RQ_QUEUED) };
}

/// Deactivates a task while its runqueue lock remains held.
///
/// Rust owns sleeping-dequeue validation and the order of migrating-state
/// publication and dequeue.
///
/// # Safety
///
/// `task` must remain live, `rq` must be its current runqueue, and the caller
/// must hold the runqueue lock.
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
#[export]
unsafe extern "C" fn rust_sched_deactivate_task_locked(rq: *mut Rq, task: *mut Task, flags: c_int) {
    let plan = rq_transition_plan(false, flags);

    if plan & RQ_WARN_DEQUEUE_SLEEP != 0 {
        // SAFETY: The helper emits the existing scheduler warning.
        unsafe { bindings::rust_helper_sched_rq_warn_dequeue_sleep() };
    }

    // SAFETY: The caller holds the runqueue lock and the task is live.
    unsafe { publish_task_on_rq(task, TASK_ON_RQ_MIGRATING) };
    // SAFETY: The task runqueue lock remains held.
    unsafe { bindings::dequeue_task(rq, task, flags) };
}
