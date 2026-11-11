// SPDX-License-Identifier: GPL-2.0-only

//! Native state machine for simple wait queues.
//!
//! Rust owns queue membership, wake selection, lock transitions, signal
//! handling, and completion of a wait.  C helpers retain raw locking, task
//! state, wakeup, and intrusive list primitives.

use crate::{
    bindings,
    ffi::{c_char, c_int, c_long, c_uint},
};
use core::{mem::MaybeUninit, ptr};
use macros::export;

type Head = bindings::swait_queue_head;
type Wait = bindings::swait_queue;
type ListHead = bindings::list_head;
type RawSpinLock = bindings::raw_spinlock_t;
type LockClassKey = bindings::lock_class_key;

/// Returns the address of the queue task list.
///
/// # Safety
///
/// `q` must point to a valid queue head.
#[inline]
unsafe fn queue_list(q: *mut Head) -> *mut ListHead {
    // SAFETY: The caller guarantees that `q` is valid.
    unsafe { ptr::addr_of_mut!((*q).task_list) }
}

/// Returns the address of the wait entry list node.
///
/// # Safety
///
/// `wait` must point to a valid wait entry.
#[inline]
unsafe fn wait_list(wait: *mut Wait) -> *mut ListHead {
    // SAFETY: The caller guarantees that `wait` is valid.
    unsafe { ptr::addr_of_mut!((*wait).task_list) }
}

/// Returns the address of the queue lock.
///
/// # Safety
///
/// `q` must point to a valid queue head.
#[inline]
unsafe fn lock(q: *mut Head) -> *mut RawSpinLock {
    // SAFETY: The caller guarantees that `q` is valid.
    unsafe { ptr::addr_of_mut!((*q).lock) }
}

/// Reports whether a list is empty.
///
/// # Safety
///
/// `list` must point to an initialized list head.
#[inline]
unsafe fn list_empty(list: *mut ListHead) -> bool {
    // SAFETY: The caller guarantees that `list` is a valid list head.
    unsafe { bindings::rust_swait_list_empty(list) }
}

/// Reports whether a list is empty without a stable same-CPU observation.
///
/// # Safety
///
/// `list` must point to an initialized list head.
#[inline]
unsafe fn list_empty_careful(list: *mut ListHead) -> bool {
    // SAFETY: The caller guarantees that `list` is a valid list head.
    unsafe { bindings::rust_swait_list_empty_careful(list) }
}

/// Returns the first wait entry in a non-empty list.
///
/// # Safety
///
/// `list` must be non-empty and contain only initialized wait entries.
#[inline]
unsafe fn first_wait(list: *mut ListHead) -> *mut Wait {
    // SAFETY: The caller guarantees that `list` is non-empty and that every
    // entry is an initialized `swait_queue`.
    unsafe { bindings::rust_swait_first_wait(list) }
}

/// Removes a wait entry and reinitializes its list node.
///
/// # Safety
///
/// `wait` must point to an initialized wait entry with valid list links.
#[inline]
unsafe fn delete_wait(wait: *mut Wait) {
    // SAFETY: The caller guarantees that `wait` is valid and initialized.
    unsafe { bindings::rust_swait_list_del_init(wait_list(wait)) };
}

/// Wakes the first queued task.
///
/// # Safety
///
/// `q` must be valid and its lock must be held.
#[inline]
unsafe fn wake_locked(q: *mut Head, wake_flags: c_int) {
    // SAFETY: The caller guarantees that `q` is valid.
    let list = unsafe { queue_list(q) };
    // SAFETY: The caller guarantees that the queue lock is held.
    if unsafe { list_empty(list) } {
        return;
    }

    // SAFETY: The list is non-empty and contains initialized wait entries.
    let wait = unsafe { first_wait(list) };
    // SAFETY: A queued wait entry has a valid task pointer.
    let task = unsafe { ptr::read(ptr::addr_of!((*wait).task)) };
    // SAFETY: The task pointer came from the queued wait entry.
    unsafe { bindings::rust_swait_try_to_wake_up(task, bindings::TASK_NORMAL, wake_flags) };
    // SAFETY: The entry was initialized and is still on the queue.
    unsafe { bindings::rust_swait_list_del_init(wait_list(wait)) };
}

/// Prepares a wait entry while the queue lock is held.
///
/// # Safety
///
/// `q` and `wait` must be valid.  The queue lock must be held.
#[inline]
unsafe fn prepare_locked(q: *mut Head, wait: *mut Wait) {
    // SAFETY: The caller guarantees that `wait` is valid.
    unsafe { (*wait).task = bindings::get_current() };
    // SAFETY: The caller guarantees that `wait` is valid.
    let list = unsafe { wait_list(wait) };
    // SAFETY: The caller guarantees that the queue lock is held.
    if unsafe { list_empty(list) } {
        // SAFETY: Both list heads are valid.
        unsafe { bindings::rust_swait_list_add_tail(list, queue_list(q)) };
    }
}

/// Initializes a simple wait queue head.
///
/// # Safety
///
/// `q`, `name`, and `key` must be valid for the lifetime of the queue.
#[export]
unsafe extern "C" fn rust_swait_init_queue_head(
    q: *mut Head,
    name: *const c_char,
    key: *mut LockClassKey,
) {
    // SAFETY: The caller guarantees that `q` is valid.
    let lock = unsafe { lock(q) };
    // SAFETY: The caller guarantees that the lock is valid and writable.
    unsafe { bindings::rust_swait_raw_spin_lock_init(lock) };
    // SAFETY: The caller guarantees that all pointers are valid.
    unsafe { bindings::rust_swait_lockdep_set_class_and_name(lock, key, name) };
    // SAFETY: The caller guarantees that `q` is valid.
    unsafe { bindings::rust_swait_list_init(queue_list(q)) };
}

/// Wakes one waiter while the caller holds the queue lock.
///
/// # Safety
///
/// `q` must be valid and its lock must be held.
#[export]
unsafe extern "C" fn rust_swait_wake_locked(q: *mut Head, wake_flags: c_int) {
    // SAFETY: The caller guarantees that the queue lock is held.
    unsafe { wake_locked(q, wake_flags) };
}

/// Wakes all waiters while the caller holds the queue lock.
///
/// # Safety
///
/// `q` must be valid and its lock must be held.
#[export]
unsafe extern "C" fn rust_swait_wake_all_locked(q: *mut Head) {
    // SAFETY: The caller guarantees that the queue lock is held.
    while !unsafe { list_empty(queue_list(q)) } {
        // SAFETY: The queue lock remains held.
        unsafe { wake_locked(q, 0) };
    }
}

/// Wakes one waiter under the queue lock.
///
/// # Safety
///
/// `q` must be valid.
#[export]
unsafe extern "C" fn rust_swait_wake_one(q: *mut Head) {
    let mut flags: usize = 0;
    // SAFETY: The caller guarantees that `q` and its lock are valid.
    let lock = unsafe { lock(q) };
    // SAFETY: The caller guarantees that `q` is valid.
    unsafe { bindings::rust_swait_raw_spin_lock_irqsave(lock, &mut flags) };
    // SAFETY: The queue lock is held.
    unsafe { wake_locked(q, 0) };
    // SAFETY: This function acquired the lock above.
    unsafe { bindings::rust_swait_raw_spin_unlock_irqrestore(lock, flags) };
}

/// Wakes all waiters with bounded lock hold time.
///
/// # Safety
///
/// `q` must be valid and the caller must permit interrupt disabling.
#[export]
unsafe extern "C" fn rust_swait_wake_all(q: *mut Head) {
    let mut tmp = MaybeUninit::<ListHead>::uninit();
    // SAFETY: The temporary list is initialized in place before use.
    unsafe { bindings::rust_swait_list_init(tmp.as_mut_ptr()) };
    // SAFETY: The list was initialized above.  Keep it in place because the
    // list head contains pointers to itself.
    let tmp = unsafe { tmp.assume_init_mut() };
    // SAFETY: The caller guarantees that `q` and its lock are valid.
    let lock = unsafe { lock(q) };

    // SAFETY: The caller guarantees that interrupt disabling is permitted.
    unsafe { bindings::rust_swait_raw_spin_lock_irq(lock) };
    // SAFETY: Both list heads are valid and the queue lock is held.
    unsafe { bindings::rust_swait_list_splice_init(queue_list(q), tmp) };

    loop {
        // SAFETY: The temporary list is initialized.
        if unsafe { list_empty(tmp) } {
            break;
        }

        // SAFETY: The temporary list is non-empty.
        let wait = unsafe { first_wait(tmp) };
        // SAFETY: A queued wait entry has a valid task pointer.
        let task = unsafe { ptr::read(ptr::addr_of!((*wait).task)) };
        // SAFETY: The task pointer came from the queued wait entry.
        unsafe { bindings::rust_swait_try_to_wake_up(task, bindings::TASK_NORMAL, 0) };
        // SAFETY: The entry is still on the temporary list.
        unsafe { bindings::rust_swait_list_del_init(wait_list(wait)) };

        // SAFETY: The temporary list is initialized.
        if unsafe { list_empty(tmp) } {
            break;
        }

        // SAFETY: This function acquired the queue lock above.
        unsafe { bindings::rust_swait_raw_spin_unlock_irq(lock) };
        // SAFETY: The caller permits interrupt disabling.
        unsafe { bindings::rust_swait_raw_spin_lock_irq(lock) };
    }

    // SAFETY: This function acquired the queue lock above.
    unsafe { bindings::rust_swait_raw_spin_unlock_irq(lock) };
}

/// Adds a wait entry while the queue lock is held.
///
/// # Safety
///
/// `q` and `wait` must be valid.  The queue lock must be held.
#[export]
unsafe extern "C" fn rust_swait_prepare_locked(q: *mut Head, wait: *mut Wait) {
    // SAFETY: The caller guarantees that the queue lock is held.
    unsafe { prepare_locked(q, wait) };
}

/// Prepares an exclusive wait and sets the task state.
///
/// # Safety
///
/// `q` and `wait` must be valid and the caller must permit interrupt
/// disabling.
#[export]
unsafe extern "C" fn rust_swait_prepare_exclusive(q: *mut Head, wait: *mut Wait, state: c_int) {
    let mut flags: usize = 0;
    // SAFETY: The caller guarantees that `q` and its lock are valid.
    let lock = unsafe { lock(q) };
    // SAFETY: The caller permits interrupt disabling.
    unsafe { bindings::rust_swait_raw_spin_lock_irqsave(lock, &mut flags) };
    // SAFETY: The queue lock is held.
    unsafe { prepare_locked(q, wait) };
    // SAFETY: The caller guarantees that changing current is valid.
    unsafe { bindings::rust_swait_set_current_state(state as c_uint) };
    // SAFETY: This function acquired the lock above.
    unsafe { bindings::rust_swait_raw_spin_unlock_irqrestore(lock, flags) };
}

/// Prepares an event wait or reports a pending signal.
///
/// # Safety
///
/// `q` and `wait` must be valid and the caller must permit interrupt
/// disabling.
#[export]
unsafe extern "C" fn rust_swait_prepare_event(
    q: *mut Head,
    wait: *mut Wait,
    state: c_int,
) -> c_long {
    let mut flags: usize = 0;
    // SAFETY: The caller guarantees that `q` and its lock are valid.
    let lock = unsafe { lock(q) };
    // SAFETY: The caller permits interrupt disabling.
    unsafe { bindings::rust_swait_raw_spin_lock_irqsave(lock, &mut flags) };

    // SAFETY: The caller guarantees that changing current is valid.
    let signaled = unsafe {
        bindings::rust_swait_signal_pending_state(state as c_uint, bindings::get_current())
    } != 0;
    let ret = if signaled {
        // SAFETY: The wait entry is valid and initialized.
        unsafe { delete_wait(wait) };
        -(bindings::ERESTARTSYS as c_long)
    } else {
        // SAFETY: The queue lock is held.
        unsafe { prepare_locked(q, wait) };
        // SAFETY: The caller guarantees that changing current is valid.
        unsafe { bindings::rust_swait_set_current_state(state as c_uint) };
        0
    };

    // SAFETY: This function acquired the lock above.
    unsafe { bindings::rust_swait_raw_spin_unlock_irqrestore(lock, flags) };
    ret
}

/// Completes a wait while the caller holds the queue lock.
///
/// # Safety
///
/// `q` and `wait` must be valid and the caller must hold the queue lock.
#[export]
unsafe extern "C" fn rust_swait_finish_locked(q: *mut Head, wait: *mut Wait) {
    let _ = q;
    // SAFETY: The caller guarantees that changing current is valid.
    unsafe { bindings::rust_swait_set_current_state_relaxed(bindings::TASK_RUNNING) };
    // SAFETY: The caller guarantees that `wait` is valid.
    if !unsafe { list_empty(wait_list(wait)) } {
        // SAFETY: The entry is queued.
        unsafe { delete_wait(wait) };
    }
}

/// Completes a wait and removes an outstanding queue entry.
///
/// # Safety
///
/// `q` and `wait` must be valid.
#[export]
unsafe extern "C" fn rust_swait_finish(q: *mut Head, wait: *mut Wait) {
    // SAFETY: The caller guarantees that changing current is valid.
    unsafe { bindings::rust_swait_set_current_state_relaxed(bindings::TASK_RUNNING) };

    // SAFETY: The caller guarantees that `wait` is valid.
    if unsafe { list_empty_careful(wait_list(wait)) } {
        return;
    }

    let mut flags: usize = 0;
    // SAFETY: The caller guarantees that `q` and its lock are valid.
    let lock = unsafe { lock(q) };
    // SAFETY: The caller permits interrupt disabling.
    unsafe { bindings::rust_swait_raw_spin_lock_irqsave(lock, &mut flags) };
    // SAFETY: The caller guarantees that `wait` is valid.
    unsafe { bindings::rust_swait_list_del_init(wait_list(wait)) };
    // SAFETY: This function acquired the lock above.
    unsafe { bindings::rust_swait_raw_spin_unlock_irqrestore(lock, flags) };
}
