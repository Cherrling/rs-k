// SPDX-License-Identifier: GPL-2.0

//! Native final task resource teardown.
//!
//! Rust owns the teardown order after the final task reference is released.
//! C retains invariant diagnostics, subsystem cleanup functions, and final
//! task memory release.

use crate::bindings;
use macros::export;

type Task = bindings::task_struct;

/// Performs final task resource teardown.
///
/// # Safety
///
/// `task` must be a valid task with zero references. The call must satisfy the
/// context requirements of the original `__put_task_struct()` function.
unsafe fn run(task: *mut Task) {
    // SAFETY: The caller guarantees that the task has zero references.
    unsafe { bindings::rust_helper_task_final_check(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_unwind_task_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_io_uring_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_cgroup_task_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_task_numa_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_security_task_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_exit_creds(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_delayacct_tsk_free(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_put_signal_struct(task) };
    // SAFETY: The final reference is gone and the task storage is still live.
    unsafe { bindings::rust_helper_task_final_sched_core_free(task) };
    // SAFETY: This call consumes the task storage.
    unsafe { bindings::rust_helper_task_final_free_task(task) };
}

/// Performs final task resource teardown through the C ABI.
///
/// # Safety
///
/// The same requirements as [`run`] apply.
#[export]
unsafe extern "C" fn rust_task_put_final(task: *mut Task) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { run(task) };
}
