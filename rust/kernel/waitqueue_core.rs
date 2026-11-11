// SPDX-License-Identifier: GPL-2.0-only

//! Native wait queue wake state machine.
//!
//! Rust owns entry ordering, entry flags, wake traversal, callback order,
//! exclusive wake accounting, and wake return values. C helpers retain
//! locking, intrusive list pointer updates, lockdep checks, scheduler wake
//! flags, and poll key construction.

use crate::{
    bindings,
    ffi::{c_int, c_uint, c_void},
};
use core::ptr;
use macros::export;

type Head = bindings::wait_queue_head;
type Entry = bindings::wait_queue_entry;

const WQ_FLAG_EXCLUSIVE: c_uint = bindings::WQ_FLAG_EXCLUSIVE;
const WQ_FLAG_PRIORITY: c_uint = bindings::WQ_FLAG_PRIORITY;

/// Acquires the wait queue lock and disables interrupts.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[inline]
unsafe fn lock_irqsave(wq_head: *mut Head) -> usize {
    let mut flags = 0;
    // SAFETY: The caller guarantees that `wq_head` is valid.
    unsafe { bindings::rust_waitqueue_lock_irqsave(wq_head, &mut flags) };
    flags
}

/// Releases the wait queue lock and restores interrupts.
///
/// # Safety
///
/// `wq_head` must be valid and the caller must own its lock.
#[inline]
unsafe fn unlock_irqrestore(wq_head: *mut Head, flags: usize) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_waitqueue_unlock_irqrestore(wq_head, flags) };
}

/// Inserts an entry after the priority prefix while the queue lock is held.
///
/// # Safety
///
/// `wq_head` and `entry` must be valid and the queue lock must be held.
unsafe fn add_priority_locked(wq_head: *mut Head, entry: *mut Entry) {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let mut current = unsafe { bindings::rust_waitqueue_first_entry(wq_head) };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let mut position: *mut bindings::list_head = unsafe { ptr::addr_of_mut!((*wq_head).head) };

    // SAFETY: `wq_head` is valid and `current` is either its list head or a
    // queued entry reached from that list.
    while !unsafe { bindings::rust_waitqueue_entry_is_head(wq_head, current) } {
        // SAFETY: The loop condition established that `current` is queued.
        if (unsafe { (*current).flags } & WQ_FLAG_PRIORITY) == 0 {
            break;
        }
        // SAFETY: The loop condition established that `current` is queued.
        position = unsafe { ptr::addr_of_mut!((*current).entry) };
        // SAFETY: The queue lock is held and `current` is not the list head.
        current = unsafe { bindings::rust_waitqueue_next_entry(current) };
    }

    // SAFETY: The caller guarantees that `entry` is valid. `position` points
    // to the queue head or a queued entry under the same lock.
    unsafe { bindings::rust_waitqueue_list_add(position, ptr::addr_of_mut!((*entry).entry)) };
}

/// Adds an entry after any priority entries, without changing its flags.
///
/// # Safety
///
/// `wq_head` and `entry` must be valid and the queue lock must be held.
unsafe fn add_locked(wq_head: *mut Head, entry: *mut Entry) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_waitqueue_assert_locked(wq_head) };
    // SAFETY: The caller guarantees that both pointers are valid.
    unsafe { add_priority_locked(wq_head, entry) };
}

/// Adds an entry to the tail without changing its flags.
///
/// # Safety
///
/// `wq_head` and `entry` must be valid and the queue lock must be held.
unsafe fn add_tail_locked(wq_head: *mut Head, entry: *mut Entry) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_waitqueue_assert_locked(wq_head) };
    // SAFETY: The caller guarantees that both pointers are valid. The queue
    // lock protects the list head and the new entry.
    unsafe {
        bindings::rust_waitqueue_list_add_tail(
            ptr::addr_of_mut!((*wq_head).head),
            ptr::addr_of_mut!((*entry).entry),
        )
    };
}

/// Removes an entry while the queue lock is held.
///
/// # Safety
///
/// `wq_head` and `entry` must be valid and the queue lock must be held.
unsafe fn remove_locked(wq_head: *mut Head, entry: *mut Entry) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { bindings::rust_waitqueue_assert_locked(wq_head) };
    // SAFETY: The caller guarantees that `entry` is queued on `wq_head`.
    unsafe { bindings::rust_waitqueue_list_del(ptr::addr_of_mut!((*entry).entry)) };
}

/// Adds a normal entry after the priority prefix.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_add(wq_head: *mut Head, wq_entry: *mut Entry) {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags &= !WQ_FLAG_EXCLUSIVE };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: This function acquired the queue lock and both pointers are
    // valid.
    unsafe { add_locked(wq_head, wq_entry) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
}

/// Adds an exclusive entry to the queue tail.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_add_exclusive(wq_head: *mut Head, wq_entry: *mut Entry) {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags |= WQ_FLAG_EXCLUSIVE };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: This function acquired the queue lock and both pointers are
    // valid.
    unsafe { add_tail_locked(wq_head, wq_entry) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
}

/// Adds a priority entry after the existing priority prefix.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_add_priority(wq_head: *mut Head, wq_entry: *mut Entry) {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags |= WQ_FLAG_PRIORITY };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: This function acquired the queue lock and both pointers are
    // valid.
    unsafe { add_locked(wq_head, wq_entry) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
}

/// Adds one exclusive priority entry at the queue head.
///
/// This operation fails if another priority entry already leads the queue.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_add_priority_exclusive(
    wq_head: *mut Head,
    wq_entry: *mut Entry,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags |= WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: `wq_head` is valid.
    let first = unsafe { bindings::rust_waitqueue_first_entry(wq_head) };
    // SAFETY: `wq_head` is valid and `first` is either its list head or a
    // queued entry.
    let busy = !unsafe { bindings::rust_waitqueue_entry_is_head(wq_head, first) }
        && (unsafe { (*first).flags } & WQ_FLAG_PRIORITY) != 0;
    let ret = if busy {
        -(bindings::EBUSY as c_int)
    } else {
        // SAFETY: The queue lock is held and both pointers are valid.
        unsafe {
            bindings::rust_waitqueue_list_add(
                ptr::addr_of_mut!((*wq_head).head),
                ptr::addr_of_mut!((*wq_entry).entry),
            )
        };
        0
    };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
    ret
}

/// Removes an entry from a queue under its lock.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_remove(wq_head: *mut Head, wq_entry: *mut Entry) {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: This function acquired the queue lock and both pointers are
    // valid.
    unsafe { remove_locked(wq_head, wq_entry) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
}

/// Runs all eligible callbacks while the wait queue lock is held.
///
/// # Safety
///
/// `wq_head` must be valid and its lock must be held. All queued entries and
/// callbacks must remain valid for the duration of this call.
unsafe fn wake_common(
    wq_head: *mut Head,
    mode: c_uint,
    mut nr_exclusive: c_int,
    wake_flags: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that the wait queue lock is held.
    unsafe { bindings::rust_waitqueue_assert_locked(wq_head) };

    // SAFETY: The caller guarantees that `wq_head` is valid.
    let mut current = unsafe { bindings::rust_waitqueue_first_entry(wq_head) };

    // SAFETY: `wq_head` is valid and `current` is either its list head or a
    // queued entry reached from that list.
    while !unsafe { bindings::rust_waitqueue_entry_is_head(wq_head, current) } {
        // SAFETY: The queue lock is held and `current` is not the list head.
        let next = unsafe { bindings::rust_waitqueue_next_entry(current) };
        // SAFETY: `current` is a queued entry.
        let flags = unsafe { (*current).flags };
        // SAFETY: `current` is a queued entry with a valid callback.
        let func = unsafe { (*current).func };
        // A valid queued entry has a callback. A missing callback cannot wake
        // this entry and must not stop traversal.
        let ret = match func {
            // SAFETY: The callback receives the queued entry and its wake data.
            Some(func) => unsafe { func(current, mode, wake_flags, key) },
            None => 0,
        };

        if ret < 0 {
            break;
        }

        if ret != 0 && (flags & bindings::WQ_FLAG_EXCLUSIVE) != 0 {
            nr_exclusive = nr_exclusive.wrapping_sub(1);
            if nr_exclusive == 0 {
                break;
            }
        }

        current = next;
    }

    nr_exclusive
}

/// Runs the common wake path under the wait queue lock.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
unsafe fn wake_common_locked(
    wq_head: *mut Head,
    mode: c_uint,
    nr_exclusive: c_int,
    wake_flags: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: This function acquired the wait queue lock.
    let remaining = unsafe { wake_common(wq_head, mode, nr_exclusive, wake_flags, key) };
    // SAFETY: This function acquired the wait queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };

    nr_exclusive.wrapping_sub(remaining)
}

/// Wakes waiters with an explicit exclusive wake budget.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[export]
unsafe extern "C" fn rust_waitqueue_wake(
    wq_head: *mut Head,
    mode: c_uint,
    nr_exclusive: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    unsafe { wake_common_locked(wq_head, mode, nr_exclusive, 0, key) }
}

/// Wakes one waiter with a preference for the current CPU.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_on_current_cpu(
    wq_head: *mut Head,
    mode: c_uint,
    key: *mut c_void,
) {
    // SAFETY: The scheduler helper has no additional requirements.
    let wake_flags = unsafe { bindings::rust_waitqueue_current_cpu_wake_flags() };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    unsafe { wake_common_locked(wq_head, mode, 1, wake_flags, key) };
}

/// Wakes waiters while the caller holds the wait queue lock.
///
/// # Safety
///
/// `wq_head` must be valid and its lock must be held.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_locked(wq_head: *mut Head, mode: c_uint, nr: c_int) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { wake_common(wq_head, mode, nr, 0, ptr::null_mut()) };
}

/// Wakes one keyed waiter while the caller holds the wait queue lock.
///
/// # Safety
///
/// `wq_head` must be valid and its lock must be held.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_locked_key(
    wq_head: *mut Head,
    mode: c_uint,
    key: *mut c_void,
) {
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { wake_common(wq_head, mode, 1, 0, key) };
}

/// Wakes one keyed waiter with synchronized wake semantics.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_sync_key(
    wq_head: *mut Head,
    mode: c_uint,
    key: *mut c_void,
) {
    if wq_head.is_null() {
        return;
    }

    // SAFETY: The scheduler helper has no additional requirements.
    let wake_flags = unsafe { bindings::rust_waitqueue_sync_wake_flags() };
    // SAFETY: The pointer was checked above.
    unsafe { wake_common_locked(wq_head, mode, 1, wake_flags, key) };
}

/// Wakes one keyed waiter with synchronized semantics under the queue lock.
///
/// # Safety
///
/// `wq_head` must be valid and its lock must be held.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_locked_sync_key(
    wq_head: *mut Head,
    mode: c_uint,
    key: *mut c_void,
) {
    // SAFETY: The scheduler helper has no additional requirements.
    let wake_flags = unsafe { bindings::rust_waitqueue_sync_wake_flags() };
    // SAFETY: The caller guarantees that the lock is held.
    unsafe { wake_common(wq_head, mode, 1, wake_flags, key) };
}

/// Wakes one waiter with synchronized wake semantics.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_sync(wq_head: *mut Head, mode: c_uint) {
    if wq_head.is_null() {
        return;
    }

    // SAFETY: The scheduler helper has no additional requirements.
    let wake_flags = unsafe { bindings::rust_waitqueue_sync_wake_flags() };
    // SAFETY: The pointer was checked above.
    unsafe { wake_common_locked(wq_head, mode, 1, wake_flags, ptr::null_mut()) };
}

/// Wakes poll waiters when the underlying object disappears.
///
/// # Safety
///
/// `wq_head` must point to a valid wait queue.
#[export]
unsafe extern "C" fn rust_waitqueue_wake_pollfree(wq_head: *mut Head) {
    // SAFETY: The poll helper has no additional requirements.
    let key = unsafe { bindings::rust_waitqueue_pollfree_key() };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    unsafe { wake_common_locked(wq_head, bindings::TASK_NORMAL, 0, 0, key) };
    // SAFETY: Poll callbacks must remove themselves from the queue.
    unsafe { bindings::rust_waitqueue_assert_pollfree_empty(wq_head) };
}
