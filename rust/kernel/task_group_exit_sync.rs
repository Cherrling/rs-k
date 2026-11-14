// SPDX-License-Identifier: GPL-2.0

//! Native task thread-group exit synchronization.
//!
//! Rust owns quick-thread convergence, group exit-code publication,
//! stop-count clearing, post-coredump marking, and coredump entry selection.
//! C helpers perform the signal-handler lock and active coredump
//! synchronization primitive.

use crate::{bindings, ffi::c_long};
use macros::export;

#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
use core::sync::atomic::{AtomicI32, AtomicU32, Ordering};

type Task = bindings::task_struct;

const PF_POSTCOREDUMP: u32 = bindings::PF_POSTCOREDUMP;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;

#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static GROUP_EXIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static PENDING_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static POST_COREDUMP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static TEST_GROUP_EXIT_FLAGS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static TEST_GROUP_EXIT_CODE: AtomicI32 = AtomicI32::new(0);
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
static TEST_GROUP_STOP_COUNT: AtomicI32 = AtomicI32::new(0);

#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
const TEST_GROUP_EXIT_CODE_VALUE: i32 = bindings::RUST_TASK_GROUP_EXIT_SYNC_KUNIT_CODE as i32;

fn should_start_group_exit(quick_threads: i32, group_exit: bool) -> bool {
    quick_threads == 0 && !group_exit
}

fn should_enter_coredump(core_state_present: bool) -> bool {
    core_state_present
}

/// Tests the production group-exit publication predicate.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_should_start(
    quick_threads: i32,
    group_exit: bool,
) -> bool {
    should_start_group_exit(quick_threads, group_exit)
}

/// Tests the production coredump entry predicate.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_should_enter_coredump(
    core_state_present: bool,
) -> bool {
    should_enter_coredump(core_state_present)
}

/// Returns the number of production synchronization entries observed.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of group-exit publications observed.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_group_exit_count() -> u32 {
    GROUP_EXIT_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of non-final quick-thread exits observed.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_pending_count() -> u32 {
    PENDING_COUNT.load(Ordering::Relaxed)
}

/// Returns the flags published by the marked test thread group.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_last_group_exit_flags() -> u32 {
    TEST_GROUP_EXIT_FLAGS.load(Ordering::Acquire)
}

/// Returns the exit code published by the marked test thread group.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_last_group_exit_code() -> i32 {
    TEST_GROUP_EXIT_CODE.load(Ordering::Acquire)
}

/// Returns the stop count published by the marked test thread group.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_last_group_stop_count() -> i32 {
    TEST_GROUP_STOP_COUNT.load(Ordering::Acquire)
}

/// Returns the number of production post-coredump markings observed.
#[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_group_exit_sync_test_post_coredump_count() -> u32 {
    POST_COREDUMP_COUNT.load(Ordering::Relaxed)
}

/// Synchronizes one exiting task with its thread group.
///
/// # Safety
///
/// `task` must be the current task in the production `do_exit()` sequence,
/// before `exit_signals()` runs. Its `sighand` and `signal` structures must
/// remain live for the complete operation.
unsafe fn synchronize_group_exit(task: *mut Task, code: c_long) {
    #[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the signal structure is live.
    let signal = unsafe { (*task).signal };
    // SAFETY: The caller guarantees that the signal handler is live.
    let sighand = unsafe { bindings::rust_helper_task_group_exit_sync_lock(task) };

    // SAFETY: The signal-handler lock serializes the quick-thread count.
    unsafe { (*signal).quick_threads -= 1 };
    // SAFETY: The signal-handler lock serializes the quick-thread count.
    let quick_threads = unsafe { (*signal).quick_threads };
    // SAFETY: The signal-handler lock serializes the group-exit flags.
    let group_exit = unsafe { (*signal).flags & SIGNAL_GROUP_EXIT != 0 };

    if should_start_group_exit(quick_threads, group_exit) {
        // SAFETY: The signal-handler lock serializes group-exit publication.
        unsafe {
            (*signal).flags = SIGNAL_GROUP_EXIT;
            (*signal).group_exit_code = code as i32;
            (*signal).group_stop_count = 0;
        }
        #[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
        if code as i32 == TEST_GROUP_EXIT_CODE_VALUE {
            // Publish the diagnostic values before the completion counter.
            // SAFETY: The signal-handler lock remains held after publication.
            let (flags, stop_count) = unsafe { ((*signal).flags, (*signal).group_stop_count) };
            TEST_GROUP_EXIT_FLAGS.store(flags, Ordering::Relaxed);
            TEST_GROUP_STOP_COUNT.store(stop_count, Ordering::Relaxed);
            TEST_GROUP_EXIT_CODE.store(code as i32, Ordering::Release);
        }
        #[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
        GROUP_EXIT_COUNT.fetch_add(1, Ordering::Relaxed);
    } else if quick_threads > 0 {
        #[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
        PENDING_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    // SAFETY: This task cannot race with its own exit-state publication.
    unsafe { (*task).flags |= PF_POSTCOREDUMP };
    #[cfg(CONFIG_TASK_GROUP_EXIT_SYNC_KUNIT_TEST)]
    POST_COREDUMP_COUNT.fetch_add(1, Ordering::Relaxed);
    // SAFETY: The signal-handler lock protects the coredump state pointer.
    let core_state = unsafe { (*signal).core_state };
    // SAFETY: This balances the signal-handler lock acquired above.
    unsafe { bindings::rust_helper_task_group_exit_sync_unlock(sighand) };

    if should_enter_coredump(!core_state.is_null()) {
        // SAFETY: The signal lock observed an active coredump. The coredump
        // state remains live until this exiting task synchronizes with it.
        unsafe {
            bindings::rust_helper_task_group_exit_sync_coredump(task, core_state);
        }
    }
}

/// Synchronizes one exiting task with its thread group through the C ABI.
///
/// # Safety
///
/// The same requirements as [`synchronize_group_exit`] apply.
#[export]
unsafe extern "C" fn rust_task_group_exit_sync(task: *mut Task, code: c_long) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { synchronize_group_exit(task, code) };
}
