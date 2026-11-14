// SPDX-License-Identifier: GPL-2.0

//! Native task-structure duplication.
//!
//! Rust owns the NUMA-node decision, allocation sequence, stack publication,
//! initialization order, and staged cleanup for a new task. C helpers provide
//! architecture, allocator, stack-accounting, CPU-mask, and subsystem
//! primitives.

use crate::bindings;
use core::ffi::c_int;
use core::ptr;
use macros::export;

#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;

const FAIL_AFTER_TASK: u32 = 1;
const FAIL_AFTER_STACK: u32 = 2;
const FAIL_AFTER_IPI_MASK: u32 = 3;

#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static TASK_ALLOC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static STACK_ALLOC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static COMPLETE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[derive(Clone, Copy)]
enum CleanupStage {
    Task,
    Stack,
    IpiMask,
}

/// Returns the number of production entry calls observed.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of successful task allocations observed.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_task_alloc_count() -> u32 {
    TASK_ALLOC_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of successful kernel-stack allocations observed.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_stack_alloc_count() -> u32 {
    STACK_ALLOC_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of completed task duplications observed.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_complete_count() -> u32 {
    COMPLETE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of failed task duplications observed.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one test-only failure after the selected cleanup stage.
#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_task_struct_test_fail_next(stage: u32) {
    NEXT_FAILURE.store(stage, Ordering::Release);
}

#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST))]
fn record_failure() {}

#[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

/// Releases resources acquired before a duplication failure.
///
/// # Safety
///
/// `task` must be a live unpublished task. `stage` must name the last resource
/// successfully acquired for that task.
unsafe fn cleanup(task: *mut Task, stage: CleanupStage) {
    if matches!(stage, CleanupStage::IpiMask) {
        // SAFETY: The IPI mask was allocated for this task.
        unsafe { bindings::rust_helper_dup_task_struct_free_ipi_mask(task) };
    }
    if !matches!(stage, CleanupStage::Task) {
        // SAFETY: The kernel stack is live and has an accounting entry.
        unsafe {
            bindings::rust_helper_dup_task_struct_exit_stack_account(task);
            bindings::rust_helper_dup_task_struct_free_stack(task);
        }
    }
    // SAFETY: The task allocation is live and no stack remains.
    unsafe { bindings::rust_helper_dup_task_struct_free_task(task) };
}

/// Initializes inherited and transient task state after stack allocation.
///
/// # Safety
///
/// `task` must be a live and exclusively owned task with an allocated kernel
/// stack. `orig` must remain live for the duration of the call.
unsafe fn initialize_task(task: *mut Task, orig: *mut Task, node: c_int) {
    #[cfg(CONFIG_SECCOMP)]
    // SAFETY: The task is unpublished and exclusively owned.
    unsafe {
        (*task).seccomp.filter = ptr::null_mut();
    }
    // SAFETY: The task is unpublished and exclusively owned.
    unsafe {
        (*task).exec_state = ptr::null_mut();
    }

    // SAFETY: The task stack is allocated and the source task is live.
    unsafe {
        bindings::rust_helper_dup_task_struct_init_thread_stack(task, orig);
        bindings::rust_helper_dup_task_struct_clear_thread_flags(task);
        bindings::rust_helper_dup_task_struct_set_stack_magic(task);
        bindings::rust_helper_dup_task_struct_clear_syscall_dispatch(task);
        bindings::rust_helper_dup_task_struct_init_canary(task);
        bindings::rust_helper_dup_task_struct_init_cpus(task, orig, node);
        bindings::rust_helper_dup_task_struct_init_refcounts(task);
    }

    #[cfg(CONFIG_BLK_DEV_IO_TRACE)]
    // SAFETY: The task is unpublished and exclusively owned.
    unsafe {
        (*task).btrace_seq = 0;
    }
    // SAFETY: These transient fields do not carry inherited references.
    unsafe {
        (*task).splice_pipe = ptr::null_mut();
        (*task).task_frag.page = ptr::null_mut();
        (*task).wake_q.next = ptr::null_mut();
        (*task).worker_private = ptr::null_mut();
    }

    // SAFETY: The task stack is allocated and subsystem initialization may use
    // the new task context.
    unsafe { bindings::rust_helper_dup_task_struct_init_subsystems(task) };
    // SAFETY: Configuration-dependent fields belong to the new task.
    unsafe { bindings::rust_helper_dup_task_struct_init_conditional_fields(task) };
}

/// Duplicates an existing task structure.
///
/// # Safety
///
/// `orig` must be live in task-creation context and remain live for the call.
/// `node` must be a valid NUMA node or [`bindings::NUMA_NO_NODE`].
unsafe fn duplicate_task_struct(orig: *mut Task, node: c_int) -> *mut Task {
    #[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The source task remains live during task creation.
    let node = unsafe { bindings::rust_helper_dup_task_struct_resolve_node(orig, node) };
    // SAFETY: The resolved node is valid for task allocation.
    let task = unsafe { bindings::rust_helper_dup_task_struct_alloc(node) };
    if task.is_null() {
        record_failure();
        return ptr::null_mut();
    }
    #[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
    TASK_ALLOC_COUNT.fetch_add(1, Ordering::Relaxed);

    if requested_failure(FAIL_AFTER_TASK) {
        record_failure();
        // SAFETY: Only the task allocation is live.
        unsafe { cleanup(task, CleanupStage::Task) };
        return ptr::null_mut();
    }

    // SAFETY: The new task allocation is live and exclusively owned.
    let error = unsafe { bindings::rust_helper_dup_task_struct_arch_copy(task, orig) };
    if error != 0 {
        record_failure();
        // SAFETY: Only the task allocation is live.
        unsafe { cleanup(task, CleanupStage::Task) };
        return ptr::null_mut();
    }

    // SAFETY: The task allocation is live and the resolved node is valid.
    let error = unsafe { bindings::rust_helper_dup_task_struct_alloc_stack(task, node) };
    if error != 0 {
        record_failure();
        // SAFETY: The task allocation is live and no stack was published.
        unsafe { cleanup(task, CleanupStage::Task) };
        return ptr::null_mut();
    }
    #[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
    STACK_ALLOC_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The task now owns a live kernel stack.
    unsafe { bindings::rust_helper_dup_task_struct_account_stack(task) };

    if requested_failure(FAIL_AFTER_STACK) {
        record_failure();
        // SAFETY: The task owns an accounted stack.
        unsafe { cleanup(task, CleanupStage::Stack) };
        return ptr::null_mut();
    }

    // SAFETY: The task is unpublished and owns the allocated stack.
    let error = unsafe { bindings::rust_helper_dup_task_struct_alloc_ipi_mask(task) };
    if error != 0 {
        record_failure();
        // SAFETY: The stack is allocated and the task still owns it.
        unsafe { cleanup(task, CleanupStage::Stack) };
        return ptr::null_mut();
    }

    if requested_failure(FAIL_AFTER_IPI_MASK) {
        record_failure();
        // SAFETY: The IPI mask and stack were successfully allocated.
        unsafe { cleanup(task, CleanupStage::IpiMask) };
        return ptr::null_mut();
    }

    // SAFETY: The task owns its stack and IPI mask.
    let error = unsafe { bindings::rust_helper_dup_task_struct_prepare_scs(task, node) };
    if error != 0 {
        record_failure();
        // SAFETY: The IPI mask and stack were successfully allocated.
        unsafe { cleanup(task, CleanupStage::IpiMask) };
        return ptr::null_mut();
    }

    // SAFETY: All fallible allocations completed and the task is exclusively
    // owned by task creation.
    unsafe { initialize_task(task, orig, node) };
    #[cfg(CONFIG_DUP_TASK_STRUCT_KUNIT_TEST)]
    COMPLETE_COUNT.fetch_add(1, Ordering::Relaxed);
    task
}

/// Duplicates a task structure through the C ABI.
///
/// # Safety
///
/// The requirements of [`duplicate_task_struct`] apply.
#[export]
unsafe extern "C" fn rust_dup_task_struct(orig: *mut Task, node: c_int) -> *mut Task {
    // SAFETY: The caller upholds the function requirements.
    unsafe { duplicate_task_struct(orig, node) }
}
