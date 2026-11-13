// SPDX-License-Identifier: GPL-2.0

//! Native PID and task association state machine.
//!
//! Rust owns attach, detach, change, transfer, cached TID exchange, orphan
//! detection, and warning decisions. C helpers execute the individual list,
//! pointer, and lockdep primitives.

use crate::bindings;
use macros::export;

type Pid = bindings::pid;
type PidType = bindings::pid_type;
type Task = bindings::task_struct;

fn assert_tasklist_write() {
    // SAFETY: The caller must hold the task list write lock.
    unsafe { bindings::rust_helper_pid_assoc_assert_tasklist_write() };
}

/// Attaches the task's association node to its current PID.
///
/// # Safety
///
/// `task` must be live, `type` must be a valid PID type, the task must have a
/// current PID of that type, and `tasklist_lock` must be write-held.
unsafe fn attach(task: *mut Task, pid_type: PidType) {
    assert_tasklist_write();

    // SAFETY: The caller guarantees a live task and valid association type.
    let pid_ptr = unsafe { bindings::rust_helper_pid_assoc_task_pid_ptr(task, pid_type) };
    // SAFETY: The selected association slot contains a live PID.
    let pid = unsafe { *pid_ptr };
    // SAFETY: The task is live and its association node is currently unlinked.
    let link = unsafe { bindings::rust_helper_pid_assoc_task_link(task, pid_type) };
    // SAFETY: The live PID owns the matching task list head.
    let head = unsafe { bindings::rust_helper_pid_assoc_pid_tasks(pid, pid_type) };
    // SAFETY: The task list write lock protects this association change.
    unsafe { bindings::rust_helper_pid_assoc_add(link, head) };
}

/// Changes one association slot and stashes an orphaned PID.
///
/// # Safety
///
/// `pids` must contain `PIDTYPE_MAX` writable entries. `task` must be live,
/// `type` must be valid, and `tasklist_lock` must be write-held.
unsafe fn change(pids: *mut *mut Pid, task: *mut Task, pid_type: PidType, new: *mut Pid) {
    assert_tasklist_write();

    // SAFETY: The caller guarantees a live task and valid association type.
    let pid_ptr = unsafe { bindings::rust_helper_pid_assoc_task_pid_ptr(task, pid_type) };
    // SAFETY: The selected association slot contains a live PID.
    let pid = unsafe { *pid_ptr };
    // SAFETY: The task is live and its association node is currently linked.
    let link = unsafe { bindings::rust_helper_pid_assoc_task_link(task, pid_type) };
    // SAFETY: The task list write lock protects this list deletion.
    unsafe { bindings::rust_helper_pid_assoc_del(link) };
    // SAFETY: The task list write lock protects this pointer update.
    unsafe { bindings::rust_helper_pid_assoc_set_pid(pid_ptr, new) };

    let mut index = bindings::pid_type_PIDTYPE_MAX;
    while index != 0 {
        index -= 1;

        // SAFETY: `pid` is live until the caller releases its reference.
        if unsafe { bindings::rust_helper_pid_assoc_has_task(pid, index) } {
            return;
        }
    }

    // SAFETY: The caller supplies the PID stash array.
    if unsafe { bindings::rust_helper_pid_assoc_stash_present(pids, pid_type) } {
        // SAFETY: This preserves the original duplicate-stash diagnostic.
        unsafe { bindings::rust_helper_pid_assoc_stash_warn(pids, pid_type) };
    }
    // SAFETY: The orphaned PID must be retained by the caller.
    unsafe { bindings::rust_helper_pid_assoc_stash_set(pids, pid_type, pid) };
}

/// Attaches a task to its current PID association.
///
/// # Safety
///
/// The same requirements as [`attach`] apply.
#[export]
unsafe extern "C" fn rust_pid_attach(task: *mut Task, pid_type: PidType) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { attach(task, pid_type) };
}

/// Detaches a task association and returns an orphan to the caller.
///
/// # Safety
///
/// The same requirements as [`change`] apply. `tasklist_lock` must be
/// write-held.
#[export]
unsafe extern "C" fn rust_pid_detach(pids: *mut *mut Pid, task: *mut Task, pid_type: PidType) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { change(pids, task, pid_type, core::ptr::null_mut()) };
}

/// Replaces one task association and attaches the new PID.
///
/// # Safety
///
/// The same requirements as [`change`] and [`attach`] apply.
#[export]
unsafe extern "C" fn rust_pid_change(
    pids: *mut *mut Pid,
    task: *mut Task,
    pid_type: PidType,
    pid: *mut Pid,
) {
    // SAFETY: The caller upholds the function requirements.
    unsafe {
        change(pids, task, pid_type, pid);
        attach(task, pid_type);
    }
}

/// Exchanges the PID associations and cached TIDs of two tasks.
///
/// # Safety
///
/// Both tasks must be live, both must have a current PID, and `tasklist_lock`
/// must be write-held.
#[export]
unsafe extern "C" fn rust_pid_exchange_tids(left: *mut Task, right: *mut Task) {
    assert_tasklist_write();

    // SAFETY: The caller guarantees live tasks with PID associations.
    let left_ptr = unsafe {
        bindings::rust_helper_pid_assoc_task_pid_ptr(left, bindings::pid_type_PIDTYPE_PID)
    };
    // SAFETY: The caller guarantees live tasks with PID associations.
    let right_ptr = unsafe {
        bindings::rust_helper_pid_assoc_task_pid_ptr(right, bindings::pid_type_PIDTYPE_PID)
    };
    // SAFETY: Both task PID slots contain live PIDs.
    let left_pid = unsafe { *left_ptr };
    // SAFETY: Both task PID slots contain live PIDs.
    let right_pid = unsafe { *right_ptr };

    // SAFETY: Both PIDs own the matching PID type list head.
    let left_head = unsafe {
        bindings::rust_helper_pid_assoc_pid_tasks(left_pid, bindings::pid_type_PIDTYPE_PID)
    };
    // SAFETY: Both PIDs own the matching PID type list head.
    let right_head = unsafe {
        bindings::rust_helper_pid_assoc_pid_tasks(right_pid, bindings::pid_type_PIDTYPE_PID)
    };
    // SAFETY: The task list write lock protects the two single-entry lists.
    unsafe { bindings::rust_helper_pid_assoc_swap_heads(left_head, right_head) };

    // SAFETY: The write lock protects task PID pointer updates.
    unsafe {
        bindings::rust_helper_pid_assoc_set_rcu(left_ptr, right_pid);
        bindings::rust_helper_pid_assoc_set_rcu(right_ptr, left_pid);
    }

    // SAFETY: Both PIDs remain live under the task list write lock.
    let left_nr = unsafe { bindings::rust_helper_pid_assoc_pid_nr(right_pid) };
    // SAFETY: Both PIDs remain live under the task list write lock.
    let right_nr = unsafe { bindings::rust_helper_pid_assoc_pid_nr(left_pid) };
    // SAFETY: The write lock protects cached task PID updates.
    unsafe {
        bindings::rust_helper_pid_assoc_set_cached_pid(left, left_nr);
        bindings::rust_helper_pid_assoc_set_cached_pid(right, right_nr);
    }
}

/// Transfers a non-PID association node between tasks.
///
/// # Safety
///
/// Both tasks must be live, `type` must not be `PIDTYPE_PID`, the old task
/// must be linked and the new task node must be unlinked. `tasklist_lock` must
/// be write-held.
#[export]
unsafe extern "C" fn rust_pid_transfer(old: *mut Task, new: *mut Task, pid_type: PidType) {
    if pid_type == bindings::pid_type_PIDTYPE_PID {
        // SAFETY: This preserves the original invalid-PID-type diagnostic.
        unsafe { bindings::rust_helper_pid_assoc_warn_transfer_pid_type(pid_type) };
    }
    assert_tasklist_write();

    // SAFETY: Both tasks are live and use the same association type.
    let old_link = unsafe { bindings::rust_helper_pid_assoc_task_link(old, pid_type) };
    // SAFETY: Both tasks are live and use the same association type.
    let new_link = unsafe { bindings::rust_helper_pid_assoc_task_link(new, pid_type) };
    // SAFETY: The write lock protects this RCU list node replacement.
    unsafe { bindings::rust_helper_pid_assoc_replace(old_link, new_link) };
}
