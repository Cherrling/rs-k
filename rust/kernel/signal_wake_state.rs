// SPDX-License-Identifier: GPL-2.0

//! Native signal wakeup state machine.
//!
//! Rust sets the pending flag, builds the scheduler wake mask, attempts the
//! wakeup, and selects the process-kick fallback. C helpers provide the
//! thread-flag operation, scheduler wakeup, and process kick primitives.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;

const TASK_INTERRUPTIBLE: u32 = bindings::TASK_INTERRUPTIBLE;

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static SET_PENDING_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static WAKE_ATTEMPT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static WAKE_SUCCESS_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static KICK_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
static LAST_WAKE_STATE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_reset() {
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    SET_PENDING_COUNT.store(0, Ordering::Relaxed);
    WAKE_ATTEMPT_COUNT.store(0, Ordering::Relaxed);
    WAKE_SUCCESS_COUNT.store(0, Ordering::Relaxed);
    KICK_COUNT.store(0, Ordering::Relaxed);
    LAST_WAKE_STATE.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_set_pending_count() -> u32 {
    SET_PENDING_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_wake_attempt_count() -> u32 {
    WAKE_ATTEMPT_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_wake_success_count() -> u32 {
    WAKE_SUCCESS_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_kick_count() -> u32 {
    KICK_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_wake_state_test_last_wake_state() -> u32 {
    LAST_WAKE_STATE.load(Ordering::Relaxed)
}

/// Wakes a task so that it can process a pending signal.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn signal_wake_up_state(task: *mut Task, state: u32) {
    #[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the task is live.
    unsafe { bindings::rust_helper_signal_wake_state_set_pending(task) };
    #[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
    SET_PENDING_COUNT.fetch_add(1, Ordering::Relaxed);

    let wake_state = state | TASK_INTERRUPTIBLE;
    // SAFETY: The task remains live under the signal-handler lock.
    let woken = unsafe { bindings::rust_helper_signal_wake_state_wake_up(task, wake_state) };
    #[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
    {
        WAKE_ATTEMPT_COUNT.fetch_add(1, Ordering::Relaxed);
        LAST_WAKE_STATE.store(wake_state, Ordering::Relaxed);
    }

    if woken {
        #[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
        WAKE_SUCCESS_COUNT.fetch_add(1, Ordering::Relaxed);
    } else {
        // SAFETY: The task remains live under the signal-handler lock.
        unsafe { bindings::rust_helper_signal_wake_state_kick(task) };
        #[cfg(CONFIG_SIGNAL_WAKE_STATE_KUNIT_TEST)]
        KICK_COUNT.fetch_add(1, Ordering::Relaxed);
    }
}

/// Wakes a task so that it can process a pending signal.
///
/// # Safety
///
/// The same requirements as [`signal_wake_up_state`] apply.
#[export]
unsafe extern "C" fn rust_signal_wake_up_state(task: *mut Task, state: u32) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { signal_wake_up_state(task, state) };
}
