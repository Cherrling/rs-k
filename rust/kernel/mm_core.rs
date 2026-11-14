// SPDX-License-Identifier: GPL-2.0

//! Native `mm_users` lifetime and task address-space acquisition.
//!
//! Rust owns the task-mm acquisition decision, atomic `mm_users` updates, the
//! synchronous finalization decision, and asynchronous finalization queueing.
//! C retains task locking, the final address-space teardown, and workqueue
//! primitives.

use crate::bindings;
use core::sync::atomic::{AtomicI32, Ordering};
use macros::export;

type Mm = bindings::mm_struct;
type Task = bindings::task_struct;
type MmUsers = AtomicI32;

crate::static_assert!(
    core::mem::size_of::<MmUsers>() == core::mem::size_of::<bindings::atomic_t>()
);
crate::static_assert!(
    core::mem::align_of::<MmUsers>() == core::mem::align_of::<bindings::atomic_t>()
);

const GET_ACTION_NONE: u32 = bindings::RUST_MM_GET_ACTION_NONE;
const GET_ACTION_ACQUIRE: u32 = bindings::RUST_MM_GET_ACTION_ACQUIRE;

const PUT_ACTION_RETURN: u32 = bindings::RUST_MM_PUT_ACTION_RETURN;
const PUT_ACTION_FINALIZE: u32 = bindings::RUST_MM_PUT_ACTION_FINALIZE;
const PUT_ACTION_ASYNC: u32 = bindings::RUST_MM_PUT_ACTION_ASYNC;

/// Selects whether a task address space must be acquired.
fn get_task_plan(kernel_thread: bool, has_mm: bool) -> u32 {
    if kernel_thread || !has_mm {
        GET_ACTION_NONE
    } else {
        GET_ACTION_ACQUIRE
    }
}

/// Returns the action for a task address-space acquisition attempt.
#[cfg(CONFIG_KUNIT)]
#[export]
extern "C" fn rust_mm_get_task_plan_select(kernel_thread: bool, has_mm: bool) -> u32 {
    get_task_plan(kernel_thread, has_mm)
}

/// Selects the action after one `mm_users` reference is dropped.
fn put_plan(became_zero: bool, asynchronous: bool) -> u32 {
    if !became_zero {
        PUT_ACTION_RETURN
    } else if asynchronous {
        PUT_ACTION_ASYNC
    } else {
        PUT_ACTION_FINALIZE
    }
}

/// Returns the action for one `mm_users` release.
#[cfg(CONFIG_KUNIT)]
#[export]
extern "C" fn rust_mm_put_plan_select(became_zero: bool, asynchronous: bool) -> u32 {
    put_plan(became_zero, asynchronous)
}

/// Returns the atomic user-count field.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct` for the duration of the
/// returned pointer's use.
unsafe fn mm_users(mm: *mut Mm) -> *mut MmUsers {
    // SAFETY: The C helper obtains the address of the live `atomic_t` field.
    unsafe { bindings::rust_helper_mm_users_ptr(mm).cast() }
}

/// Increments `mm_users` with the full ordering used by `atomic_inc()`.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct`.
pub(crate) unsafe fn get(mm: *mut Mm) {
    // SAFETY: The caller holds a live address-space reference.
    unsafe { (*mm_users(mm)).fetch_add(1, Ordering::SeqCst) };
}

/// Decrements `mm_users` and reports whether it became zero.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct` with a reference owned by the
/// caller.
unsafe fn mmput_dec_and_test(mm: *mut Mm) -> bool {
    // SAFETY: The caller holds a live address-space reference.
    unsafe { (*mm_users(mm)).fetch_sub(1, Ordering::SeqCst) == 1 }
}

/// Finishes an `mm_users` release after the count reached zero.
///
/// # Safety
///
/// `mm` must no longer have user references. The synchronous path must run in
/// sleepable context. The asynchronous path may run in atomic context.
unsafe fn finish_put(mm: *mut Mm, asynchronous: bool) {
    let plan = put_plan(true, asynchronous);

    if plan & PUT_ACTION_ASYNC != 0 {
        // SAFETY: The caller transferred the final user reference.
        unsafe { bindings::rust_helper_mm_put_async_schedule(mm) };
    } else {
        debug_assert_ne!(plan & PUT_ACTION_FINALIZE, 0);
        #[cfg(CONFIG_RUST_MM_TEARDOWN)]
        // SAFETY: The caller transferred the final user reference.
        unsafe {
            crate::mm_teardown::run(mm)
        };
        #[cfg(not(CONFIG_RUST_MM_TEARDOWN))]
        // SAFETY: The caller transferred the final user reference.
        unsafe {
            bindings::rust_helper_mm_put_final(mm)
        };
    }
}

/// Acquires the task's address space unless the task is a kernel thread.
///
/// # Safety
///
/// `task` must be a live task reference for the duration of this call.
#[export]
unsafe extern "C" fn rust_mm_get_task(task: *mut Task) -> *mut Mm {
    // SAFETY: The caller supplies a live task.
    let kernel_thread = unsafe { bindings::rust_helper_mm_task_is_kernel_thread(task) };
    if get_task_plan(kernel_thread, true) == GET_ACTION_NONE {
        return core::ptr::null_mut();
    }

    // SAFETY: The task remains live and task_lock serializes `task->mm`.
    unsafe { bindings::rust_helper_mm_task_lock(task) };
    // SAFETY: The lock is held while the optional mm pointer is collected.
    let mm = unsafe { bindings::rust_helper_mm_task_mm(task) };
    let plan = get_task_plan(false, !mm.is_null());
    if plan & GET_ACTION_ACQUIRE != 0 {
        // SAFETY: The task lock keeps the mm alive while its user count grows.
        unsafe { get(mm) };
    }
    // SAFETY: This call balances the successful lock above.
    unsafe { bindings::rust_helper_mm_task_unlock(task) };

    mm
}

/// Releases one synchronous `mm_users` reference.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct` and the caller must own one
/// user reference. The call must occur in sleepable context.
pub(crate) unsafe fn put_sync(mm: *mut Mm) {
    // SAFETY: The caller owns one live user reference.
    let became_zero = unsafe { mmput_dec_and_test(mm) };
    let plan = put_plan(became_zero, false);

    if plan & PUT_ACTION_RETURN == 0 {
        debug_assert_ne!(plan & PUT_ACTION_FINALIZE, 0);
        // SAFETY: This call consumes the caller's final user reference.
        unsafe { finish_put(mm, false) };
    }
}

/// Releases one synchronous `mm_users` reference.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct` and the caller must own one
/// user reference. The call must occur in sleepable context.
#[export]
unsafe extern "C" fn rust_mm_put(mm: *mut Mm) {
    // SAFETY: The caller owns one live user reference.
    unsafe { put_sync(mm) };
}

/// Releases one `mm_users` reference and queues final teardown when needed.
///
/// # Safety
///
/// `mm` must point to a live `struct mm_struct` and the caller must own one
/// user reference.
#[export]
unsafe extern "C" fn rust_mm_put_async(mm: *mut Mm) {
    // SAFETY: The caller owns one live user reference.
    let became_zero = unsafe { mmput_dec_and_test(mm) };
    let plan = put_plan(became_zero, true);

    if plan & PUT_ACTION_ASYNC != 0 {
        // SAFETY: This call consumes the caller's final user reference.
        unsafe { finish_put(mm, true) };
    } else {
        debug_assert_ne!(plan & PUT_ACTION_RETURN, 0);
    }
}
