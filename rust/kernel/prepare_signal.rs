// SPDX-License-Identifier: GPL-2.0

//! Native signal preparation.
//!
//! Rust owns the process-wide stop/continue state machine and the signal
//! preparation result. C helpers provide queue-mask cleanup, signal policy
//! queries, thread traversal, ptrace notification, wakeup, and signal-flag bit
//! operations.

use crate::bindings;
use core::mem::MaybeUninit;
use macros::export;

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Pending = bindings::sigpending;
type SignalSet = bindings::sigset_t;
type Task = bindings::task_struct;

const SIGNAL_CLD_CONTINUED: u32 = bindings::SIGNAL_CLD_CONTINUED;
const SIGNAL_CLD_STOPPED: u32 = bindings::SIGNAL_CLD_STOPPED;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;
const SIGNAL_STOP_CONTINUED: u32 = bindings::SIGNAL_STOP_CONTINUED;
const SIGNAL_STOP_STOPPED: u32 = bindings::SIGNAL_STOP_STOPPED;
const SIGCONT: i32 = bindings::SIGCONT as i32;
const SIGKILL: i32 = bindings::SIGKILL as i32;

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static GROUP_EXIT_DROP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static COREDUMP_KILL_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static KERNEL_STOP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static CONTINUE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static SEIZED_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static STOPPED_NOTIFY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static GROUP_STOP_NOTIFY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static IGNORE_DECISION_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static IGNORE_DROP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
static DELIVER_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_reset() {
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    GROUP_EXIT_DROP_COUNT.store(0, Ordering::Relaxed);
    COREDUMP_KILL_COUNT.store(0, Ordering::Relaxed);
    KERNEL_STOP_COUNT.store(0, Ordering::Relaxed);
    CONTINUE_COUNT.store(0, Ordering::Relaxed);
    SEIZED_COUNT.store(0, Ordering::Relaxed);
    STOPPED_NOTIFY_COUNT.store(0, Ordering::Relaxed);
    GROUP_STOP_NOTIFY_COUNT.store(0, Ordering::Relaxed);
    IGNORE_DECISION_COUNT.store(0, Ordering::Relaxed);
    IGNORE_DROP_COUNT.store(0, Ordering::Relaxed);
    DELIVER_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_group_exit_drop_count() -> u32 {
    GROUP_EXIT_DROP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_coredump_kill_count() -> u32 {
    COREDUMP_KILL_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_kernel_stop_count() -> u32 {
    KERNEL_STOP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_continue_count() -> u32 {
    CONTINUE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_seized_count() -> u32 {
    SEIZED_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_stopped_notify_count() -> u32 {
    STOPPED_NOTIFY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_group_stop_notify_count() -> u32 {
    GROUP_STOP_NOTIFY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_ignore_decision_count() -> u32 {
    IGNORE_DECISION_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_ignore_drop_count() -> u32 {
    IGNORE_DROP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_prepare_signal_test_deliver_count() -> u32 {
    DELIVER_COUNT.load(Ordering::Relaxed)
}

/// Clears the selected pending queue with a prepared signal mask.
///
/// # Safety
///
/// The task, mask, and pending queue must be live under the signal-handler lock.
unsafe fn flush_pending(task: *mut Task, mask: *mut SignalSet, pending: *mut Pending) {
    // SAFETY: The caller upholds the queue-clearing contract.
    unsafe { bindings::rust_helper_prepare_signal_flush(task, mask, pending) };
}

/// Clears pending SIGCONT state for a process-wide kernel stop.
///
/// # Safety
///
/// The task must be live and the caller must hold its signal-handler lock.
unsafe fn flush_continue_for_stop(task: *mut Task) {
    let mut flush = MaybeUninit::<SignalSet>::uninit();
    let mask = flush.as_mut_ptr();
    // SAFETY: The mask storage is writable and the signal lock is held.
    unsafe { bindings::rust_helper_prepare_signal_init_cont_flush(mask) };

    // SAFETY: The signal structure is live under the signal-handler lock.
    let signal = unsafe { (*task).signal };
    // SAFETY: The shared queue and mask are live under the signal lock.
    unsafe {
        flush_pending(
            task,
            mask,
            core::ptr::addr_of_mut!((*signal).shared_pending),
        )
    };

    // SAFETY: The thread list is stable under the signal-handler lock.
    let mut thread = unsafe { bindings::rust_helper_prepare_signal_first_thread(signal) };
    while !thread.is_null() {
        // SAFETY: The thread and its pending queue are live under the lock.
        unsafe { flush_pending(task, mask, core::ptr::addr_of_mut!((*thread).pending)) };
        // SAFETY: The thread list is stable under the signal lock.
        thread = unsafe { bindings::rust_helper_prepare_signal_next_group_thread(thread) };
    }
}

/// Applies the process-wide SIGCONT transition.
///
/// # Safety
///
/// The task must be live and the caller must hold its signal-handler lock.
unsafe fn prepare_continue(task: *mut Task) {
    let mut flush = MaybeUninit::<SignalSet>::uninit();
    let mask = flush.as_mut_ptr();
    // SAFETY: The mask storage is writable and the signal lock is held.
    unsafe { bindings::rust_helper_prepare_signal_init_stop_flush(mask) };

    // SAFETY: The signal structure is live under the signal-handler lock.
    let signal = unsafe { (*task).signal };
    // SAFETY: The shared queue and mask are live under the signal lock.
    unsafe {
        flush_pending(
            task,
            mask,
            core::ptr::addr_of_mut!((*signal).shared_pending),
        )
    };

    // SAFETY: The thread list is stable under the signal-handler lock.
    let mut thread = unsafe { bindings::rust_helper_prepare_signal_first_thread(signal) };
    while !thread.is_null() {
        // SAFETY: The thread and its pending queue are live under the lock.
        unsafe { flush_pending(task, mask, core::ptr::addr_of_mut!((*thread).pending)) };
        // SAFETY: The thread's jobctl state is live under the signal lock.
        unsafe { bindings::rust_helper_prepare_signal_clear_stop_pending(thread) };

        // SAFETY: The task's ptrace state is live under the signal lock.
        if unsafe { bindings::rust_helper_prepare_signal_is_seized(thread) } {
            // SAFETY: The ptrace state keeps the task available under the lock.
            unsafe { bindings::rust_helper_prepare_signal_ptrace_notify(thread) };
            #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
            SEIZED_COUNT.fetch_add(1, Ordering::Relaxed);
        } else {
            // SAFETY: The thread's jobctl state is live under the signal lock.
            unsafe { bindings::rust_helper_prepare_signal_clear_stopped(thread) };
            // SAFETY: The task is live and its wakeup state is protected by the lock.
            unsafe { bindings::rust_helper_prepare_signal_wake_stopped(thread) };
        }

        // SAFETY: The thread list is stable under the signal lock.
        thread = unsafe { bindings::rust_helper_prepare_signal_next_group_thread(thread) };
    }

    let mut why = 0;
    // SAFETY: The signal flags are live under the signal-handler lock.
    if unsafe { (*signal).flags & SIGNAL_STOP_STOPPED } != 0 {
        why = SIGNAL_CLD_CONTINUED;
        #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
        STOPPED_NOTIFY_COUNT.fetch_add(1, Ordering::Relaxed);
    // SAFETY: The group-stop count is live under the signal-handler lock.
    } else if unsafe { (*signal).group_stop_count } != 0 {
        why = SIGNAL_CLD_STOPPED;
        #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
        GROUP_STOP_NOTIFY_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    if why != 0 {
        // SAFETY: The signal state is live under the signal-handler lock.
        unsafe {
            bindings::rust_helper_prepare_signal_set_stop_flags(signal, why | SIGNAL_STOP_CONTINUED)
        };
        // SAFETY: The signal state is writable under the signal-handler lock.
        unsafe {
            (*signal).group_stop_count = 0;
            (*signal).group_exit_code = 0;
        }
    }
}

/// Prepares a signal for queue insertion.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn prepare_signal(sig: i32, task: *mut Task, force: bool) -> bool {
    #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The signal structure is live under the signal-handler lock.
    let signal = unsafe { (*task).signal };
    // SAFETY: The signal flags and core state are protected by the lock.
    let group_exit = unsafe { (*signal).flags & SIGNAL_GROUP_EXIT } != 0;

    if group_exit {
        // SAFETY: The core state is protected by the signal-handler lock.
        if unsafe { (*signal).core_state.is_null() } {
            #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
            GROUP_EXIT_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
            return false;
        }

        if sig == SIGKILL {
            #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
            COREDUMP_KILL_COUNT.fetch_add(1, Ordering::Relaxed);
            return true;
        }

        #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
        GROUP_EXIT_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
        return false;
    }

    // SAFETY: Signal classification does not access caller-owned state.
    if unsafe { bindings::rust_helper_prepare_signal_is_kernel_stop(sig) } {
        // SAFETY: The caller guarantees the signal-lock contract.
        unsafe { flush_continue_for_stop(task) };
        #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
        KERNEL_STOP_COUNT.fetch_add(1, Ordering::Relaxed);
    } else if sig == SIGCONT {
        // SAFETY: The caller guarantees the signal-lock contract.
        unsafe { prepare_continue(task) };
        #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
        CONTINUE_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
    IGNORE_DECISION_COUNT.fetch_add(1, Ordering::Relaxed);
    // SAFETY: The task and signal-handler state are live under the lock.
    let ignored = unsafe { bindings::rust_helper_prepare_signal_ignored(task, sig, force) };
    #[cfg(CONFIG_PREPARE_SIGNAL_KUNIT_TEST)]
    if ignored {
        IGNORE_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
    } else {
        DELIVER_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    !ignored
}

/// Prepares a signal for queue insertion.
///
/// # Safety
///
/// The requirements of [`prepare_signal`] apply.
#[export]
unsafe extern "C" fn rust_prepare_signal(sig: i32, task: *mut Task, force: bool) -> bool {
    // SAFETY: The caller upholds the function requirements.
    unsafe { prepare_signal(sig, task, force) }
}
