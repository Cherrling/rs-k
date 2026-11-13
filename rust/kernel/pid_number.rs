// SPDX-License-Identifier: GPL-2.0

//! PID namespace number resolution.
//!
//! Rust decides whether a PID is visible in a namespace and which number it
//! has there. C helpers retain task PID slot access and RCU primitives.

use crate::bindings;
use macros::export;

type Pid = bindings::pid;
type PidNamespace = bindings::pid_namespace;
type PidType = bindings::pid_type;
type Task = bindings::task_struct;

/// Resolves a live PID into a selected PID namespace.
///
/// # Safety
///
/// `pid` and `ns`, when non-null, must remain live for the duration of the
/// call. A non-null `pid` must contain `level + 1` initialized numbers.
unsafe fn nr_ns(pid: *mut Pid, ns: *mut PidNamespace) -> bindings::pid_t {
    if pid.is_null() || ns.is_null() {
        return 0;
    }

    // SAFETY: Both pointers are live for this call.
    let level = unsafe { (*ns).level };
    // SAFETY: The live PID records the deepest namespace that can name it.
    if level > unsafe { (*pid).level } {
        return 0;
    }

    // SAFETY: The level check above selects an initialized PID number.
    let upid = unsafe { bindings::rust_helper_pid_upid(pid, level) };
    // SAFETY: A valid PID number entry and live namespace are available.
    if unsafe { (*upid).ns } != ns {
        return 0;
    }

    // SAFETY: Namespace ownership was checked above.
    unsafe { (*upid).nr }
}

/// Resolves a live PID into a selected PID namespace.
///
/// # Safety
///
/// The same requirements as [`nr_ns`] apply.
#[export]
unsafe extern "C" fn rust_pid_nr_ns(pid: *mut Pid, ns: *mut PidNamespace) -> bindings::pid_t {
    // SAFETY: The caller upholds the function requirements.
    unsafe { nr_ns(pid, ns) }
}

/// Resolves a live PID into the active PID namespace of the current task.
///
/// # Safety
///
/// A non-null `pid` must remain live for the duration of the call.
#[export]
unsafe extern "C" fn rust_pid_vnr(pid: *mut Pid) -> bindings::pid_t {
    // SAFETY: The current task has an active PID namespace.
    let current = unsafe { bindings::get_current() };
    // SAFETY: The current task is live and has a PID namespace.
    let ns = unsafe { bindings::task_active_pid_ns(current) };

    // SAFETY: The caller guarantees that the PID remains live.
    unsafe { nr_ns(pid, ns) }
}

/// Resolves a task PID of the selected type into a PID namespace.
///
/// # Safety
///
/// `task`, `type`, and `ns` must follow the contract of `__task_pid_nr_ns()`.
#[export]
unsafe extern "C" fn rust_task_pid_nr_ns(
    task: *mut Task,
    pid_type: PidType,
    ns: *mut PidNamespace,
) -> bindings::pid_t {
    let _guard = crate::sync::rcu::read_lock();
    let ns = if ns.is_null() {
        // SAFETY: The current task has an active PID namespace.
        let current = unsafe { bindings::get_current() };
        // SAFETY: The current task is live and has a PID namespace.
        unsafe { bindings::task_active_pid_ns(current) }
    } else {
        ns
    };

    // SAFETY: RCU keeps the task PID slot stable while it is read.
    let pid = unsafe { bindings::rust_helper_pid_number_task_pid(task, pid_type) };
    // SAFETY: The PID is protected by the RCU read-side critical section.
    unsafe { nr_ns(pid, ns) }
}
