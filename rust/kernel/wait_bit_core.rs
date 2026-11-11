// SPDX-License-Identifier: GPL-2.0-only

//! Native bit and variable wait state machines.
//!
//! Rust owns shared queue selection, bit and variable key matching, wait entry
//! construction, normal and exclusive wait loops, lock acquisition, timed wait
//! setup, and wake submission. C helpers retain shared table storage, hashing,
//! atomic bit operations, current task and jiffies reads, scheduler actions,
//! and public ABI adapters.

use crate::{
    bindings,
    ffi::{c_int, c_uint, c_ulong, c_void},
};
use core::{mem::MaybeUninit, ptr};
use macros::export;

type Head = bindings::wait_queue_head;
type Entry = bindings::wait_queue_entry;
type BitEntry = bindings::wait_bit_queue_entry;
type Key = bindings::wait_bit_key;

/// Returns the shared queue selected by a bit wait key.
///
/// # Safety
///
/// `word` is used only as an address and need not be readable.
#[export]
unsafe extern "C" fn rust_wait_bit_waitqueue(word: *mut c_ulong, bit: c_int) -> *mut Head {
    let shift = if usize::BITS == 32 { 5 } else { 6 };
    let value = (word as usize) << shift | bit as usize;
    // SAFETY: The shared wait queue table is initialized before bit waits are
    // used, and the hash helper returns an in-range table index.
    unsafe { rust_wait_bit_waitqueue_hash(bindings::rust_wait_bit_hash(value)) }
}

/// Returns the shared queue selected by a variable wait key.
///
/// # Safety
///
/// `var` is used only as an address and need not be readable.
#[export]
unsafe extern "C" fn rust_wait_bit_var_waitqueue(var: *mut c_void) -> *mut Head {
    // SAFETY: The shared wait queue table is initialized before variable waits
    // are used, and the hash helper returns an in-range table index.
    unsafe { rust_wait_bit_waitqueue_hash(bindings::rust_wait_bit_hash(var as usize)) }
}

/// Selects a shared wait queue by hash.
///
/// # Safety
///
/// The bit wait table must be initialized, and `hash` must be in range.
#[inline]
unsafe fn rust_wait_bit_waitqueue_hash(hash: c_uint) -> *mut Head {
    // SAFETY: The caller guarantees that the table is initialized.
    let table = unsafe { bindings::rust_wait_bit_table() };
    // SAFETY: The caller guarantees that `hash` selects an entry in the table.
    unsafe { table.add(hash as usize) }
}

/// Returns the containing bit wait entry for a wait queue entry.
///
/// # Safety
///
/// `wq_entry` must point to the embedded entry of a valid bit wait entry.
#[inline]
unsafe fn bit_entry(wq_entry: *mut Entry) -> *mut BitEntry {
    let offset = core::mem::offset_of!(BitEntry, wq_entry);
    // SAFETY: The caller guarantees that `wq_entry` is embedded in a bit wait
    // entry at the field offset described by `offset`.
    unsafe { wq_entry.cast::<u8>().sub(offset).cast() }
}

/// Compares a bit wait entry key with a wake key.
///
/// # Safety
///
/// Both pointers must point to valid keys.
#[inline]
unsafe fn key_matches(left: *const Key, right: *const Key) -> bool {
    // SAFETY: The caller guarantees that both pointers are valid.
    let same_address = unsafe { (*left).flags == (*right).flags };
    // SAFETY: The caller guarantees that both pointers are valid.
    let same_bit = unsafe { (*left).bit_nr == (*right).bit_nr };
    same_address && same_bit
}

/// Calls a non-null bit wait action.
///
/// # Safety
///
/// `action` must be non-null and accept `key` with the requested scheduler
/// mode according to its contract.
#[inline]
unsafe fn call_action(action: bindings::wait_bit_action_f, key: *mut Key, mode: c_uint) -> c_int {
    // SAFETY: The caller guarantees that the action is non-null.
    let action = unsafe { action.unwrap_unchecked() };
    // SAFETY: The caller guarantees that the action accepts these arguments.
    unsafe { action(key, mode as c_int) }
}

/// Initializes a bit wait entry.
///
/// # Safety
///
/// `wbq_entry` must point to writable storage for a bit wait entry.
#[inline]
unsafe fn init_bit_entry(wbq_entry: *mut BitEntry, word: *mut c_ulong, bit: c_int) {
    // SAFETY: The caller guarantees that the output storage is writable.
    unsafe {
        (*wbq_entry).key = Key {
            flags: word,
            bit_nr: bit,
            timeout: 0,
        };
        (*wbq_entry).wq_entry = Entry::default();
        (*wbq_entry).wq_entry.private = bindings::rust_wait_bit_current_task();
        (*wbq_entry).wq_entry.func = Some(rust_wait_bit_wake_bit_function);
        bindings::rust_waitqueue_list_init(ptr::addr_of_mut!((*wbq_entry).wq_entry.entry));
    }
}

/// Matches and wakes a bit waiter.
///
/// # Safety
///
/// `wq_entry` must be a valid bit wait entry, and `key` must be a valid wake
/// key. The wait queue lock must be held.
#[export]
unsafe extern "C" fn rust_wait_bit_wake_bit_function(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    if key.is_null() {
        return 0;
    }

    // SAFETY: The caller guarantees that `wq_entry` is embedded in a bit wait
    // entry.
    let wbq_entry = unsafe { bit_entry(wq_entry) };
    let key = key.cast::<Key>();
    // SAFETY: Both key pointers are valid for the duration of the callback.
    if !unsafe { key_matches(ptr::addr_of!((*wbq_entry).key), key) } {
        return 0;
    }

    // SAFETY: The caller guarantees that `key` is valid.
    if unsafe { bindings::rust_wait_bit_test(key) } {
        return 0;
    }

    // SAFETY: The matching key and valid entry satisfy the autoremove wake
    // function contract, and the queue lock is held by the caller.
    unsafe { bindings::autoremove_wake_function(wq_entry, mode, sync, key.cast()) }
}

/// Matches a variable waiter and returns the wake key.
///
/// # Safety
///
/// `wq_entry` must be a valid variable wait entry, and `arg` must be either
/// null or a valid wake key.
#[export]
unsafe extern "C" fn rust_wait_bit_var_wake_key(
    wq_entry: *mut Entry,
    arg: *mut c_void,
) -> *mut Key {
    if arg.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees that `wq_entry` is embedded in a variable
    // wait entry.
    let wbq_entry = unsafe { bit_entry(wq_entry) };
    let key = arg.cast::<Key>();
    // SAFETY: Both key pointers are valid for the duration of the callback.
    if unsafe { key_matches(ptr::addr_of!((*wbq_entry).key), key) } {
        key
    } else {
        ptr::null_mut()
    }
}

/// Matches and wakes a variable waiter.
///
/// # Safety
///
/// `wq_entry` must be a valid variable wait entry, and `key` must be a valid
/// wake key. The wait queue lock must be held.
#[export]
unsafe extern "C" fn rust_wait_bit_var_wake_function(
    wq_entry: *mut Entry,
    mode: c_uint,
    sync: c_int,
    key: *mut c_void,
) -> c_int {
    // SAFETY: The caller guarantees that `wq_entry` is valid and `key` is
    // either null or a valid wake key.
    let key = unsafe { rust_wait_bit_var_wake_key(wq_entry, key) };
    if key.is_null() {
        return 0;
    }

    // SAFETY: The matching key and valid entry satisfy the autoremove wake
    // function contract, and the queue lock is held by the caller.
    unsafe { bindings::autoremove_wake_function(wq_entry, mode, sync, key.cast()) }
}

/// Initializes a variable wait entry.
///
/// # Safety
///
/// `wbq_entry` must point to writable storage for a variable wait entry.
#[export]
unsafe extern "C" fn rust_wait_bit_init_var_entry(
    wbq_entry: *mut BitEntry,
    var: *mut c_void,
    flags: c_int,
) {
    // SAFETY: The caller guarantees that the output storage is writable.
    unsafe {
        (*wbq_entry).key = Key {
            flags: var.cast(),
            bit_nr: -1,
            timeout: 0,
        };
        (*wbq_entry).wq_entry = Entry::default();
        (*wbq_entry).wq_entry.flags = flags as c_uint;
        (*wbq_entry).wq_entry.private = bindings::rust_wait_bit_current_task();
        (*wbq_entry).wq_entry.func = Some(rust_wait_bit_var_wake_function);
        bindings::rust_waitqueue_list_init(ptr::addr_of_mut!((*wbq_entry).wq_entry.entry));
    }
}

/// Waits for a bit to clear.
///
/// # Safety
///
/// All pointers must be valid, and `action` must be non-null.
#[export]
unsafe extern "C" fn rust_wait_bit_wait_on_bit(
    wq_head: *mut Head,
    wbq_entry: *mut BitEntry,
    action: bindings::wait_bit_action_f,
    mode: c_uint,
) -> c_int {
    let mut ret = 0;

    loop {
        // SAFETY: The caller guarantees that all pointers are valid.
        unsafe {
            bindings::prepare_to_wait(
                wq_head,
                ptr::addr_of_mut!((*wbq_entry).wq_entry),
                mode as c_int,
            )
        };
        // SAFETY: The caller guarantees that the embedded key is valid.
        if unsafe { bindings::rust_wait_bit_test(ptr::addr_of_mut!((*wbq_entry).key)) } {
            // SAFETY: The caller guarantees that the action is non-null.
            ret = unsafe { call_action(action, ptr::addr_of_mut!((*wbq_entry).key), mode) };
        }

        // SAFETY: The caller guarantees that the embedded key is valid.
        if !unsafe { bindings::rust_wait_bit_test_acquire(ptr::addr_of_mut!((*wbq_entry).key)) }
            || ret != 0
        {
            break;
        }
    }

    // SAFETY: The caller guarantees that the queue and entry are valid.
    unsafe {
        bindings::finish_wait(wq_head, ptr::addr_of_mut!((*wbq_entry).wq_entry));
    }
    ret
}

/// Waits for and atomically acquires a bit.
///
/// # Safety
///
/// All pointers must be valid, and `action` must be non-null.
#[export]
unsafe extern "C" fn rust_wait_bit_wait_on_bit_lock(
    wq_head: *mut Head,
    wbq_entry: *mut BitEntry,
    action: bindings::wait_bit_action_f,
    mode: c_uint,
) -> c_int {
    let mut ret = 0;

    loop {
        // SAFETY: The caller guarantees that all pointers are valid.
        unsafe {
            bindings::prepare_to_wait_exclusive(
                wq_head,
                ptr::addr_of_mut!((*wbq_entry).wq_entry),
                mode as c_int,
            )
        };
        // SAFETY: The caller guarantees that the embedded key is valid.
        if unsafe { bindings::rust_wait_bit_test(ptr::addr_of_mut!((*wbq_entry).key)) } {
            // SAFETY: The caller guarantees that the action is non-null.
            ret = unsafe { call_action(action, ptr::addr_of_mut!((*wbq_entry).key), mode) };
            if ret != 0 {
                // SAFETY: The entry was queued by prepare_to_wait_exclusive.
                unsafe { bindings::finish_wait(wq_head, ptr::addr_of_mut!((*wbq_entry).wq_entry)) };
            }
        }

        // SAFETY: The caller guarantees that the embedded key is valid.
        if !unsafe { bindings::rust_wait_bit_test_and_set(ptr::addr_of_mut!((*wbq_entry).key)) } {
            if ret == 0 {
                // SAFETY: The entry can still be queued after a successful
                // atomic acquisition.
                unsafe { bindings::finish_wait(wq_head, ptr::addr_of_mut!((*wbq_entry).wq_entry)) };
            }
            return 0;
        } else if ret != 0 {
            return ret;
        }
    }
}

/// Constructs and runs an out-of-line bit wait.
///
/// # Safety
///
/// `word` and `action` must be valid, and `action` must be non-null.
#[export]
unsafe extern "C" fn rust_wait_bit_out_of_line_wait(
    word: *mut c_ulong,
    bit: c_int,
    action: bindings::wait_bit_action_f,
    mode: c_uint,
) -> c_int {
    let mut wbq_entry = MaybeUninit::<BitEntry>::uninit();
    let wbq_entry = wbq_entry.as_mut_ptr();
    // SAFETY: `wbq_entry` points to writable storage.
    unsafe { init_bit_entry(wbq_entry, word, bit) };
    // SAFETY: The queue and initialized entry are valid, and the action is
    // non-null.
    unsafe {
        rust_wait_bit_wait_on_bit(rust_wait_bit_waitqueue(word, bit), wbq_entry, action, mode)
    }
}

/// Constructs and runs a timed out-of-line bit wait.
///
/// # Safety
///
/// `word` and `action` must be valid, and `action` must be non-null.
#[export]
unsafe extern "C" fn rust_wait_bit_out_of_line_wait_timeout(
    word: *mut c_ulong,
    bit: c_int,
    action: bindings::wait_bit_action_f,
    mode: c_uint,
    timeout: c_ulong,
) -> c_int {
    let mut wbq_entry = MaybeUninit::<BitEntry>::uninit();
    let wbq_entry = wbq_entry.as_mut_ptr();
    // SAFETY: `wbq_entry` points to writable storage.
    unsafe { init_bit_entry(wbq_entry, word, bit) };
    // SAFETY: `wbq_entry` is initialized and its key is writable.
    unsafe {
        (*wbq_entry).key.timeout = bindings::rust_wait_bit_jiffies().wrapping_add(timeout);
    }
    // SAFETY: The queue and initialized entry are valid, and the action is
    // non-null.
    unsafe {
        rust_wait_bit_wait_on_bit(rust_wait_bit_waitqueue(word, bit), wbq_entry, action, mode)
    }
}

/// Constructs and runs an out-of-line locked bit wait.
///
/// # Safety
///
/// `word` and `action` must be valid, and `action` must be non-null.
#[export]
unsafe extern "C" fn rust_wait_bit_out_of_line_wait_lock(
    word: *mut c_ulong,
    bit: c_int,
    action: bindings::wait_bit_action_f,
    mode: c_uint,
) -> c_int {
    let mut wbq_entry = MaybeUninit::<BitEntry>::uninit();
    let wbq_entry = wbq_entry.as_mut_ptr();
    // SAFETY: `wbq_entry` points to writable storage.
    unsafe { init_bit_entry(wbq_entry, word, bit) };
    // SAFETY: The queue and initialized entry are valid, and the action is
    // non-null.
    unsafe {
        rust_wait_bit_wait_on_bit_lock(rust_wait_bit_waitqueue(word, bit), wbq_entry, action, mode)
    }
}

/// Wakes waiters on one bit through an already selected shared queue.
///
/// # Safety
///
/// `wq_head` and `word` must be valid.
#[export]
unsafe extern "C" fn rust_wait_bit_wake_up_bit(wq_head: *mut Head, word: *mut c_ulong, bit: c_int) {
    // SAFETY: The caller guarantees that the queue and word are valid.
    unsafe { rust_wait_bit_wake_up_common(wq_head, word, bit) };
}

/// Selects the queue for a bit and wakes its matching waiters.
///
/// # Safety
///
/// `word` is used only as an address and need not be readable.
#[export]
unsafe extern "C" fn rust_wait_bit_wake_up(word: *mut c_ulong, bit: c_int) {
    // SAFETY: The shared table is initialized.
    let wq_head = unsafe { rust_wait_bit_waitqueue(word, bit) };
    // SAFETY: The selected queue and word are valid for a bit wake.
    unsafe { rust_wait_bit_wake_up_common(wq_head, word, bit) };
}

/// Selects the queue for a variable and wakes its matching waiters.
///
/// # Safety
///
/// `var` is used only as an address and need not be readable.
#[export]
unsafe extern "C" fn rust_wait_bit_wake_up_var(var: *mut c_void) {
    // SAFETY: The shared table is initialized.
    let wq_head = unsafe { rust_wait_bit_var_waitqueue(var) };
    // SAFETY: The selected queue and variable are valid for a variable wake.
    unsafe { rust_wait_bit_wake_up_common(wq_head, var.cast(), -1) };
}

/// Wakes matching waiters on an already selected queue.
///
/// # Safety
///
/// `wq_head` and `word` must be valid.
#[inline]
unsafe fn rust_wait_bit_wake_up_common(wq_head: *mut Head, word: *mut c_ulong, bit: c_int) {
    // SAFETY: The caller guarantees that `wq_head` is valid.
    if unsafe { bindings::rust_waitqueue_head_is_empty(wq_head) } {
        return;
    }

    let mut key = Key {
        flags: word,
        bit_nr: bit,
        timeout: 0,
    };
    // SAFETY: The caller guarantees that the queue is valid, and `key` lives
    // for the duration of the wake. The common wake path uses one normal task
    // and one exclusive wake credit.
    unsafe {
        bindings::__wake_up(
            wq_head,
            bindings::TASK_NORMAL,
            1,
            ptr::addr_of_mut!(key).cast(),
        )
    };
}
