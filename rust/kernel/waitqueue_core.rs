// SPDX-License-Identifier: GPL-2.0-only

//! Native wait queue wake state machine.
//!
//! Rust owns entry ordering, entry flags, wait entry lifecycle, wait
//! preparation, task state transitions, signal rejection, finish cleanup,
//! wake traversal, callback order, exclusive wake accounting, and wake return
//! values. C helpers retain locking, intrusive list pointer updates, lockdep
//! checks, scheduler state writes, signal predicates, scheduler wake flags,
//! and poll key construction.

use crate::{
    bindings,
    ffi::{c_int, c_long, c_uint, c_void},
    sync::barrier::{smp_mb, Full},
};
use core::ptr;
use macros::export;

type Head = bindings::wait_queue_head;
type Entry = bindings::wait_queue_entry;

const WQ_FLAG_EXCLUSIVE: c_uint = bindings::WQ_FLAG_EXCLUSIVE;
const WQ_FLAG_PRIORITY: c_uint = bindings::WQ_FLAG_PRIORITY;
const WQ_FLAG_WOKEN: c_uint = bindings::WQ_FLAG_WOKEN;

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

/// Initializes a wait entry for the current wait operation.
///
/// # Safety
///
/// `wq_entry` and `task` must point to valid objects. `autoremove` must be a
/// valid wait queue callback.
#[export]
unsafe extern "C" fn rust_waitqueue_init_entry(
    wq_entry: *mut Entry,
    flags: c_uint,
    task: *mut bindings::task_struct,
    autoremove: bindings::wait_queue_func_t,
) {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe {
        (*wq_entry).flags = flags;
        (*wq_entry).private = task.cast();
        (*wq_entry).func = autoremove;
    }
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { bindings::rust_waitqueue_list_init(ptr::addr_of_mut!((*wq_entry).entry)) };
}

/// Wakes the configured task and removes the entry after a successful wake.
///
/// # Safety
///
/// `wq_entry` must point to a valid queued entry, and the wait queue lock must
/// be held.
#[export]
unsafe extern "C" fn rust_waitqueue_autoremove(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    let ret = unsafe { bindings::rust_waitqueue_default_wake(wq_entry, mode, sync, key) };
    if ret != 0 {
        // SAFETY: Wake callbacks run under the wait queue lock. The caller
        // guarantees that `wq_entry` is queued.
        unsafe {
            bindings::rust_waitqueue_list_del_init_careful(ptr::addr_of_mut!((*wq_entry).entry))
        };
    }
    ret
}

/// Waits until the entry is marked woken or the timeout expires.
///
/// # Safety
///
/// `wq_entry` must point to a valid wait entry associated with the current
/// task.
#[export]
unsafe extern "C" fn rust_waitqueue_wait_woken(
    wq_entry: *mut Entry,
    mode: c_uint,
    mut timeout: c_long,
) -> c_long {
    // SAFETY: The caller guarantees that `wq_entry` is valid. This helper
    // installs the task state with the scheduler's required barrier.
    unsafe { bindings::rust_waitqueue_set_current_state(mode) };
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    if (unsafe { (*wq_entry).flags } & WQ_FLAG_WOKEN) == 0
        // SAFETY: The scheduler predicate has no additional requirements.
        && !unsafe { bindings::kthread_should_stop_or_park() }
    {
        // SAFETY: The current task was placed in the requested wait state.
        timeout = unsafe { bindings::schedule_timeout(timeout) };
    }
    // SAFETY: The wait operation is complete. This helper updates the current
    // task state without a barrier as required by the woken wait protocol.
    unsafe { bindings::rust_waitqueue_set_current_state_relaxed(bindings::TASK_RUNNING as c_uint) };

    // SAFETY: The caller guarantees that `wq_entry` is valid.
    let flags = unsafe { (*wq_entry).flags } & !WQ_FLAG_WOKEN;
    // SAFETY: Store the flags with the release-side barrier required by the
    // woken wait protocol.
    unsafe { bindings::rust_waitqueue_store_flags_mb(wq_entry, flags) };

    timeout
}

/// Marks an entry woken, emits the pairing barrier, and wakes its task.
///
/// # Safety
///
/// `wq_entry` must point to a valid queued wait entry, and the wait queue lock
/// must be held.
unsafe fn woken_wake_common(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    // Pairs with the store barrier in `rust_waitqueue_wait_woken`.
    smp_mb(Full);
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags |= WQ_FLAG_WOKEN };
    // SAFETY: The caller guarantees that `wq_entry` has a valid task target.
    unsafe { bindings::rust_waitqueue_default_wake(wq_entry, mode, sync, key) }
}

/// Marks an entry woken and wakes its task.
///
/// # Safety
///
/// `wq_entry` must point to a valid queued wait entry, and the wait queue lock
/// must be held.
#[export]
unsafe extern "C" fn rust_waitqueue_woken_wake(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller satisfies the common woken wake requirements.
    unsafe { woken_wake_common(wq_entry, mode, sync, key) }
}

/// Marks a matching variable wait entry woken and wakes its task.
///
/// # Safety
///
/// `wq_entry` must point to a valid queued variable wait entry, `key` must be
/// a valid wake key, and the wait queue lock must be held.
#[export]
unsafe extern "C" fn rust_waitqueue_woken_wake_bit(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_entry` and `key` are valid.
    let key = unsafe { bindings::__var_wake_key(wq_entry, key) };
    if key.is_null() {
        return 0;
    }

    // SAFETY: `__var_wake_key` verified that this entry matches the wake key.
    unsafe { woken_wake_common(wq_entry, mode, sync, key.cast()) }
}

/// Prepares a normal wait entry and sets the current task state.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_prepare(
    wq_head: *mut Head,
    wq_entry: *mut Entry,
    state: c_uint,
) {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags &= !WQ_FLAG_EXCLUSIVE };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: The queue lock is held and `wq_entry` is valid.
    if unsafe { bindings::rust_waitqueue_entry_is_empty(wq_entry) } {
        // SAFETY: The queue lock is held and both pointers are valid.
        unsafe { add_locked(wq_head, wq_entry) };
    }
    // SAFETY: The queue lock is held. The helper provides the required
    // scheduler store barrier.
    unsafe { bindings::rust_waitqueue_set_current_state(state) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
}

/// Prepares an exclusive wait entry and reports whether it led an empty queue.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_prepare_exclusive(
    wq_head: *mut Head,
    wq_entry: *mut Entry,
    state: c_uint,
) -> bool {
    // SAFETY: The caller guarantees that `wq_entry` is valid.
    unsafe { (*wq_entry).flags |= WQ_FLAG_EXCLUSIVE };
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    let mut was_empty = false;
    // SAFETY: The queue lock is held and `wq_entry` is valid.
    if unsafe { bindings::rust_waitqueue_entry_is_empty(wq_entry) } {
        // SAFETY: The queue lock is held and `wq_head` is valid.
        was_empty = unsafe { bindings::rust_waitqueue_head_is_empty(wq_head) };
        // SAFETY: The queue lock is held and both pointers are valid.
        unsafe { add_tail_locked(wq_head, wq_entry) };
    }
    // SAFETY: The queue lock is held. The helper provides the required
    // scheduler store barrier.
    unsafe { bindings::rust_waitqueue_set_current_state(state) };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
    was_empty
}

/// Prepares a wait entry unless an eligible signal is pending.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_prepare_event(
    wq_head: *mut Head,
    wq_entry: *mut Entry,
    state: c_uint,
) -> c_long {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    let flags = unsafe { lock_irqsave(wq_head) };
    // SAFETY: The helper reads the state of the current task.
    let interrupted = unsafe { bindings::rust_waitqueue_signal_pending_state(state) } != 0;
    let ret = if interrupted {
        // SAFETY: `wq_entry` is valid. `list_del_init` accepts a self-linked
        // entry, which preserves the queued or unqueued state correctly.
        unsafe { bindings::rust_waitqueue_list_del_init(ptr::addr_of_mut!((*wq_entry).entry)) };
        -(bindings::ERESTARTSYS as c_long)
    } else {
        // SAFETY: The queue lock is held and `wq_entry` is valid.
        if unsafe { bindings::rust_waitqueue_entry_is_empty(wq_entry) } {
            // SAFETY: `wq_entry` is valid.
            if (unsafe { (*wq_entry).flags } & WQ_FLAG_EXCLUSIVE) != 0 {
                // SAFETY: The queue lock is held and both pointers are valid.
                unsafe { add_tail_locked(wq_head, wq_entry) };
            } else {
                // SAFETY: The queue lock is held and both pointers are valid.
                unsafe { add_locked(wq_head, wq_entry) };
            }
        }
        // SAFETY: The queue lock is held. The helper provides the required
        // scheduler store barrier.
        unsafe { bindings::rust_waitqueue_set_current_state(state) };
        0
    };
    // SAFETY: This function acquired the queue lock.
    unsafe { unlock_irqrestore(wq_head, flags) };
    ret
}

/// Restores the current task state and removes a queued wait entry.
///
/// # Safety
///
/// `wq_head` and `wq_entry` must point to valid objects.
#[export]
unsafe extern "C" fn rust_waitqueue_finish(wq_head: *mut Head, wq_entry: *mut Entry) {
    // SAFETY: The helper updates the current task state without a barrier as
    // required by the finish_wait protocol.
    unsafe { bindings::rust_waitqueue_set_current_state_relaxed(bindings::TASK_RUNNING as c_uint) };
    // SAFETY: `wq_entry` is valid. The careful emptiness check is the existing
    // lockless protocol for avoiding half-pending intrusive list updates.
    if !unsafe { bindings::rust_waitqueue_entry_is_empty_careful(wq_entry) } {
        // SAFETY: The caller guarantees that `wq_head` is valid.
        let flags = unsafe { lock_irqsave(wq_head) };
        // SAFETY: The queue lock is held and `wq_entry` is valid.
        unsafe { bindings::rust_waitqueue_list_del_init(ptr::addr_of_mut!((*wq_entry).entry)) };
        // SAFETY: This function acquired the queue lock.
        unsafe { unlock_irqrestore(wq_head, flags) };
    }
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
