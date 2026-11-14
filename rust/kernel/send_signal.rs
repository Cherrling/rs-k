// SPDX-License-Identifier: GPL-2.0

//! Native signal queue insertion.
//!
//! Rust owns queue admission, legacy de-duplication, allocation policy,
//! siginfo selection, pending-state transitions, multiprocess stop/continue
//! updates, completion, and trace result selection. C helpers provide queue
//! allocation, intrusive-list operations, signal-set bit operations, credential
//! formatting, tracing, and the remaining low-level signal policies.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Info = bindings::kernel_siginfo;
type MultiprocessSignals = bindings::multiprocess_signals;
type Pending = bindings::sigpending;
type Queue = bindings::sigqueue;
type Task = bindings::task_struct;

const EAGAIN: i32 = bindings::EAGAIN as i32;
const PF_EXITING: u32 = bindings::PF_EXITING;
const PF_KTHREAD: u32 = bindings::PF_KTHREAD;
const PIDTYPE_PID: u32 = bindings::pid_type_PIDTYPE_PID;
const PIDTYPE_TGID: u32 = bindings::pid_type_PIDTYPE_TGID;
const SI_USER: i32 = bindings::SI_USER as i32;
const SIGCONT: i32 = bindings::SIGCONT as i32;
const SIGKILL: i32 = bindings::SIGKILL as i32;
const SIGRTMIN: i32 = bindings::SIGRTMIN as i32;
// Must match the trace result enum in include/trace/events/signal.h.
const TRACE_SIGNAL_DELIVERED: u32 = 0;
const TRACE_SIGNAL_IGNORED: u32 = 1;
const TRACE_SIGNAL_ALREADY_PENDING: u32 = 2;
const TRACE_SIGNAL_OVERFLOW_FAIL: u32 = 3;
const TRACE_SIGNAL_LOSE_INFO: u32 = 4;

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static EXIT_DROP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static PREPARE_DROP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static LEGACY_DROP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static ALLOC_SKIP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static PENDING_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static MULTIPROCESS_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static COMPLETE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
static TRACE_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_reset() {
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    EXIT_DROP_COUNT.store(0, Ordering::Relaxed);
    PREPARE_DROP_COUNT.store(0, Ordering::Relaxed);
    LEGACY_DROP_COUNT.store(0, Ordering::Relaxed);
    ALLOC_SKIP_COUNT.store(0, Ordering::Relaxed);
    PENDING_COUNT.store(0, Ordering::Relaxed);
    MULTIPROCESS_COUNT.store(0, Ordering::Relaxed);
    COMPLETE_COUNT.store(0, Ordering::Relaxed);
    TRACE_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_exit_drop_count() -> u32 {
    EXIT_DROP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_prepare_drop_count() -> u32 {
    PREPARE_DROP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_legacy_drop_count() -> u32 {
    LEGACY_DROP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_alloc_skip_count() -> u32 {
    ALLOC_SKIP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_pending_count() -> u32 {
    PENDING_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_multiprocess_count() -> u32 {
    MULTIPROCESS_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_complete_count() -> u32 {
    COMPLETE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
#[export]
extern "C" fn rust_send_signal_test_trace_count() -> u32 {
    TRACE_COUNT.load(Ordering::Relaxed)
}

/// Returns whether a signal can be queued for `task`.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn task_can_queue_signal(task: *mut Task, pid_type: u32) -> bool {
    // SAFETY: The task is live under the signal-handler lock.
    if unsafe { (*task).flags & PF_EXITING == 0 } {
        return true;
    }

    pid_type != PIDTYPE_PID
}

/// Selects the task-local or thread-group queue.
///
/// # Safety
///
/// `task` must be live and the caller must hold `task->sighand->siglock`.
unsafe fn pending_queue(task: *mut Task, pid_type: u32) -> *mut Pending {
    if pid_type == PIDTYPE_PID {
        // SAFETY: The task and its pending queue are live under the lock.
        unsafe { core::ptr::addr_of_mut!((*task).pending) }
    } else {
        // SAFETY: The signal structure is live under the lock.
        unsafe { core::ptr::addr_of_mut!((*((*task).signal)).shared_pending) }
    }
}

/// Formats the queue payload selected by the caller.
///
/// # Safety
///
/// `queue` must be allocated and `info` must be valid for the selected branch.
unsafe fn initialize_info(queue: *mut Queue, sig: i32, info: *mut Info, task: *mut Task) {
    // SAFETY: The caller ensures that `info` is valid for payload selection.
    if unsafe { bindings::rust_helper_send_signal_is_noinfo(info) } {
        // SAFETY: The queue and target task are valid.
        unsafe { bindings::rust_helper_send_signal_init_noinfo(queue, sig, task) };
    // SAFETY: The caller ensures that `info` is valid for payload selection.
    } else if unsafe { bindings::rust_helper_send_signal_is_priv(info) } {
        // SAFETY: The queue is valid and the private payload needs no user data.
        unsafe { bindings::rust_helper_send_signal_init_priv(queue, sig) };
    } else {
        // SAFETY: The caller supplied a regular siginfo pointer.
        unsafe { bindings::rust_helper_send_signal_copy_info(queue, info) };
    }
}

/// Updates delayed multiprocess stop/continue state.
///
/// # Safety
///
/// `task` must be live and the caller must hold its signal-handler lock.
unsafe fn update_multiprocess_signals(sig: i32, task: *mut Task) {
    // SAFETY: The signal structure is live under the lock.
    let signal = unsafe { (*task).signal };
    // SAFETY: The delayed list is protected by the signal-handler lock.
    let mut delayed: *mut MultiprocessSignals =
        unsafe { bindings::rust_helper_send_signal_first_delayed(signal) };
    // SAFETY: Signal classification reads no caller-owned memory.
    let kernel_stop = unsafe { bindings::rust_helper_send_signal_kernel_stop(sig) };

    while !delayed.is_null() {
        // SAFETY: The delayed entry is live under the lock.
        let set = unsafe { core::ptr::addr_of_mut!((*delayed).signal) };
        if sig == SIGCONT {
            // SAFETY: The delayed signal set is live under the lock.
            unsafe { bindings::rust_helper_send_signal_delete_stop_mask(set) };
        } else if kernel_stop {
            // SAFETY: The delayed signal set is live under the lock.
            unsafe { bindings::rust_helper_send_signal_delete_cont(set) };
        }
        // SAFETY: The delayed signal set is live under the lock.
        unsafe { bindings::rust_helper_send_signal_add_pending(set, sig) };

        #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
        MULTIPROCESS_COUNT.fetch_add(1, Ordering::Relaxed);
        // SAFETY: The signal-handler lock keeps the delayed list stable.
        delayed = unsafe { bindings::rust_helper_send_signal_next_delayed(delayed) };
    }
}

/// Inserts a signal into the selected pending queue.
///
/// # Safety
///
/// `task` must be live, `info` must be valid for the signal source, and the
/// caller must hold `task->sighand->siglock` with local interrupts disabled.
unsafe fn send_signal_locked(
    sig: i32,
    info: *mut Info,
    task: *mut Task,
    pid_type: u32,
    force: bool,
) -> i32 {
    #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees the locked task contract.
    unsafe { bindings::rust_helper_send_signal_assert_locked(task) };

    let mut result = TRACE_SIGNAL_IGNORED;
    // SAFETY: The task is live under the signal-handler lock.
    if !unsafe { task_can_queue_signal(task, pid_type) } {
        #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
        EXIT_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
        // SAFETY: The caller still holds the signal lock and all pointers are valid.
        return unsafe { finish(sig, info, task, pid_type, result) };
    }

    // SAFETY: The C policy validates the signal and task state under the lock.
    if !unsafe { bindings::rust_helper_send_signal_prepare(sig, task, force) } {
        #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
        PREPARE_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
        // SAFETY: The caller still holds the signal lock and all pointers are valid.
        return unsafe { finish(sig, info, task, pid_type, result) };
    }

    // SAFETY: The task and selected queue are live under the lock.
    let pending = unsafe { pending_queue(task, pid_type) };
    // SAFETY: The selected pending queue is live under the signal-handler lock.
    if unsafe { bindings::rust_helper_send_signal_pending_member(pending, sig) } && sig < SIGRTMIN {
        #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
        LEGACY_DROP_COUNT.fetch_add(1, Ordering::Relaxed);
        result = TRACE_SIGNAL_ALREADY_PENDING;
        // SAFETY: The caller still holds the signal lock and all pointers are valid.
        return unsafe { finish(sig, info, task, pid_type, result) };
    }

    result = TRACE_SIGNAL_DELIVERED;
    // SAFETY: The task is live under the signal-handler lock.
    let skip_allocation = sig == SIGKILL || unsafe { (*task).flags & PF_KTHREAD != 0 };
    if !skip_allocation {
        // SAFETY: The caller selected a regular or special siginfo pointer.
        let special = unsafe { bindings::rust_helper_send_signal_is_special(info) };
        let code = if special {
            0
        } else {
            // SAFETY: A non-special pointer references a valid kernel siginfo.
            unsafe { bindings::rust_helper_send_signal_info_code(info) }
        };
        let override_rlimit = sig < SIGRTMIN && (special || code >= 0);
        // SAFETY: The target task is live under the signal-handler lock.
        let queue = unsafe {
            bindings::rust_helper_send_signal_alloc_queue(sig, task, override_rlimit.into())
        };

        if !queue.is_null() {
            // SAFETY: The new queue is valid and the pending list is live.
            unsafe {
                bindings::rust_helper_send_signal_append_queue(queue, pending);
                initialize_info(queue, sig, info, task);
            }
        } else if !special && sig >= SIGRTMIN && code != SI_USER {
            result = TRACE_SIGNAL_OVERFLOW_FAIL;
            // SAFETY: The caller still holds the signal lock and all pointers are valid.
            return unsafe { finish(sig, info, task, pid_type, result) };
        } else {
            result = TRACE_SIGNAL_LOSE_INFO;
        }
    } else {
        #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
        ALLOC_SKIP_COUNT.fetch_add(1, Ordering::Relaxed);
    }

    // SAFETY: The task remains live under the signal-handler lock.
    unsafe { bindings::rust_helper_send_signal_signalfd_notify(task, sig) };
    // SAFETY: The pending queue is live under the signal-handler lock.
    unsafe {
        bindings::rust_helper_send_signal_add_pending(
            core::ptr::addr_of_mut!((*pending).signal),
            sig,
        )
    };
    #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
    PENDING_COUNT.fetch_add(1, Ordering::Relaxed);

    if pid_type > PIDTYPE_TGID {
        // SAFETY: The target task remains live under the signal-handler lock.
        unsafe { update_multiprocess_signals(sig, task) };
    }

    // SAFETY: The queue, task, and signal state remain live under the lock.
    unsafe { bindings::rust_helper_send_signal_complete(sig, task, pid_type) };
    #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
    COMPLETE_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller still holds the signal lock and all pointers are valid.
    unsafe { finish(sig, info, task, pid_type, result) }
}

/// Emits the generation tracepoint and returns the queue result.
///
/// # Safety
///
/// The caller must uphold the [`send_signal_locked`] contract.
unsafe fn finish(sig: i32, info: *mut Info, task: *mut Task, pid_type: u32, result: u32) -> i32 {
    // SAFETY: The task is live and the tracepoint does not retain pointers.
    unsafe {
        bindings::rust_helper_send_signal_trace(sig, info, task, pid_type != PIDTYPE_PID, result)
    };
    #[cfg(CONFIG_SEND_SIGNAL_KUNIT_TEST)]
    TRACE_COUNT.fetch_add(1, Ordering::Relaxed);
    if result == TRACE_SIGNAL_OVERFLOW_FAIL {
        -EAGAIN
    } else {
        0
    }
}

/// Inserts a signal into a pending queue.
///
/// # Safety
///
/// The requirements of [`send_signal_locked`] apply.
#[export]
unsafe extern "C" fn rust_send_signal_locked(
    sig: i32,
    info: *mut Info,
    task: *mut Task,
    pid_type: u32,
    force: bool,
) -> i32 {
    // SAFETY: The caller upholds the function requirements.
    unsafe { send_signal_locked(sig, info, task, pid_type, force) }
}
