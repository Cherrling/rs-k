// SPDX-License-Identifier: GPL-2.0

//! Native thread-group signal structure creation.
//!
//! Rust owns the `CLONE_THREAD` decision and the creation order for a new
//! `signal_struct`. C helpers provide allocation, subsystem constructors,
//! locking primitives, and inheritance reads that require kernel internals.

use crate::bindings;
use core::ffi::c_int;
use core::ptr;
use macros::export;

#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;
type SignalStruct = bindings::signal_struct;

const CLONE_THREAD: u64 = bindings::CLONE_THREAD as u64;

#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
static THREAD_RETURN_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
static ALLOC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
static COMPLETE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
static ALLOC_FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);

fn errno(error: u32) -> c_int {
    error as c_int
}

fn should_copy_signal_struct(clone_flags: u64) -> bool {
    clone_flags & CLONE_THREAD == 0
}

/// Tests the production thread-copy branch predicate.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_should_copy(clone_flags: u64) -> bool {
    should_copy_signal_struct(clone_flags)
}

/// Returns the number of production entry calls observed.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of shared thread-group early returns observed.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_thread_return_count() -> u32 {
    THREAD_RETURN_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of successful signal-structure allocations observed.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_alloc_count() -> u32 {
    ALLOC_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of completed signal-structure initializations observed.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_complete_count() -> u32 {
    COMPLETE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of signal-structure allocation failures observed.
#[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
#[export]
extern "C" fn rust_signal_struct_copy_test_alloc_failure_count() -> u32 {
    ALLOC_FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Initializes a newly allocated signal structure.
///
/// # Safety
///
/// `signal` must be a live, zeroed allocation owned by `task`. The caller must
/// run in the task-creation context whose `current->signal` is the source.
unsafe fn initialize_signal_struct(signal: *mut SignalStruct, task: *mut Task) {
    // SAFETY: The new signal allocation and task are live and unshared.
    unsafe {
        (*signal).nr_threads = 1;
        (*signal).quick_threads = 1;
    }
    // SAFETY: The allocation is live and exclusively owned by task creation.
    unsafe { bindings::rust_helper_signal_struct_copy_counts(signal) };

    // SAFETY: Both objects are live. The links form the initial thread group.
    let (thread_head, thread_node) = unsafe {
        (
            ptr::addr_of_mut!((*signal).thread_head),
            ptr::addr_of_mut!((*task).thread_node),
        )
    };
    // SAFETY: The new thread group and its first task are exclusively owned.
    unsafe {
        (*thread_head).next = thread_node;
        (*thread_head).prev = thread_node;
        (*thread_node).next = thread_head;
        (*thread_node).prev = thread_head;
    }

    // SAFETY: The allocation is live and exclusively owned by task creation.
    unsafe {
        bindings::rust_helper_signal_struct_copy_waitqueue(signal);
        (*signal).curr_target = task;
        bindings::rust_helper_signal_struct_copy_pending(signal);
        (*signal).multiprocess.first = ptr::null_mut();
        bindings::rust_helper_signal_struct_copy_stats_lock(signal);
        bindings::rust_helper_signal_struct_copy_prev_cputime(signal);
        bindings::rust_helper_signal_struct_copy_posix_timers(signal);
        bindings::rust_helper_signal_struct_copy_rlimits(signal);
        bindings::rust_helper_signal_struct_copy_cpu_timers(signal);
        bindings::rust_helper_signal_struct_copy_tty_audit(signal);
        bindings::rust_helper_signal_struct_copy_autogroup(signal);
        bindings::rust_helper_signal_struct_copy_cgroup_lock(signal);
        bindings::rust_helper_signal_struct_copy_oom_scores(signal);
        bindings::rust_helper_signal_struct_copy_cred_guard_lock(signal);
        bindings::rust_helper_signal_struct_copy_exec_update_lock(signal);
    }
}

/// Creates and initializes the signal structure for a new thread group.
///
/// # Safety
///
/// `task` must be an unpublished task under construction in `copy_process()`.
/// `clone_flags` must be the flags for that task-creation operation.
unsafe fn copy_signal_struct(task: *mut Task, clone_flags: u64) -> c_int {
    #[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    if !should_copy_signal_struct(clone_flags) {
        #[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
        THREAD_RETURN_COUNT.fetch_add(1, Ordering::Relaxed);
        return 0;
    }

    // SAFETY: The caller provides the unpublished task and fork context.
    let signal = unsafe { bindings::rust_helper_signal_struct_copy_alloc(task) };
    if signal.is_null() {
        #[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
        ALLOC_FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
        return -errno(bindings::ENOMEM);
    }
    #[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
    ALLOC_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: Allocation succeeded and remains owned by the unpublished task.
    unsafe { initialize_signal_struct(signal, task) };
    #[cfg(CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST)]
    COMPLETE_COUNT.fetch_add(1, Ordering::Relaxed);
    0
}

/// Creates the signal structure for a new task.
///
/// # Safety
///
/// The requirements of [`copy_signal_struct`] apply.
#[export]
unsafe extern "C" fn rust_signal_struct_copy(task: *mut Task, clone_flags: u64) -> c_int {
    // SAFETY: The caller upholds the function requirements.
    unsafe { copy_signal_struct(task, clone_flags) }
}
