// SPDX-License-Identifier: GPL-2.0

//! Native shared pending signal retargeting.
//!
//! Rust owns signal-set intersection, thread-group traversal, exit-state
//! filtering, retarget selection, and wakeup decisions. C helpers provide
//! thread traversal, pending-state inspection, and signal wakeup primitives.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Sigset = bindings::sigset_t;
type Task = bindings::task_struct;

const PF_EXITING: u32 = bindings::PF_EXITING;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static GROUP_EXIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static EMPTY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static THREAD_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static EXITING_SKIP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static BLOCKED_SKIP_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static WAKE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
static COMPLETE_COUNT: AtomicU32 = AtomicU32::new(0);

fn set_and(destination: &mut Sigset, left: &Sigset, right: &Sigset) {
    for ((destination, left), right) in destination
        .sig
        .iter_mut()
        .zip(left.sig.iter())
        .zip(right.sig.iter())
    {
        *destination = *left & *right;
    }
}

fn set_intersect(destination: &mut Sigset, other: &Sigset) {
    for (destination, other) in destination.sig.iter_mut().zip(other.sig.iter()) {
        *destination &= *other;
    }
}

fn is_empty(set: &Sigset) -> bool {
    set.sig.iter().all(|bits| *bits == 0)
}

fn has_pending_signals(signal: &Sigset, blocked: &Sigset) -> bool {
    signal
        .sig
        .iter()
        .zip(blocked.sig.iter())
        .any(|(signal, blocked)| *signal & !*blocked != 0)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_reset() {
    ENTRY_COUNT.store(0, Ordering::Relaxed);
    GROUP_EXIT_COUNT.store(0, Ordering::Relaxed);
    EMPTY_COUNT.store(0, Ordering::Relaxed);
    THREAD_COUNT.store(0, Ordering::Relaxed);
    EXITING_SKIP_COUNT.store(0, Ordering::Relaxed);
    BLOCKED_SKIP_COUNT.store(0, Ordering::Relaxed);
    WAKE_COUNT.store(0, Ordering::Relaxed);
    COMPLETE_COUNT.store(0, Ordering::Relaxed);
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_group_exit_count() -> u32 {
    GROUP_EXIT_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_empty_count() -> u32 {
    EMPTY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_thread_count() -> u32 {
    THREAD_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_exiting_skip_count() -> u32 {
    EXITING_SKIP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_blocked_skip_count() -> u32 {
    BLOCKED_SKIP_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_wake_count() -> u32 {
    WAKE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_retarget_test_complete_count() -> u32 {
    COMPLETE_COUNT.load(Ordering::Relaxed)
}

/// Moves shared pending signal responsibility to other live threads.
///
/// # Safety
///
/// `task` must be live, `which` must point to a valid signal set, and the
/// caller must hold `task->sighand->siglock`.
unsafe fn retarget_shared_pending(task: *mut Task, which: *mut Sigset) {
    #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller guarantees that the signal structure is live.
    let signal = unsafe { (*task).signal };
    // SAFETY: The signal-handler lock protects the group-exit flags.
    if unsafe { (*signal).flags & SIGNAL_GROUP_EXIT != 0 } {
        #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
        GROUP_EXIT_COUNT.fetch_add(1, Ordering::Relaxed);
        return;
    }

    // SAFETY: The caller supplies a valid requested signal set.
    let requested = unsafe { &*which };
    // SAFETY: The signal-handler lock protects the shared pending set.
    let shared = unsafe { &(*signal).shared_pending.signal };
    let mut retarget = Sigset::default();
    set_and(&mut retarget, shared, requested);
    if is_empty(&retarget) {
        #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
        EMPTY_COUNT.fetch_add(1, Ordering::Relaxed);
        return;
    }

    // SAFETY: The signal-handler lock keeps the thread list stable.
    let mut thread = unsafe { bindings::rust_helper_signal_retarget_next_thread(task) };
    while thread != task {
        #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
        THREAD_COUNT.fetch_add(1, Ordering::Relaxed);

        // SAFETY: The thread list is stable under the signal-handler lock.
        if unsafe { (*thread).flags & PF_EXITING != 0 } {
            #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
            EXITING_SKIP_COUNT.fetch_add(1, Ordering::Relaxed);
        } else {
            // SAFETY: The target task and its signal mask are live under the lock.
            let blocked = unsafe { &(*thread).blocked };
            if !has_pending_signals(&retarget, blocked) {
                #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
                BLOCKED_SKIP_COUNT.fetch_add(1, Ordering::Relaxed);
            } else {
                set_intersect(&mut retarget, blocked);

                // SAFETY: The target task is live under the signal-handler lock.
                if !unsafe { bindings::rust_helper_signal_retarget_task_sigpending(thread) } {
                    // SAFETY: The target task is live under the signal-handler lock.
                    unsafe { bindings::rust_helper_signal_retarget_wake_up(thread) };
                    #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
                    WAKE_COUNT.fetch_add(1, Ordering::Relaxed);
                }

                if is_empty(&retarget) {
                    #[cfg(CONFIG_SIGNAL_RETARGET_KUNIT_TEST)]
                    COMPLETE_COUNT.fetch_add(1, Ordering::Relaxed);
                    break;
                }
            }
        }

        // SAFETY: The signal-handler lock keeps the thread list stable.
        thread = unsafe { bindings::rust_helper_signal_retarget_next_thread(thread) };
    }
}

/// Moves shared pending signal responsibility to other live threads.
///
/// # Safety
///
/// The same requirements as [`retarget_shared_pending`] apply.
#[export]
unsafe extern "C" fn rust_signal_retarget_pending(task: *mut Task, which: *mut Sigset) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { retarget_shared_pending(task, which) };
}
