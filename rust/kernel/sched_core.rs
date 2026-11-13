// SPDX-License-Identifier: GPL-2.0-only

//! Native scheduler core state machines.
//!
//! Rust owns task-state matching, wake activation, and task blocking
//! decisions. C retains task and runqueue locking, warning delivery,
//! scheduling-class queue operations, and task selection.

use crate::bindings;
#[cfg(any(
    CONFIG_RUST_SCHED_WAKE_STATE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_RQ_TRANSITION
))]
use crate::ffi::c_int;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
use crate::ffi::c_ulong;
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_RQ_TRANSITION))]
use crate::sync::atomic::atomic_load;
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
use crate::sync::atomic::atomic_store;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
use crate::sync::atomic::Atomic;
#[cfg(any(
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE,
    CONFIG_RUST_SCHED_RQ_TRANSITION
))]
use crate::sync::atomic::Relaxed;
use macros::export;

type Task = bindings::task_struct;
#[cfg(any(
    CONFIG_RUST_SCHED_BLOCK_TASK,
    CONFIG_RUST_SCHED_RQ_TRANSITION,
    CONFIG_RUST_SCHED_TTWU_ACTIVATE,
    CONFIG_RUST_SCHED_TTWU_RUNNABLE
))]
type Rq = bindings::rq;
#[cfg(CONFIG_RUST_SCHED_TTWU_ACTIVATE)]
type TaskWokenCallback = unsafe extern "C" fn(rq: *mut Rq, task: *mut Task);

#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_RQ_TRANSITION))]
const TASK_ON_RQ_QUEUED: i8 = bindings::RUST_SCHED_TASK_ON_RQ_QUEUED as i8;
#[cfg(CONFIG_RUST_SCHED_RQ_TRANSITION)]
const TASK_ON_RQ_MIGRATING: i8 = bindings::RUST_SCHED_TASK_ON_RQ_MIGRATING as i8;

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
#[cfg(any(CONFIG_RUST_SCHED_TTWU_RUNNABLE, CONFIG_RUST_SCHED_RQ_TRANSITION))]
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
#[cfg(CONFIG_RUST_SCHED_TTWU_RUNNABLE)]
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
    unsafe { bindings::rust_helper_sched_ttwu_update_clock(rq) };

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
        unsafe { bindings::rust_helper_sched_ttwu_preempt(rq, task, wake_flags) };
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
    unsafe { bindings::rust_helper_sched_ttwu_preempt(rq, task, wake_flags) };
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
