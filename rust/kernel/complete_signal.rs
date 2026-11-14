// SPDX-License-Identifier: GPL-2.0

//! Native complete-signal state machine.
//!
//! Rust selects the notification target, balances the thread-group target,
//! prepares fatal group exit state, and selects the final wakeup.

use crate::bindings;
use crate::signal_wake_select;
use macros::export;

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;

const PF_EXITING: u32 = bindings::PF_EXITING;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;
const PIDTYPE_PID: u32 = bindings::pid_type_PIDTYPE_PID;
const JOBCTL_PENDING_MASK: usize = bindings::JOBCTL_PENDING_MASK as usize;
const SIGKILL: i32 = bindings::SIGKILL as i32;

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static SUGGESTED_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static SINGLE_RETURN_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static SCAN_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static NO_CANDIDATE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static TARGET_UPDATE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static GROUP_EXIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static COREDUMP_SKIP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
static FINAL_WAKE_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_reset() {
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    SUGGESTED_COUNT.store(0, Ordering::Relaxed);
    SINGLE_RETURN_COUNT.store(0, Ordering::Relaxed);
    SCAN_COUNT.store(0, Ordering::Relaxed);
    NO_CANDIDATE_COUNT.store(0, Ordering::Relaxed);
    TARGET_UPDATE_COUNT.store(0, Ordering::Relaxed);
    GROUP_EXIT_COUNT.store(0, Ordering::Relaxed);
    COREDUMP_SKIP_COUNT.store(0, Ordering::Relaxed);
    FINAL_WAKE_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_suggested_count() -> u32 {
    SUGGESTED_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_single_return_count() -> u32 {
    SINGLE_RETURN_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_scan_count() -> u32 {
    SCAN_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_no_candidate_count() -> u32 {
    NO_CANDIDATE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_target_update_count() -> u32 {
    TARGET_UPDATE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_group_exit_count() -> u32 {
    GROUP_EXIT_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_coredump_skip_count() -> u32 {
    COREDUMP_SKIP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_complete_signal_test_final_wake_count() -> u32 {
    FINAL_WAKE_COUNT.load(Ordering::Relaxed)
}

/// Returns whether `task` is willing to handle `sig`.
///
/// # Safety
///
/// `task` must be live and the caller must hold its signal-handler lock.
unsafe fn wants_signal(sig: i32, task: *mut Task) -> bool {
    // SAFETY: The task and its blocked set are live under the signal lock.
    if unsafe { bindings::rust_helper_complete_signal_member(&(*task).blocked, sig) } {
        return false;
    }

    // SAFETY: The task is live under the signal lock.
    if unsafe { (*task).flags & PF_EXITING != 0 } {
        return false;
    }

    if sig == SIGKILL {
        return true;
    }

    // SAFETY: The task is live under the signal lock.
    if unsafe { bindings::rust_helper_complete_signal_stopped_or_traced(task) } {
        return false;
    }

    // SAFETY: The task is live under the signal lock.
    unsafe {
        bindings::rust_helper_complete_signal_task_curr(task)
            || !bindings::rust_helper_complete_signal_task_pending(task)
    }
}

/// Completes signal delivery by selecting a task and wake policy.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn complete_signal(sig: i32, task: *mut Task, pid_type: u32) {
    #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The signal structure is live under the signal-handler lock.
    let signal = unsafe { (*task).signal };
    let target;

    // SAFETY: The task is live under the signal-handler lock.
    if unsafe { wants_signal(sig, task) } {
        target = task;
        #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
        SUGGESTED_COUNT.fetch_add(1, Ordering::Relaxed);
    } else {
        // SAFETY: The task is live under the signal-handler lock.
        let single = pid_type == PIDTYPE_PID
            || unsafe { bindings::rust_helper_complete_signal_thread_group_empty(task) };
        if single {
            #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
            SINGLE_RETURN_COUNT.fetch_add(1, Ordering::Relaxed);
            return;
        }

        #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
        SCAN_COUNT.fetch_add(1, Ordering::Relaxed);
        // SAFETY: curr_target is valid under the signal-handler lock.
        let start = unsafe { (*signal).curr_target };
        let mut candidate = start;

        loop {
            // SAFETY: The candidate is live under the signal-handler lock.
            if unsafe { wants_signal(sig, candidate) } {
                target = candidate;
                // SAFETY: The signal structure is live under the lock.
                unsafe { (*signal).curr_target = candidate };
                #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
                TARGET_UPDATE_COUNT.fetch_add(1, Ordering::Relaxed);
                break;
            }

            // SAFETY: The thread list is stable under the signal-handler lock.
            candidate = unsafe { bindings::rust_helper_complete_signal_next_thread(candidate) };
            if candidate == start {
                #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
                NO_CANDIDATE_COUNT.fetch_add(1, Ordering::Relaxed);
                return;
            }
        }
    }

    // SAFETY: The selected task and its signal state are live under the lock.
    let fatal = unsafe { bindings::rust_helper_complete_signal_fatal(task, sig) };
    // SAFETY: The selected task is live under the lock.
    let real_blocked =
        unsafe { bindings::rust_helper_complete_signal_member(&(*target).real_blocked, sig) };
    // SAFETY: The original task is live under the lock.
    let ptraced = unsafe { (*task).ptrace != 0 };

    if fatal && !real_blocked && (sig == SIGKILL || !ptraced) {
        // SAFETY: Signal classification does not dereference user state.
        if !unsafe { bindings::rust_helper_complete_signal_kernel_coredump(sig) } {
            // SAFETY: The signal structure is live under the lock.
            unsafe {
                (*signal).flags = SIGNAL_GROUP_EXIT;
                (*signal).group_exit_code = sig;
                (*signal).group_stop_count = 0;
            }

            // SAFETY: The group thread list is stable under the signal lock.
            let mut thread = unsafe { bindings::rust_helper_complete_signal_first_thread(signal) };
            while !thread.is_null() {
                // SAFETY: The thread is live under the signal-handler lock.
                unsafe {
                    bindings::rust_helper_complete_signal_clear_jobctl(thread, JOBCTL_PENDING_MASK);
                    bindings::rust_helper_complete_signal_add_pending(
                        &mut (*thread).pending.signal,
                        SIGKILL,
                    );
                    signal_wake_select::signal_wake_up(thread, true);
                }
                // SAFETY: The group thread list is stable under the signal lock.
                thread = unsafe { bindings::rust_helper_complete_signal_next_group_thread(thread) };
            }

            #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
            GROUP_EXIT_COUNT.fetch_add(1, Ordering::Relaxed);
            return;
        }

        #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
        COREDUMP_SKIP_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    // SAFETY: The selected task remains live under the signal-handler lock.
    unsafe { signal_wake_select::signal_wake_up(target, sig == SIGKILL) };
    #[cfg(CONFIG_COMPLETE_SIGNAL_KUNIT_TEST)]
    FINAL_WAKE_COUNT.fetch_add(1, Ordering::Relaxed);
}

/// Completes signal delivery by selecting a task and wake policy.
///
/// # Safety
///
/// The requirements of [`complete_signal`] apply.
#[export]
unsafe extern "C" fn rust_complete_signal(sig: i32, task: *mut Task, pid_type: u32) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { complete_signal(sig, task, pid_type) };
}
