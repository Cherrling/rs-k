// SPDX-License-Identifier: GPL-2.0-only

//! Native `task_work` queue and lifecycle state machine.
//!
//! Rust owns enqueue, cancellation traversal, exit sentinel handling, queue
//! detachment, callback dispatch, and reentrant draining. C helpers retain
//! task field access, locking, notification flags, diagnostics, and IRQ work.

use crate::bindings;
use core::{ffi::c_int, ptr};
use macros::export;

type Callback = bindings::callback_head;
type Task = bindings::task_struct;
type TaskWorkFunc = bindings::task_work_func_t;
type MatchFunc = Option<unsafe extern "C" fn(*mut Callback, *mut core::ffi::c_void) -> bool>;
type Notify = bindings::task_work_notify_mode;

const TWA_NMI_CURRENT: Notify = bindings::task_work_notify_mode_TWA_NMI_CURRENT;
const TWA_RESUME: Notify = bindings::task_work_notify_mode_TWA_RESUME;
const TWA_SIGNAL: Notify = bindings::task_work_notify_mode_TWA_SIGNAL;
const TWA_SIGNAL_NO_IPI: Notify = bindings::task_work_notify_mode_TWA_SIGNAL_NO_IPI;

/// Returns the address of a task's work-list head.
///
/// # Safety
///
/// `task` must point to valid task storage.
#[inline]
unsafe fn task_head(task: *mut Task) -> *mut *mut Callback {
    // SAFETY: The caller guarantees that `task` is valid.
    unsafe { bindings::rust_task_work_head_ptr(task) }
}

/// Returns the address of a work item's next pointer.
///
/// # Safety
///
/// `work` must point to valid callback storage.
#[inline]
unsafe fn work_next(work: *mut Callback) -> *mut *mut Callback {
    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { bindings::rust_task_work_next_ptr(work) }
}

/// Reads a callback-list pointer with one-copy atomicity.
///
/// # Safety
///
/// `ptr` must point to valid callback pointer storage.
#[inline]
unsafe fn read_ptr(ptr: *mut *mut Callback) -> *mut Callback {
    // SAFETY: The caller guarantees that `ptr` is valid.
    unsafe { bindings::rust_task_work_read_ptr(ptr) }
}

/// Replaces a callback pointer when it still has the expected value.
///
/// # Safety
///
/// `ptr` and `old` must point to valid callback pointer storage.
#[inline]
unsafe fn try_cmpxchg(
    ptr: *mut *mut Callback,
    old: &mut *mut Callback,
    new: *mut Callback,
) -> bool {
    // SAFETY: The caller guarantees that `ptr` and `old` are valid.
    unsafe { bindings::rust_task_work_try_cmpxchg(ptr, old, new) }
}

/// Matches a callback by function pointer.
///
/// # Safety
///
/// `head` must point to a valid callback. `data` must point to a valid
/// `TaskWorkFunc` value.
unsafe extern "C" fn match_func(head: *mut Callback, data: *mut core::ffi::c_void) -> bool {
    // SAFETY: `cancel_func` passes a pointer to a valid function pointer.
    let func = unsafe { ptr::read(data.cast::<TaskWorkFunc>()) };
    // SAFETY: The callback-list owner guarantees that `head` is valid.
    unsafe { bindings::rust_task_work_func_matches(head, func) }
}

/// Matches a callback by exact pointer identity.
///
/// # Safety
///
/// The caller must provide the two callback ABI arguments. The function
/// compares their addresses and does not dereference either pointer.
unsafe extern "C" fn match_callback(head: *mut Callback, data: *mut core::ffi::c_void) -> bool {
    head.cast::<core::ffi::c_void>() == data
}

/// Notifies a task according to the requested mode.
///
/// # Safety
///
/// `task` must point to valid task storage.
unsafe fn notify_task(task: *mut Task, notify: Notify) {
    match notify {
        TWA_RESUME => {
            // SAFETY: The caller guarantees that `task` is valid.
            unsafe { bindings::rust_task_work_notify_resume(task) };
        }
        TWA_SIGNAL => {
            // SAFETY: The caller guarantees that `task` is valid.
            unsafe { bindings::rust_task_work_notify_signal(task) };
        }
        TWA_SIGNAL_NO_IPI => {
            // SAFETY: The caller guarantees that `task` is valid.
            unsafe { bindings::rust_task_work_set_signal_only(task) };
        }
        TWA_NMI_CURRENT => {
            // SAFETY: The caller verified that `task` is current.
            unsafe { bindings::rust_task_work_notify_nmi_current() };
        }
        0 => {}
        _ => {
            // SAFETY: This helper only emits the diagnostic.
            unsafe { bindings::rust_task_work_warn_invalid_mode() };
        }
    }
}

/// Adds a callback to a task's work list.
///
/// # Safety
///
/// `task` and `work` must point to valid storage. `work` must remain allocated
/// until the callback runs or cancellation succeeds.
#[export]
unsafe extern "C" fn task_work_add(task: *mut Task, work: *mut Callback, notify: Notify) -> c_int {
    if notify == TWA_NMI_CURRENT {
        // SAFETY: This helper only reads the current task pointer.
        let current = unsafe { bindings::rust_task_work_current() };
        if task != current {
            // SAFETY: This helper only emits the diagnostic.
            unsafe { bindings::rust_task_work_warn_invalid_nmi_task() };
            return -(bindings::EINVAL as c_int);
        }
        if !cfg!(CONFIG_IRQ_WORK) {
            return -(bindings::EINVAL as c_int);
        }
    } else {
        // SAFETY: The caller guarantees that `work` is valid.
        unsafe { bindings::rust_task_work_record_aux_stack(work) };
    }

    // SAFETY: The caller guarantees that `task` is valid.
    let head = unsafe { task_head(task) };
    // SAFETY: `head` points to the task's valid work-list head.
    let mut old = unsafe { read_ptr(head) };
    loop {
        // SAFETY: This helper only reads the exit sentinel address.
        if old == unsafe { bindings::rust_task_work_exited() } {
            return -(bindings::ESRCH as c_int);
        }
        // SAFETY: The caller guarantees that `work` is valid.
        unsafe { *work_next(work) = old };
        // SAFETY: `head` and `old` point to valid callback pointer storage.
        if unsafe { try_cmpxchg(head, &mut old, work) } {
            break;
        }
    }

    // SAFETY: The caller guarantees that `task` is valid.
    unsafe { notify_task(task, notify) };
    0
}

/// Cancels the first work item that matches the supplied predicate.
///
/// # Safety
///
/// `task` must point to valid task storage. `match_fn` must remain valid for
/// the duration of the call.
#[export]
unsafe extern "C" fn task_work_cancel_match(
    task: *mut Task,
    match_fn: MatchFunc,
    data: *mut core::ffi::c_void,
) -> *mut Callback {
    let Some(matches) = match_fn else {
        return ptr::null_mut();
    };

    // SAFETY: The caller guarantees that `task` is valid.
    let head = unsafe { task_head(task) };
    // SAFETY: `head` points to the task's valid work-list head.
    if unsafe { read_ptr(head) }.is_null() {
        return ptr::null_mut();
    }

    let mut flags = 0;
    // SAFETY: The caller guarantees that `task` is valid and `flags` is writable.
    unsafe { bindings::rust_task_work_pi_lock(task, &mut flags) };
    let mut pprev = head;
    // SAFETY: `pprev` points to valid callback pointer storage.
    let mut work = unsafe { read_ptr(pprev) };
    while !work.is_null() {
        // SAFETY: `work` points to a valid callback while the queue is locked.
        if unsafe { matches(work, data) } {
            // SAFETY: The callback queue lock stabilizes `pprev` and `work`.
            let next = unsafe { read_ptr(work_next(work)) };
            let mut expected = work;
            // SAFETY: `pprev` and `expected` point to valid pointer storage.
            if unsafe { try_cmpxchg(pprev, &mut expected, next) } {
                break;
            }
            // The failed compare-exchange stored the observed pointer.
            work = expected;
        } else {
            // SAFETY: `work` points to a valid callback while the queue is locked.
            pprev = unsafe { work_next(work) };
            // SAFETY: `pprev` points to valid callback pointer storage.
            work = unsafe { read_ptr(pprev) };
        }
    }
    // SAFETY: The slow path acquired the task's PI lock.
    unsafe { bindings::rust_task_work_pi_unlock(task, flags) };

    work
}

/// Cancels the last queued work item with the supplied callback function.
///
/// # Safety
///
/// `task` must point to valid task storage.
#[export]
unsafe extern "C" fn task_work_cancel_func(task: *mut Task, func: TaskWorkFunc) -> *mut Callback {
    let mut func = func;
    // SAFETY: The caller provides the same contract for `task`.
    unsafe {
        task_work_cancel_match(
            task,
            Some(match_func),
            ptr::from_mut(&mut func).cast::<core::ffi::c_void>(),
        )
    }
}

/// Cancels a specific queued work item.
///
/// # Safety
///
/// `task` and `callback` must point to valid storage.
#[export]
unsafe extern "C" fn task_work_cancel(task: *mut Task, callback: *mut Callback) -> bool {
    // SAFETY: The caller provides the same contract for `task` and `callback`.
    unsafe {
        task_work_cancel_match(
            task,
            Some(match_callback),
            callback.cast::<core::ffi::c_void>(),
        ) == callback
    }
}

/// Detaches the current callback list from a task.
///
/// # Safety
///
/// `task` must point to valid task storage.
unsafe fn detach(task: *mut Task) -> *mut Callback {
    // SAFETY: The caller guarantees that `task` is valid.
    let head = unsafe { task_head(task) };
    // SAFETY: `head` points to the task's valid work-list head.
    let mut work = unsafe { read_ptr(head) };
    loop {
        let replacement = if work.is_null() {
            // SAFETY: This helper only reads the task exit state.
            if unsafe { bindings::rust_task_work_task_exiting(task) } {
                // SAFETY: This helper only reads the exit sentinel address.
                unsafe { bindings::rust_task_work_exited() }
            } else {
                return ptr::null_mut();
            }
        } else {
            ptr::null_mut()
        };

        // SAFETY: `head` and `work` point to valid callback pointer storage.
        if unsafe { try_cmpxchg(head, &mut work, replacement) } {
            return work;
        }
    }
}

/// Runs all pending callbacks for a task.
///
/// # Safety
///
/// `task` must remain valid for the duration of the call.
unsafe fn run_task(task: *mut Task) {
    loop {
        // SAFETY: The caller guarantees that `task` is valid.
        let mut work = unsafe { detach(task) };
        if work.is_null() {
            break;
        }

        let mut flags = 0;
        // SAFETY: The caller guarantees that `task` is valid and `flags` is writable.
        unsafe { bindings::rust_task_work_pi_lock(task, &mut flags) };
        // SAFETY: The matching unlock consumes `flags`.
        unsafe { bindings::rust_task_work_pi_unlock(task, flags) };

        loop {
            // SAFETY: The detached list owns `work` until its callback returns.
            let next = unsafe { read_ptr(work_next(work)) };
            // SAFETY: The queue only publishes callbacks with a valid function.
            let callback = unsafe { (*work).func.unwrap_unchecked() };
            // SAFETY: The callback contract permits execution from task context.
            unsafe { callback(work) };
            work = next;
            // SAFETY: This helper only requests scheduler progress.
            unsafe { bindings::rust_task_work_cond_resched() };
            if work.is_null() {
                break;
            }
        }
    }
}

/// Executes the works added by `task_work_add()`.
///
/// # Safety
///
/// The caller must execute in the context of the task whose work is drained.
/// It must permit callback execution and scheduler progress.
#[export]
unsafe extern "C" fn task_work_run() {
    // SAFETY: This helper only reads the current task pointer.
    let current = unsafe { bindings::rust_task_work_current() };
    // SAFETY: The current task remains valid for the duration of the drain.
    unsafe { run_task(current) };
}

/// Runs pending callbacks for a test task without requiring it to be current.
///
/// # Safety
///
/// `task` must remain valid for the duration of the call.
#[cfg(CONFIG_TASK_WORK_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_task_work_test_run_task(task: *mut Task) {
    // SAFETY: The caller guarantees that `task` remains valid.
    unsafe { run_task(task) };
}
