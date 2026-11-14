// SPDX-License-Identifier: GPL-2.0

//! Native pending-signal recalculation.
//!
//! Rust owns the signal-set, job-control, cgroup-freezer, and freezer policy
//! that sets or clears `TIF_SIGPENDING`. C helpers provide the thread-flag,
//! cgroup-freezer, and freezer primitives.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;
type SignalSet = bindings::sigset_t;

const JOBCTL_PENDING_OR_FREEZE: usize =
    bindings::JOBCTL_PENDING_MASK as usize | bindings::JOBCTL_TRAP_FREEZE as usize;

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static RECALC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static TSK_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static SET_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static FREEZING_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static CLEAR_CHECK_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
static CLEAR_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_reset() {
    RECALC_COUNT.store(0, Ordering::Relaxed);
    TSK_COUNT.store(0, Ordering::Relaxed);
    SET_COUNT.store(0, Ordering::Relaxed);
    FREEZING_COUNT.store(0, Ordering::Relaxed);
    CLEAR_CHECK_COUNT.store(0, Ordering::Relaxed);
    CLEAR_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_recalc_count() -> u32 {
    RECALC_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_tsk_count() -> u32 {
    TSK_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_set_count() -> u32 {
    SET_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_freezing_count() -> u32 {
    FREEZING_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_clear_check_count() -> u32 {
    CLEAR_CHECK_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_pending_recalc_test_clear_count() -> u32 {
    CLEAR_COUNT.load(Ordering::Relaxed)
}

fn has_pending_signals(signal: &SignalSet, blocked: &SignalSet) -> bool {
    signal
        .sig
        .iter()
        .zip(blocked.sig.iter())
        .any(|(pending, blocked)| pending & !blocked != 0)
}

/// Sets the pending flag when the task has work that requires it.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn recalc_tsk(task: *mut Task) -> bool {
    #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
    TSK_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the task and its signal state are live.
    let (jobctl, local, shared, blocked) = unsafe {
        (
            (*task).jobctl,
            &(*task).pending.signal,
            &(*(*task).signal).shared_pending.signal,
            &(*task).blocked,
        )
    };
    let mut needs_flag = jobctl & JOBCTL_PENDING_OR_FREEZE != 0;
    if !needs_flag {
        needs_flag = has_pending_signals(local, blocked);
    }
    if !needs_flag {
        needs_flag = has_pending_signals(shared, blocked);
    }
    if !needs_flag {
        // SAFETY: The caller guarantees that the task is live under its signal lock.
        needs_flag = unsafe { bindings::rust_helper_signal_pending_recalc_cgroup_frozen(task) };
    }

    if needs_flag {
        // SAFETY: The task remains live under the signal-handler lock.
        unsafe { bindings::rust_helper_signal_pending_recalc_set(task) };
        #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
        SET_COUNT.fetch_add(1, Ordering::Relaxed);
        true
    } else {
        false
    }
}

/// Recalculates the pending flag for the current task.
///
/// # Safety
///
/// `task` must be `current`, live, and protected by its signal-handler lock.
unsafe fn recalc(task: *mut Task) {
    #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
    RECALC_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller upholds the function requirements.
    if unsafe { recalc_tsk(task) } {
        return;
    }

    // SAFETY: The task remains live under the signal-handler lock.
    let freezing = unsafe { bindings::rust_helper_signal_pending_recalc_freezing(task) };
    #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
    FREEZING_COUNT.fetch_add(1, Ordering::Relaxed);
    if freezing {
        return;
    }

    // SAFETY: The task remains live under the signal-handler lock.
    if unsafe { bindings::rust_helper_signal_pending_recalc_test(task) } {
        #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
        CLEAR_CHECK_COUNT.fetch_add(1, Ordering::Relaxed);
        // SAFETY: The current task can clear its own pending flag here.
        unsafe { bindings::rust_helper_signal_pending_recalc_clear(task) };
        #[cfg(CONFIG_SIGNAL_PENDING_RECALC_KUNIT_TEST)]
        CLEAR_COUNT.fetch_add(1, Ordering::Relaxed);
    }
}

/// Recalculates the pending flag for a task without clearing it.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
#[export]
unsafe extern "C" fn rust_signal_pending_recalc_tsk(task: *mut Task) -> bool {
    // SAFETY: The caller upholds the function requirements.
    unsafe { recalc_tsk(task) }
}

/// Recalculates the pending flag for `current`.
///
/// # Safety
///
/// The requirements of [`recalc`] apply.
#[export]
unsafe extern "C" fn rust_signal_pending_recalc(task: *mut Task) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { recalc(task) };
}
