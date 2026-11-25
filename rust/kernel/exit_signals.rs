// SPDX-License-Identifier: GPL-2.0

//! Native signal-aware task exit.
//!
//! Rust owns the exit transition order, local queue retirement, shared pending
//! signal retargeting, and group-stop notification selection. C helpers provide
//! the signal-handler lock, static queue operations, cgroup thread-group
//! protection, and parent notification primitives.

use crate::bindings;
use crate::signal_retarget;
use core::mem::MaybeUninit;
use macros::export;

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
use core::sync::atomic::{AtomicPtr, AtomicU32, Ordering};

type SignalSet = bindings::sigset_t;
type Task = bindings::task_struct;

const CLD_STOPPED: i32 = bindings::CLD_STOPPED as i32;
const JOBCTL_STOP_PENDING: usize = bindings::JOBCTL_STOP_PENDING as usize;
const PF_EXITING: u32 = bindings::PF_EXITING;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static TEST_TASK: AtomicPtr<Task> = AtomicPtr::new(core::ptr::null_mut());
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static THREADGROUP_BEGIN_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static THREADGROUP_END_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static PF_EXITING_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static DEQUEUE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static RETARGET_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static GROUP_STOP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
static NOTIFY_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
fn test_tracks(task: *mut Task) -> bool {
    TEST_TASK.load(Ordering::Acquire) == task
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
fn count_event(task: *mut Task, counter: &AtomicU32) {
    if test_tracks(task) {
        counter.fetch_add(1, Ordering::Relaxed);
    }
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_reset() {
    TEST_TASK.store(core::ptr::null_mut(), Ordering::Release);
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    THREADGROUP_BEGIN_COUNT.store(0, Ordering::Relaxed);
    THREADGROUP_END_COUNT.store(0, Ordering::Relaxed);
    PF_EXITING_COUNT.store(0, Ordering::Relaxed);
    DEQUEUE_COUNT.store(0, Ordering::Relaxed);
    RETARGET_COUNT.store(0, Ordering::Relaxed);
    GROUP_STOP_COUNT.store(0, Ordering::Relaxed);
    NOTIFY_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_arm(task: *mut Task) {
    TEST_TASK.store(task, Ordering::Release);
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_threadgroup_begin_count() -> u32 {
    THREADGROUP_BEGIN_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_threadgroup_end_count() -> u32 {
    THREADGROUP_END_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_pf_exiting_count() -> u32 {
    PF_EXITING_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_dequeue_count() -> u32 {
    DEQUEUE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_retarget_count() -> u32 {
    RETARGET_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_group_stop_count() -> u32 {
    GROUP_STOP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_exit_test_notify_count() -> u32 {
    NOTIFY_COUNT.load(Ordering::Relaxed)
}

/// Marks a task as exiting and retires its signal-delivery responsibility.
///
/// # Safety
///
/// `task` must be the current task about to enter the production exit path.
/// Its `sighand` and `signal` structures must be live and pinned by that path.
pub(crate) unsafe fn exit_signals_for_task(task: *mut Task) {
    let mut queue = MaybeUninit::<bindings::list_head>::uninit();
    let queue = queue.as_mut_ptr();

    // SAFETY: The queue storage is writable and remains live for this call.
    unsafe { bindings::rust_helper_signal_exit_init_queue(queue) };

    #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
    count_event(task, &ENTRY_COUNT);

    // SAFETY: The caller guarantees that the task is entering exit.
    unsafe { bindings::rust_helper_signal_exit_threadgroup_change_begin(task) };
    #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
    count_event(task, &THREADGROUP_BEGIN_COUNT);

    // SAFETY: The caller guarantees that the task and its signal structures are live.
    unsafe { bindings::rust_helper_signal_exit_lock(task) };

    // SAFETY: The signal-handler lock protects the task flags.
    unsafe { (*task).flags |= PF_EXITING };
    #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
    count_event(task, &PF_EXITING_COUNT);

    // SAFETY: The signal-handler lock protects the local pending queue.
    unsafe {
        bindings::rust_helper_signal_exit_dequeue_pending(
            core::ptr::addr_of_mut!((*task).pending),
            queue,
        )
    };
    #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
    count_event(task, &DEQUEUE_COUNT);

    // SAFETY: The signal-handler lock is held and the signal structure is live.
    let signal = unsafe { (*task).signal };
    let mut group_stop = 0;

    // SAFETY: The task and signal structures are live under the signal lock.
    let pending = unsafe { bindings::rust_helper_signal_exit_task_sigpending(task) };
    // SAFETY: The task is live under the signal-handler lock.
    let group_empty = unsafe { bindings::rust_helper_signal_exit_thread_group_empty(task) };
    // SAFETY: The signal-handler lock protects the group-exit flags.
    let group_exit = unsafe { (*signal).flags & SIGNAL_GROUP_EXIT != 0 };

    if pending && !group_empty && !group_exit {
        #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
        count_event(task, &RETARGET_COUNT);

        let mut unblocked = MaybeUninit::<SignalSet>::uninit();
        let unblocked = unblocked.as_mut_ptr();
        // SAFETY: The output set is writable and the task is live under the lock.
        unsafe { bindings::rust_helper_signal_exit_copy_unblocked(task, unblocked) };
        // SAFETY: The signal-handler lock is held and the mask remains live.
        unsafe { signal_retarget::retarget_shared_pending(task, unblocked) };

        // SAFETY: The signal-handler lock protects the job-control state.
        let stop_pending = unsafe { (*task).jobctl & JOBCTL_STOP_PENDING != 0 };
        if stop_pending {
            // SAFETY: The signal-handler lock protects group-stop accounting.
            if unsafe { bindings::rust_helper_signal_exit_participate_group_stop(task) } {
                group_stop = CLD_STOPPED;
                #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
                count_event(task, &GROUP_STOP_COUNT);
            }
        }
    }

    // SAFETY: The caller guarantees that the signal-handler lock is held.
    unsafe { bindings::rust_helper_signal_exit_unlock(task) };
    // SAFETY: The matching thread-group protection remains active.
    unsafe { bindings::rust_helper_signal_exit_threadgroup_change_end(task) };
    #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
    count_event(task, &THREADGROUP_END_COUNT);

    // SAFETY: The queue was initialized above and is no longer lock-protected.
    unsafe { bindings::rust_helper_signal_exit_flush_queue(queue) };

    if group_stop != 0 {
        #[cfg(CONFIG_SIGNAL_EXIT_KUNIT_TEST)]
        count_event(task, &NOTIFY_COUNT);
        // SAFETY: The task remains live and group-stop completion was selected.
        unsafe { bindings::rust_helper_signal_exit_notify_group_stop(task, group_stop) };
    }
}

/// Marks a task as exiting and retires its signal-delivery responsibility.
///
/// # Safety
///
/// `task` must be the current task about to enter the production exit path.
#[export]
unsafe extern "C" fn exit_signals(task: *mut Task) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { exit_signals_for_task(task) };
}
