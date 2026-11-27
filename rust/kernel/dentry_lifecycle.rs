// SPDX-License-Identifier: GPL-2.0

//! Native dentry lifecycle.
//!
//! Rust owns reference-drop selection, retention, kill ordering, private
//! shrink-list processing, LRU isolation decisions, and final release
//! scheduling. C retains the shared layouts, lockref atomics, lock and RCU
//! primitives, hash and tree unlinking, list-LRU traversal, accounting, and
//! filesystem callbacks.

use crate::{
    bindings,
    sync::barrier::{smp_mb, Read},
};
use core::{
    ffi::{c_int, c_uint},
    ptr,
};
use macros::export;

type Dentry = bindings::dentry;
type ListHead = bindings::list_head;
type ListLruOne = bindings::list_lru_one;

const DCACHE_LRU_LIST: c_uint = bindings::dentry_flags_DCACHE_LRU_LIST;
const DCACHE_SHRINK_LIST: c_uint = bindings::dentry_flags_DCACHE_SHRINK_LIST;
const DCACHE_REFERENCED: c_uint = bindings::dentry_flags_DCACHE_REFERENCED;
const DCACHE_DISCONNECTED: c_uint = bindings::dentry_flags_DCACHE_DISCONNECTED;
const DCACHE_OP_DELETE: c_uint = bindings::dentry_flags_DCACHE_OP_DELETE;
const DCACHE_OP_PRUNE: c_uint = bindings::dentry_flags_DCACHE_OP_PRUNE;
const DCACHE_DONTCACHE: c_uint = bindings::dentry_flags_DCACHE_DONTCACHE;
const DCACHE_DENTRY_KILLED: c_uint = bindings::dentry_flags_DCACHE_DENTRY_KILLED;
const DCACHE_NORCU: c_uint = bindings::dentry_flags_DCACHE_NORCU;
const DCACHE_PERSISTENT: c_uint = bindings::dentry_flags_DCACHE_PERSISTENT;
const DENTRY_D_LOCK_NESTED: c_uint = bindings::dentry_d_lock_class_DENTRY_D_LOCK_NESTED;

/// Returns the dentry flags with the original read-once semantics.
///
/// # Safety
///
/// `dentry` must remain live for the duration of the call.
unsafe fn flags(dentry: *mut Dentry) -> c_uint {
    // SAFETY: The caller guarantees a live dentry.
    unsafe { bindings::rust_helper_dentry_flags(dentry) }
}

/// Replaces the dentry flags.
///
/// # Safety
///
/// `dentry` must remain live and its flags must be stable under `d_lock`.
unsafe fn set_flags(dentry: *mut Dentry, value: c_uint) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_set_flags(dentry, value) }
}

/// Returns the reference count under `d_lock`.
///
/// # Safety
///
/// `dentry` must remain live and its `d_lock` must be held.
unsafe fn count(dentry: *mut Dentry) -> c_int {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_count(dentry) }
}

/// Decrements the reference count under `d_lock`.
///
/// # Safety
///
/// `dentry` must remain live and its `d_lock` must be held.
unsafe fn dec_count(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_dec_count(dentry) }
}

/// Acquires a dentry lock.
///
/// # Safety
///
/// `dentry` must remain live and the caller must preserve lock ordering.
unsafe fn lock(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_lock(dentry) }
}

/// Releases a dentry lock.
///
/// # Safety
///
/// The caller must hold the dentry lock.
unsafe fn unlock(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_unlock(dentry) }
}

/// Acquires a nested dentry lock.
///
/// # Safety
///
/// `dentry` must remain live and the caller must preserve lock ordering.
unsafe fn lock_nested(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_lock_nested(dentry, DENTRY_D_LOCK_NESTED as c_uint) }
}

/// Returns whether the dentry has no inode.
///
/// # Safety
///
/// `dentry` must remain live for the duration of the call.
unsafe fn is_negative(dentry: *mut Dentry) -> bool {
    // SAFETY: The caller guarantees a live dentry.
    unsafe { bindings::rust_helper_dentry_is_negative(dentry) }
}

/// Returns the parent dentry pointer.
///
/// # Safety
///
/// `dentry` must remain live for the duration of the call.
unsafe fn parent(dentry: *mut Dentry) -> *mut Dentry {
    // SAFETY: The caller guarantees a live dentry.
    unsafe { bindings::rust_helper_dentry_parent(dentry) }
}

/// Verifies the dentry LRU-list flags.
///
/// # Safety
///
/// `dentry` must remain live and its LRU state must be stable under `d_lock`.
unsafe fn verify_lru_flags(dentry: *mut Dentry, expected: c_uint) {
    // SAFETY: The caller guarantees the locking contract.
    unsafe { bindings::rust_helper_dentry_verify_flags(dentry, expected) }
}

/// Adjusts the per-CPU negative-dentry count.
///
/// # Safety
///
/// The caller must hold the dentry LRU state that makes the adjustment valid.
unsafe fn account_negative(delta: c_int) {
    // SAFETY: The caller guarantees the accounting contract.
    unsafe { bindings::rust_helper_dentry_account_negative(delta) }
}

/// Adds a dentry to its superblock LRU.
///
/// # Safety
///
/// `dentry` must remain live, be absent from the LRU, and hold `d_lock`.
unsafe fn d_lru_add(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the LRU and locking contracts.
    unsafe { verify_lru_flags(dentry, 0) };
    // SAFETY: The LRU bit describes this callback's state transition.
    unsafe { set_flags(dentry, flags(dentry) | DCACHE_LRU_LIST) };
    // SAFETY: The caller holds the accounting contract.
    unsafe { bindings::rust_helper_dentry_account_unused(1) };
    // SAFETY: The caller guarantees dentry lifetime.
    if unsafe { is_negative(dentry) } {
        // SAFETY: The dentry is negative and now on the LRU.
        unsafe { account_negative(1) };
    }
    // SAFETY: The caller guarantees the LRU contract.
    let added = unsafe { bindings::rust_helper_dentry_lru_add(dentry) };
    // SAFETY: This preserves the original list insertion diagnostic.
    unsafe { bindings::rust_helper_dentry_warn_once(!added) };
}

/// Removes a dentry from its superblock LRU.
///
/// # Safety
///
/// `dentry` must remain live, be on the LRU, and hold `d_lock`.
unsafe fn d_lru_del(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the LRU and locking contracts.
    unsafe { verify_lru_flags(dentry, DCACHE_LRU_LIST) };
    // SAFETY: The caller guarantees dentry lifetime.
    let negative = unsafe { is_negative(dentry) };
    // SAFETY: The LRU bit describes this callback's state transition.
    unsafe { set_flags(dentry, flags(dentry) & !DCACHE_LRU_LIST) };
    // SAFETY: The caller holds the accounting contract.
    unsafe { bindings::rust_helper_dentry_account_unused(-1) };
    if negative {
        // SAFETY: The dentry was negative on the LRU.
        unsafe { account_negative(-1) };
    }
    // SAFETY: The caller guarantees the LRU contract.
    let deleted = unsafe { bindings::rust_helper_dentry_lru_del(dentry) };
    // SAFETY: This preserves the original list removal diagnostic.
    unsafe { bindings::rust_helper_dentry_warn_once(!deleted) };
}

/// Removes a dentry from a private shrink list.
///
/// # Safety
///
/// `dentry` must remain live, be on a shrink list, and hold `d_lock`.
unsafe fn d_shrink_del(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the shrink-list contract.
    unsafe { verify_lru_flags(dentry, DCACHE_SHRINK_LIST | DCACHE_LRU_LIST) };
    // SAFETY: The caller owns the private shrink-list entry.
    unsafe { bindings::rust_helper_dentry_list_del_init(ptr::addr_of_mut!((*dentry).d_lru)) };
    // SAFETY: Both list bits describe the entry being removed.
    unsafe {
        set_flags(
            dentry,
            flags(dentry) & !(DCACHE_SHRINK_LIST | DCACHE_LRU_LIST),
        )
    };
    // SAFETY: The dentry leaves the list-accounted state.
    unsafe { bindings::rust_helper_dentry_account_unused(-1) };
}

/// Adds a dentry to a private shrink list.
///
/// # Safety
///
/// `dentry` must remain live, be absent from all LRU lists, and hold `d_lock`.
unsafe fn d_shrink_add(dentry: *mut Dentry, list: *mut ListHead) {
    // SAFETY: The caller guarantees the shrink-list contract.
    unsafe { verify_lru_flags(dentry, 0) };
    // SAFETY: The caller supplies a private list with sufficient lifetime.
    unsafe { bindings::rust_helper_dentry_list_add(ptr::addr_of_mut!((*dentry).d_lru), list) };
    // SAFETY: Both list bits describe the entry being added.
    unsafe { set_flags(dentry, flags(dentry) | DCACHE_SHRINK_LIST | DCACHE_LRU_LIST) };
    // SAFETY: The dentry enters the list-accounted state.
    unsafe { bindings::rust_helper_dentry_account_unused(1) };
}

/// Removes a dentry from its superblock LRU during isolation.
///
/// # Safety
///
/// `dentry` must remain live, be on the LRU, and the LRU callback must hold the
/// required LRU lock and `d_lock`.
unsafe fn d_lru_isolate(lru: *mut ListLruOne, dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the LRU callback contract.
    unsafe { verify_lru_flags(dentry, DCACHE_LRU_LIST) };
    // SAFETY: The LRU callback guarantees dentry lifetime.
    let negative = unsafe { is_negative(dentry) };
    // SAFETY: The LRU bit describes this isolation.
    unsafe { set_flags(dentry, flags(dentry) & !DCACHE_LRU_LIST) };
    // SAFETY: The dentry leaves the LRU accounting state.
    unsafe { bindings::rust_helper_dentry_account_unused(-1) };
    if negative {
        // SAFETY: The isolated dentry was negative.
        unsafe { account_negative(-1) };
    }
    // SAFETY: The LRU callback holds the required list lock.
    unsafe { bindings::list_lru_isolate(lru, ptr::addr_of_mut!((*dentry).d_lru)) };
}

/// Moves a dentry from the superblock LRU to a private shrink list.
///
/// # Safety
///
/// `dentry` must remain live, be on the LRU, and the LRU callback must hold the
/// required LRU lock and `d_lock`.
unsafe fn d_lru_shrink_move(lru: *mut ListLruOne, dentry: *mut Dentry, list: *mut ListHead) {
    // SAFETY: The caller guarantees the LRU callback contract.
    unsafe { verify_lru_flags(dentry, DCACHE_LRU_LIST) };
    // SAFETY: The LRU callback guarantees dentry lifetime.
    let negative = unsafe { is_negative(dentry) };
    // SAFETY: The shrink bit describes the transfer to a private list.
    unsafe { set_flags(dentry, flags(dentry) | DCACHE_SHRINK_LIST) };
    if negative {
        // SAFETY: The transferred dentry was negative.
        unsafe { account_negative(-1) };
    }
    // SAFETY: The LRU callback holds the required list lock.
    unsafe { bindings::list_lru_isolate_move(lru, ptr::addr_of_mut!((*dentry).d_lru), list) };
}

/// Decides whether a unreferenced dentry remains useful.
///
/// # Safety
///
/// `dentry` must remain live. When `locked` is false it must be protected by
/// an RCU read-side section. When `locked` is true its `d_lock` must be held.
unsafe fn retain_dentry(dentry: *mut Dentry, locked: bool) -> bool {
    smp_mb(Read);
    // SAFETY: The caller guarantees dentry lifetime.
    let d_flags = unsafe { flags(dentry) };

    // SAFETY: The caller guarantees dentry lifetime.
    if unsafe { bindings::rust_helper_dentry_unhashed(dentry) } {
        return false;
    }
    if d_flags & DCACHE_DISCONNECTED != 0 {
        return false;
    }
    if d_flags & DCACHE_OP_DELETE != 0 {
        if !locked {
            return false;
        }
        // SAFETY: The delete operation is present and d_lock is held.
        if unsafe { bindings::rust_helper_dentry_delete(dentry) } {
            return false;
        }
    }
    if d_flags & DCACHE_DONTCACHE != 0 {
        return false;
    }
    if d_flags & DCACHE_LRU_LIST == 0 {
        if !locked {
            return false;
        }
        // SAFETY: The dentry is locked and absent from the LRU.
        unsafe { d_lru_add(dentry) };
    } else if d_flags & DCACHE_REFERENCED == 0 {
        if !locked {
            return false;
        }
        // SAFETY: The dentry is locked and on the LRU.
        unsafe { set_flags(dentry, flags(dentry) | DCACHE_REFERENCED) };
    }
    true
}

/// Attempts the lockless reference-drop path.
///
/// On success the dentry lock is not held. On failure the dentry lock is held
/// and the count is zero.
///
/// # Safety
///
/// `dentry` must hold a reference and remain live for the call.
unsafe fn fast_dput(dentry: *mut Dentry) -> bool {
    // SAFETY: The caller guarantees dentry lifetime.
    unsafe { bindings::rcu_read_lock() };
    // SAFETY: The caller owns one reference in this live dentry.
    let ret = unsafe { bindings::lockref_put_return(ptr::addr_of_mut!((*dentry).d_lockref)) };

    if ret < 0 {
        // SAFETY: The caller guarantees dentry lifetime.
        unsafe { lock(dentry) };
        // SAFETY: This closes the RCU section before returning with d_lock.
        unsafe { bindings::rcu_read_unlock() };
        // SAFETY: The helper preserves the original invariant diagnostic.
        if unsafe {
            bindings::rust_helper_dentry_warn_once(bindings::rust_helper_dentry_dead_or_zero(
                dentry,
            ))
        } {
            // SAFETY: This function acquired d_lock.
            unsafe { unlock(dentry) };
            return true;
        }
        // SAFETY: This function holds d_lock and is completing the drop.
        unsafe { dec_count(dentry) };
    } else if ret != 0 || {
        // SAFETY: The caller guarantees dentry lifetime and RCU protection.
        unsafe { retain_dentry(dentry, false) }
    } {
        // SAFETY: This closes the RCU section opened above.
        unsafe { bindings::rcu_read_unlock() };
        return true;
    } else {
        // SAFETY: The caller guarantees dentry lifetime.
        unsafe { lock(dentry) };
        // SAFETY: This closes the RCU section before returning with d_lock.
        unsafe { bindings::rcu_read_unlock() };
    }

    // SAFETY: This function holds d_lock after the fallback above.
    if unsafe { count(dentry) } != 0 || unsafe { retain_dentry(dentry, true) } {
        // SAFETY: This function acquired d_lock.
        unsafe { unlock(dentry) };
        true
    } else {
        false
    }
}

/// Schedules or performs final dentry memory release.
///
/// # Safety
///
/// `dentry` must be killed, unlocked, absent from every list, and dead to all
/// new references.
unsafe fn dentry_free(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the final-release invariants.
    unsafe { bindings::rust_helper_dentry_verify_free(dentry) };
    // SAFETY: The caller guarantees the final-release invariants.
    if unsafe { bindings::rust_helper_dentry_put_external_name(dentry) } {
        return;
    }
    // SAFETY: The caller guarantees dentry lifetime.
    let use_rcu = unsafe { flags(dentry) } & DCACHE_NORCU == 0;
    // SAFETY: The dentry is dead and this is the final release owner.
    unsafe { bindings::rust_helper_dentry_free(dentry, use_rcu) };
}

/// Evicts one dentry and returns a parent that also reached zero references.
///
/// # Safety
///
/// `dentry` must remain live, `d_lock` must be held, and the caller must not use
/// the dentry after this call. The returned parent has `d_lock` held.
unsafe fn dentry_kill(dentry: *mut Dentry) -> *mut Dentry {
    // SAFETY: The caller guarantees the lock and lifetime contracts.
    if !unsafe { bindings::rust_helper_dentry_lock_for_kill(dentry) } {
        return ptr::null_mut();
    }

    // SAFETY: Lock acquisition made the dentry unrecoverably dead.
    unsafe { bindings::lockref_mark_dead(ptr::addr_of_mut!((*dentry).d_lockref)) };

    // SAFETY: The caller guarantees dentry lifetime and d_lock.
    if unsafe { flags(dentry) } & DCACHE_OP_PRUNE != 0 {
        // SAFETY: The prune callback is present and d_lock is held.
        unsafe { bindings::rust_helper_dentry_prune(dentry) };
    }
    // SAFETY: The caller guarantees dentry lifetime and d_lock.
    let d_flags = unsafe { flags(dentry) };
    if d_flags & DCACHE_LRU_LIST != 0 && d_flags & DCACHE_SHRINK_LIST == 0 {
        // SAFETY: The dentry is locked and on the superblock LRU.
        unsafe { d_lru_del(dentry) };
    }
    // SAFETY: The dentry is locked and dead.
    unsafe { bindings::rust_helper_dentry_drop(dentry) };
    // SAFETY: The caller guarantees dentry lifetime and d_lock.
    if unsafe { bindings::rust_helper_dentry_is_positive(dentry) } {
        // SAFETY: A positive dead dentry is locked with its inode lock held.
        unsafe { bindings::rust_helper_dentry_unlink_inode(dentry) };
    } else {
        // SAFETY: A negative dead dentry holds only d_lock.
        unsafe { unlock(dentry) };
    }
    // SAFETY: The dentry leaves the live object accounting.
    unsafe { bindings::rust_helper_dentry_account_dentry(-1) };
    // SAFETY: The dentry remains live until final release.
    if unsafe { bindings::rust_helper_dentry_has_release(dentry) } {
        // SAFETY: The release callback is present and dentry state is stable.
        unsafe { bindings::rust_helper_dentry_release(dentry) };
    }
    // SAFETY: This preserves the original quiescent-state check.
    unsafe { bindings::rust_helper_dentry_resched_tasks_rcu_qs() };

    let mut parent = ptr::null_mut();
    // SAFETY: The caller guarantees dentry lifetime.
    if !unsafe { bindings::rust_helper_dentry_is_root(dentry) } {
        // SAFETY: The non-root dentry remains live and has a stable parent.
        parent = unsafe { self::parent(dentry) };
        // SAFETY: A non-root dentry has a stable live parent here.
        unsafe { lock(parent) };
    }
    // SAFETY: The dentry is dead and its parent lock establishes nesting order.
    unsafe { lock_nested(dentry) };
    // SAFETY: The dentry lock protects the tree links and killed state.
    unsafe { bindings::rust_helper_dentry_unlist(dentry) };
    // SAFETY: This function holds the nested dentry lock.
    let can_free = unsafe { flags(dentry) } & DCACHE_SHRINK_LIST == 0;
    // SAFETY: This function acquired the nested dentry lock.
    unsafe { unlock(dentry) };
    if can_free {
        // SAFETY: The dentry is killed and absent from every shrink list.
        unsafe { dentry_free(dentry) };
    }
    if !parent.is_null() {
        // SAFETY: This function holds the parent lock.
        unsafe { dec_count(parent) };
        // SAFETY: This function holds the parent lock.
        if unsafe { count(parent) } != 0 {
            // SAFETY: This function acquired the parent lock.
            unsafe { unlock(parent) };
            return ptr::null_mut();
        }
    }
    parent
}

/// Completes a reference drop that failed the lockless path.
///
/// # Safety
///
/// `dentry` must hold a reference and its `d_lock` must be held.
unsafe fn finish_dput(mut dentry: *mut Dentry) {
    loop {
        // SAFETY: The current dentry has zero references and d_lock held.
        dentry = unsafe { dentry_kill(dentry) };
        if dentry.is_null() {
            return;
        }
        // SAFETY: `dentry_kill()` returned a live locked parent.
        if unsafe { retain_dentry(dentry, true) } {
            // SAFETY: This function owns the returned parent lock.
            unsafe { unlock(dentry) };
            return;
        }
    }
}

/// Kills a locked shrink-list chain.
///
/// # Safety
///
/// `dentry` must be live and its `d_lock` must be held.
unsafe fn shrink_kill(mut dentry: *mut Dentry) {
    loop {
        // SAFETY: The current dentry has zero references and d_lock held.
        dentry = unsafe { dentry_kill(dentry) };
        if dentry.is_null() {
            return;
        }
    }
}

/// Releases a dentry reference.
///
/// # Safety
///
/// `dentry` must be null or own one live reference.
#[export]
unsafe extern "C" fn dput(dentry: *mut Dentry) {
    if dentry.is_null() {
        return;
    }
    // SAFETY: This preserves the original sleeping-context contract.
    unsafe { bindings::rust_helper_dentry_might_sleep() };
    // SAFETY: The caller owns one reference in this live dentry.
    if unsafe { fast_dput(dentry) } {
        return;
    }
    // SAFETY: The fast path failed and left d_lock held at zero references.
    unsafe { finish_dput(dentry) };
}

/// Removes the persistent reference and completes discardability.
///
/// # Safety
///
/// `dentry` must remain live and own the persistent reference.
#[export]
unsafe extern "C" fn d_make_discardable(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees dentry lifetime.
    unsafe { lock(dentry) };
    // SAFETY: This preserves the original persistent-state diagnostic.
    unsafe { bindings::rust_helper_dentry_warn_once(flags(dentry) & DCACHE_PERSISTENT == 0) };
    // SAFETY: The dentry lock makes the flag transition stable.
    unsafe { set_flags(dentry, flags(dentry) & !DCACHE_PERSISTENT) };
    // SAFETY: The persistent reference is being removed under d_lock.
    unsafe { dec_count(dentry) };
    // SAFETY: The dentry is locked and its persistent reference is gone.
    unsafe { finish_dput(dentry) };
}

/// Moves an idle locked dentry to a private shrink list.
///
/// # Safety
///
/// `dentry` must remain live, `d_lock` must be held, and `list` must remain
/// private and live until `shrink_dentry_list()`.
unsafe fn move_to_shrink_list(dentry: *mut Dentry, list: *mut ListHead) -> bool {
    // SAFETY: The caller guarantees dentry lifetime and d_lock.
    if unsafe { count(dentry) } == 0
        // SAFETY: The caller guarantees dentry lifetime and d_lock.
        && unsafe { flags(dentry) } & DCACHE_SHRINK_LIST == 0
    {
        // SAFETY: The dentry is locked and its LRU state is stable.
        if unsafe { flags(dentry) } & DCACHE_LRU_LIST != 0 {
            // SAFETY: The dentry is locked and on the superblock LRU.
            unsafe { d_lru_del(dentry) };
        }
        // SAFETY: The dentry is locked and absent from every LRU list.
        unsafe { d_shrink_add(dentry, list) };
        true
    } else {
        false
    }
}

/// Moves an idle locked dentry to a private shrink list.
///
/// # Safety
///
/// `dentry` must remain live, `d_lock` must be held, and `list` must remain
/// private and live until `shrink_dentry_list()`.
#[export]
unsafe extern "C" fn __move_to_shrink_list(dentry: *mut Dentry, list: *mut ListHead) -> bool {
    // SAFETY: The caller guarantees the lock, lifetime, and list contracts.
    unsafe { move_to_shrink_list(dentry, list) }
}

/// Drops a dentry reference and moves the final reference to a shrink list.
///
/// # Safety
///
/// `dentry` must own one live reference and `list` must remain private and live
/// until `shrink_dentry_list()`.
#[export]
unsafe extern "C" fn dput_to_list(dentry: *mut Dentry, list: *mut ListHead) {
    // SAFETY: The caller owns one reference in this live dentry.
    if unsafe { fast_dput(dentry) } {
        return;
    }
    // SAFETY: The fast path failed and left d_lock held at zero references.
    unsafe { move_to_shrink_list(dentry, list) };
    // SAFETY: The slow path left d_lock held.
    unsafe { unlock(dentry) };
}

/// Kills every dentry on a private shrink list.
///
/// # Safety
///
/// `list` must be a live private shrink list whose entries were isolated by the
/// dentry LRU code.
#[export]
unsafe extern "C" fn shrink_dentry_list(list: *mut ListHead) {
    // SAFETY: The caller guarantees the private list lifetime.
    while !unsafe { bindings::rust_helper_dentry_list_empty(list) } {
        // SAFETY: The list is non-empty and holds dentries through `d_lru`.
        let dentry = unsafe { bindings::rust_helper_dentry_lru_tail(list) };
        // SAFETY: The list owns a stable reference to this dentry object.
        unsafe { lock(dentry) };
        // SAFETY: The dentry lock protects this private-list entry.
        unsafe { d_shrink_del(dentry) };
        // SAFETY: This function holds d_lock.
        if unsafe { flags(dentry) } & DCACHE_DENTRY_KILLED != 0 {
            // SAFETY: This function acquired d_lock.
            unsafe { unlock(dentry) };
            // SAFETY: The killed dentry is already absent from the tree.
            unsafe { dentry_free(dentry) };
        } else {
            // SAFETY: The dentry is locked and still owns tree state.
            unsafe { shrink_kill(dentry) };
        }
    }
}

/// Implements the main LRU isolation callback.
///
/// # Safety
///
/// The list-LRU walker supplies a live item and holds the required LRU lock.
#[export]
unsafe extern "C" fn rust_dentry_lru_isolate(
    item: *mut ListHead,
    lru: *mut ListLruOne,
    arg: *mut core::ffi::c_void,
) -> bindings::lru_status {
    // SAFETY: The list contains dentries through their `d_lru` member.
    let dentry = unsafe { crate::container_of!(item, Dentry, d_lru) };
    // SAFETY: The LRU walker guarantees dentry lifetime.
    if !unsafe { bindings::rust_helper_dentry_trylock(dentry) } {
        return bindings::lru_status::LRU_SKIP;
    }
    // SAFETY: This callback acquired d_lock.
    if unsafe { count(dentry) } != 0 {
        // SAFETY: The LRU walker and d_lock permit isolation.
        unsafe { d_lru_isolate(lru, dentry) };
        // SAFETY: This function acquired d_lock.
        unsafe { unlock(dentry) };
        return bindings::lru_status::LRU_REMOVED;
    }
    // SAFETY: This callback holds d_lock.
    if unsafe { flags(dentry) } & DCACHE_REFERENCED != 0 {
        // SAFETY: The dentry lock protects the referenced bit.
        unsafe { set_flags(dentry, flags(dentry) & !DCACHE_REFERENCED) };
        // SAFETY: This function acquired d_lock.
        unsafe { unlock(dentry) };
        return bindings::lru_status::LRU_ROTATE;
    }
    // SAFETY: The LRU walker and d_lock permit the list move.
    unsafe { d_lru_shrink_move(lru, dentry, arg.cast()) };
    // SAFETY: This function acquired d_lock.
    unsafe { unlock(dentry) };
    bindings::lru_status::LRU_REMOVED
}

/// Implements the unmount LRU isolation callback.
///
/// # Safety
///
/// The list-LRU walker supplies a live item and holds the required LRU lock.
#[export]
unsafe extern "C" fn rust_dentry_lru_isolate_shrink(
    item: *mut ListHead,
    lru: *mut ListLruOne,
    arg: *mut core::ffi::c_void,
) -> bindings::lru_status {
    // SAFETY: The list contains dentries through their `d_lru` member.
    let dentry = unsafe { crate::container_of!(item, Dentry, d_lru) };
    // SAFETY: The LRU walker guarantees dentry lifetime.
    if !unsafe { bindings::rust_helper_dentry_trylock(dentry) } {
        return bindings::lru_status::LRU_SKIP;
    }
    // SAFETY: The LRU walker and d_lock permit the list move.
    unsafe { d_lru_shrink_move(lru, dentry, arg.cast()) };
    // SAFETY: This function acquired d_lock.
    unsafe { unlock(dentry) };
    bindings::lru_status::LRU_REMOVED
}

/// Kills a locked dentry and its now-unreferenced ancestors.
///
/// # Safety
///
/// `dentry` must be live and its `d_lock` must be held.
#[export]
unsafe extern "C" fn rust_dentry_shrink_kill(dentry: *mut Dentry) {
    // SAFETY: The caller guarantees the lock and lifetime contract.
    unsafe { shrink_kill(dentry) };
}
