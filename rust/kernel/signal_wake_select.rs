// SPDX-License-Identifier: GPL-2.0

//! Native fatal-signal and ptrace wake state selection.
//!
//! Rust reads and updates the task job-control state, selects the task wake
//! state, and calls the native Rust wakeup state machine.

use crate::bindings;
use crate::signal_wake_state;
use macros::export;

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;

const JOBCTL_PTRACE_FROZEN: usize = bindings::JOBCTL_PTRACE_FROZEN as usize;
const JOBCTL_STOPPED: usize = bindings::JOBCTL_STOPPED as usize;
const JOBCTL_TRACED: usize = bindings::JOBCTL_TRACED as usize;
const TASK_WAKEKILL: u32 = bindings::TASK_WAKEKILL;
const TASK_TRACED: u32 = bindings::__TASK_TRACED;

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static FATAL_ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static FROZEN_SKIP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static FATAL_CLEAR_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static PTRACE_ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static PTRACE_RESUME_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
static LAST_STATE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_reset() {
    FATAL_ENTRY_COUNT.store(0, Ordering::Relaxed);
    FROZEN_SKIP_COUNT.store(0, Ordering::Relaxed);
    FATAL_CLEAR_COUNT.store(0, Ordering::Relaxed);
    PTRACE_ENTRY_COUNT.store(0, Ordering::Relaxed);
    PTRACE_RESUME_COUNT.store(0, Ordering::Relaxed);
    LAST_STATE.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_fatal_entry_count() -> u32 {
    FATAL_ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_frozen_skip_count() -> u32 {
    FROZEN_SKIP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_fatal_clear_count() -> u32 {
    FATAL_CLEAR_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_ptrace_entry_count() -> u32 {
    PTRACE_ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_ptrace_resume_count() -> u32 {
    PTRACE_RESUME_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_select_test_last_state() -> u32 {
    LAST_STATE.load(Ordering::Relaxed)
}

/// Selects and applies the fatal-signal wake state.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn signal_wake_up(task: *mut Task, fatal: bool) {
    #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
    FATAL_ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    let mut state = 0;
    if fatal {
        // SAFETY: The caller guarantees that the task is live under the lock.
        if unsafe { (*task).jobctl & JOBCTL_PTRACE_FROZEN != 0 } {
            #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
            FROZEN_SKIP_COUNT.fetch_add(1, Ordering::Relaxed);
        } else {
            // SAFETY: The caller guarantees that the task is live under the lock.
            unsafe { (*task).jobctl &= !(JOBCTL_STOPPED | JOBCTL_TRACED) };
            state = TASK_WAKEKILL | TASK_TRACED;
            #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
            FATAL_CLEAR_COUNT.fetch_add(1, Ordering::Relaxed);
        }
    }

    #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
    LAST_STATE.store(state, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the task is live under the lock.
    unsafe { signal_wake_state::signal_wake_up_state(task, state) };
}

/// Selects and applies the ptrace wake state.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn ptrace_signal_wake_up(task: *mut Task, resume: bool) {
    #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
    PTRACE_ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    let mut state = 0;
    if resume {
        // SAFETY: The caller guarantees that the task is live under the lock.
        unsafe { (*task).jobctl &= !JOBCTL_TRACED };
        state = TASK_TRACED;
        #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
        PTRACE_RESUME_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    #[cfg(CONFIG_SIGNAL_WAKE_SELECT_KUNIT_TEST)]
    LAST_STATE.store(state, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the task is live under the lock.
    unsafe { signal_wake_state::signal_wake_up_state(task, state) };
}

/// Wakes a task for a fatal or non-fatal signal.
///
/// # Safety
///
/// The requirements of [`signal_wake_up`] apply.
#[export]
unsafe extern "C" fn rust_signal_wake_up(task: *mut Task, fatal: bool) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { signal_wake_up(task, fatal) };
}

/// Wakes a task for ptrace resume or notification.
///
/// # Safety
///
/// The requirements of [`ptrace_signal_wake_up`] apply.
#[export]
unsafe extern "C" fn rust_ptrace_signal_wake_up(task: *mut Task, resume: bool) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { ptrace_signal_wake_up(task, resume) };
}
