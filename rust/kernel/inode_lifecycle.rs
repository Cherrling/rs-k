// SPDX-License-Identifier: GPL-2.0

//! Native inode lifecycle.
//!
//! Rust owns allocation rollback, reference acquisition and release, LRU
//! insertion and isolation, writeback triggering, eviction, and final
//! destruction. C retains the inode layouts, slab allocation, hash and list
//! traversal, page cache and writeback primitives, locks, security, and
//! filesystem callbacks.

use crate::bindings;
#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};
use core::{ffi::c_uint, mem::MaybeUninit, ptr};
use macros::export;

type Inode = bindings::inode;
type ListHead = bindings::list_head;
type ListLruOne = bindings::list_lru_one;
type ShrinkControl = bindings::shrink_control;
type SuperBlock = bindings::super_block;

const I_NEW: c_uint = bindings::inode_state_flags_enum_I_NEW as c_uint;
const I_SYNC: c_uint = bindings::inode_state_flags_enum_I_SYNC as c_uint;
const I_LRU_ISOLATING: c_uint = bindings::inode_state_flags_enum_I_LRU_ISOLATING as c_uint;
const I_DIRTY_SYNC: c_uint = bindings::inode_state_flags_enum_I_DIRTY_SYNC as c_uint;
const I_DIRTY_DATASYNC: c_uint = bindings::inode_state_flags_enum_I_DIRTY_DATASYNC as c_uint;
const I_DIRTY_PAGES: c_uint = bindings::inode_state_flags_enum_I_DIRTY_PAGES as c_uint;
const I_WILL_FREE: c_uint = bindings::inode_state_flags_enum_I_WILL_FREE as c_uint;
const I_FREEING: c_uint = bindings::inode_state_flags_enum_I_FREEING as c_uint;
const I_CLEAR: c_uint = bindings::inode_state_flags_enum_I_CLEAR as c_uint;
const I_REFERENCED: c_uint = bindings::inode_state_flags_enum_I_REFERENCED as c_uint;
const I_DIRTY_TIME: c_uint = bindings::inode_state_flags_enum_I_DIRTY_TIME as c_uint;
const I_DONTCACHE: c_uint = bindings::inode_state_flags_enum_I_DONTCACHE as c_uint;
const I_DIRTY_ALL: c_uint = I_DIRTY_SYNC | I_DIRTY_DATASYNC | I_DIRTY_PAGES | I_DIRTY_TIME;

const FAIL_ALLOC: u32 = 1;
const FAIL_INIT: u32 = 2;

const COUNTER_ALLOC: usize = 0;
const COUNTER_INIT_FAILURE: usize = 1;
const COUNTER_DESTROY: usize = 2;
const COUNTER_FREE_SCHEDULED: usize = 3;
const COUNTER_FREE_NOW: usize = 4;
const COUNTER_FAILURE: usize = 5;

#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
static COUNTERS: [AtomicU32; 6] = [
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
fn record_counter(index: usize) {
    COUNTERS[index].fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_INODE_LIFECYCLE_KUNIT_TEST))]
fn record_counter(_index: usize) {}

fn record_alloc() {
    record_counter(COUNTER_ALLOC);
}

fn record_init_failure() {
    record_counter(COUNTER_INIT_FAILURE);
}

fn record_destroy() {
    record_counter(COUNTER_DESTROY);
}

fn record_free_scheduled() {
    record_counter(COUNTER_FREE_SCHEDULED);
}

fn record_free_now() {
    record_counter(COUNTER_FREE_NOW);
}

fn record_failure() {
    record_counter(COUNTER_FAILURE);
}

#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_INODE_LIFECYCLE_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

fn injected_failure(stage: u32) -> bool {
    if requested_failure(stage) {
        record_failure();
        true
    } else {
        false
    }
}

/// Terminates on an unconditional inode lifecycle invariant violation.
#[cold]
fn inode_bug() -> ! {
    // SAFETY: Callers reached an invariant that the original code treats as
    // fatal.
    unsafe { bindings::BUG() }
}

/// Reports and terminates on a debug-only inode invariant violation.
#[cfg(CONFIG_DEBUG_VFS)]
#[cold]
fn vfs_bug_inode(inode: *mut Inode) {
    // SAFETY: The caller guarantees a live inode for diagnostics.
    unsafe { bindings::rust_helper_inode_dump(inode) };
    inode_bug();
}

/// Returns up to six lifecycle counters for the KUnit harness.
#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_inode_test_counter(index: c_uint) -> c_uint {
    match index as usize {
        COUNTER_ALLOC
        | COUNTER_INIT_FAILURE
        | COUNTER_DESTROY
        | COUNTER_FREE_SCHEDULED
        | COUNTER_FREE_NOW
        | COUNTER_FAILURE => COUNTERS[index as usize].load(Ordering::Relaxed),
        _ => 0,
    }
}

/// Clears the KUnit lifecycle counters.
#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_inode_test_reset() {
    NEXT_FAILURE.store(0, Ordering::Relaxed);
    for counter in &COUNTERS {
        counter.store(0, Ordering::Relaxed);
    }
}

/// Arms one KUnit failure stage.
#[cfg(CONFIG_INODE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_inode_test_fail(stage: c_uint) {
    NEXT_FAILURE.store(stage, Ordering::Relaxed);
}

/// Allocates an inode through the filesystem or the generic slab.
///
/// # Safety
///
/// `sb` must remain live for the duration of the allocation.
unsafe fn alloc_inode_internal(sb: *mut SuperBlock) -> *mut Inode {
    record_alloc();
    if injected_failure(FAIL_ALLOC) {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees a live superblock.
    let inode = if unsafe { bindings::rust_helper_inode_has_alloc(sb) } {
        // SAFETY: The superblock advertises a filesystem allocation callback.
        unsafe { bindings::rust_helper_inode_alloc_fs(sb) }
    } else {
        // SAFETY: The generic inode cache is initialized before use.
        unsafe { bindings::rust_helper_inode_alloc_slab(sb) }
    };
    if inode.is_null() {
        return inode;
    }

    // SAFETY: The allocation callback returned a live inode object.
    let init_result = unsafe { bindings::rust_helper_inode_init_always(sb, inode) };
    if init_result != 0 || injected_failure(FAIL_INIT) {
        record_init_failure();
        // SAFETY: Failed initialization transfers cleanup ownership here.
        if unsafe { bindings::rust_helper_inode_has_destroy(inode) } {
            // SAFETY: The superblock advertises a destroy callback.
            unsafe { bindings::rust_helper_inode_call_destroy(inode) };
            // SAFETY: The callback returned and the allocation is still live.
            if !unsafe { bindings::rust_helper_inode_has_free(inode) } {
                return ptr::null_mut();
            }
        }
        // SAFETY: The inode is still owned by this failed allocation path.
        unsafe { bindings::rust_helper_inode_set_free(inode) };
        record_free_now();
        // SAFETY: No RCU reader can observe a never-published inode.
        unsafe { bindings::rust_helper_inode_free_now(inode) };
        return ptr::null_mut();
    }

    inode
}

/// Allocates and initializes an inode.
///
/// # Safety
///
/// `sb` must remain live for the duration of the allocation.
#[export]
unsafe extern "C" fn alloc_inode(sb: *mut SuperBlock) -> *mut Inode {
    // SAFETY: The caller guarantees a live superblock.
    unsafe { alloc_inode_internal(sb) }
}

/// Allocates an inode and adds it to the superblock inode list.
///
/// # Safety
///
/// `sb` must remain live for the duration of the allocation.
#[export]
unsafe extern "C" fn new_inode(sb: *mut SuperBlock) -> *mut Inode {
    // SAFETY: The caller guarantees a live superblock.
    let inode = unsafe { alloc_inode_internal(sb) };
    if !inode.is_null() {
        // SAFETY: Initialization completed and the inode is not published.
        unsafe { bindings::inode_sb_list_add(inode) };
    }
    inode
}

/// Adds an unused clean inode to its superblock LRU when eligible.
///
/// # Safety
///
/// `inode` must remain live and its `i_lock` must be held.
unsafe fn inode_lru_list_add_internal(inode: *mut Inode, rotate: bool) {
    // SAFETY: The caller holds `i_lock`.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
    if state & (I_DIRTY_ALL | I_SYNC | I_FREEING | I_WILL_FREE) != 0 {
        return;
    }
    // SAFETY: The caller holds `i_lock`.
    if unsafe { bindings::rust_helper_inode_count_read(inode) } != 0 {
        return;
    }
    // SAFETY: The caller guarantees a live inode.
    if !unsafe { bindings::rust_helper_inode_sb_active(inode) } {
        return;
    }
    // SAFETY: The caller guarantees a live inode.
    if !unsafe { bindings::rust_helper_inode_mapping_shrinkable(inode) } {
        return;
    }

    // SAFETY: The caller holds the lock required by the list-LRU API.
    if unsafe { bindings::rust_helper_inode_lru_add(inode) } {
        // SAFETY: The inode entered the unused LRU.
        unsafe { bindings::rust_helper_inode_account_unused(1) };
    } else if rotate {
        // SAFETY: The caller holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_set(inode, I_REFERENCED) };
    }
}

/// Adds an unused clean inode to the superblock LRU when eligible.
///
/// # Safety
///
/// `inode` must remain live and its `i_lock` must be held.
#[export]
unsafe extern "C" fn inode_lru_list_add(inode: *mut Inode) {
    // SAFETY: The caller holds `i_lock`.
    unsafe { inode_lru_list_add_internal(inode, false) };
}

/// Removes an inode from the unused LRU.
///
/// # Safety
///
/// `inode` must remain live and its `i_lock` must be held.
unsafe fn inode_lru_list_del(inode: *mut Inode) {
    // SAFETY: The caller guarantees a live inode.
    if unsafe { bindings::rust_helper_inode_lru_empty(inode) } {
        return;
    }
    // SAFETY: The caller holds the lock required by the list-LRU API.
    if unsafe { bindings::rust_helper_inode_lru_del(inode) } {
        // SAFETY: The inode left the unused LRU.
        unsafe { bindings::rust_helper_inode_account_unused(-1) };
    }
}

/// Removes an inode from the LRU for the C superblock teardown walk.
///
/// # Safety
///
/// `inode` must remain live and its `i_lock` must be held.
#[export]
unsafe extern "C" fn rust_inode_lru_list_del(inode: *mut Inode) {
    // SAFETY: The caller holds `i_lock`.
    unsafe { inode_lru_list_del(inode) };
}

/// Releases final inode storage after all teardown callbacks complete.
///
/// # Safety
///
/// `inode` must be fully evicted and owned by this final release path.
unsafe fn destroy_inode_internal(inode: *mut Inode) {
    record_destroy();
    // SAFETY: The inode remains live for the final-release checks.
    if !unsafe { bindings::rust_helper_inode_lru_empty(inode) } {
        inode_bug();
    }
    // SAFETY: The inode is no longer reachable by new users.
    unsafe { bindings::rust_helper_inode_destroy_common(inode) };
    // SAFETY: The inode remains allocated while its superblock is live.
    if unsafe { bindings::rust_helper_inode_has_destroy(inode) } {
        // SAFETY: The superblock advertises a destroy callback.
        unsafe { bindings::rust_helper_inode_call_destroy(inode) };
        // SAFETY: The callback returned and the object is still live.
        if !unsafe { bindings::rust_helper_inode_has_free(inode) } {
            return;
        }
    }
    // SAFETY: This is the final release owner.
    unsafe { bindings::rust_helper_inode_set_free(inode) };
    record_free_scheduled();
    // SAFETY: RCU readers may still hold the inode until a grace period.
    unsafe { bindings::rust_helper_inode_schedule_free(inode) };
}

/// Releases final inode storage for a C-owned eviction path.
///
/// # Safety
///
/// `inode` must be fully evicted and owned by this final release path.
#[export]
unsafe extern "C" fn rust_inode_destroy(inode: *mut Inode) {
    // SAFETY: The caller transfers final release ownership.
    unsafe { destroy_inode_internal(inode) };
}

/// Evicts an inode and all state attached to it.
///
/// # Safety
///
/// `inode` must be marked `I_FREEING`, absent from the LRU, and owned by the
/// final release path.
unsafe fn evict(inode: *mut Inode) {
    // SAFETY: The caller keeps the inode live through eviction.
    let state = unsafe { bindings::rust_helper_inode_state_read_once(inode) };
    if state & I_FREEING == 0 {
        inode_bug();
    }
    // SAFETY: The caller keeps the inode live through eviction.
    if !unsafe { bindings::rust_helper_inode_lru_empty(inode) } {
        inode_bug();
    }
    // SAFETY: The inode remains live during eviction.
    unsafe { bindings::rust_helper_inode_io_list_del(inode) };
    // SAFETY: The inode remains live during eviction.
    unsafe { bindings::rust_helper_inode_sb_list_del(inode) };
    // SAFETY: The inode remains live and is not entering the LRU again.
    unsafe { bindings::rust_helper_inode_lock(inode) };
    // SAFETY: This function holds `i_lock`.
    unsafe { bindings::rust_helper_inode_wait_lru_isolating(inode) };
    // SAFETY: This function holds `i_lock`.
    unsafe { bindings::rust_helper_inode_wait_writeback(inode) };
    // SAFETY: This function acquired `i_lock`.
    unsafe { bindings::rust_helper_inode_unlock(inode) };

    // SAFETY: The inode is no longer visible to new users.
    if unsafe { bindings::rust_helper_inode_has_evict(inode) } {
        // SAFETY: The superblock advertises an eviction callback.
        unsafe { bindings::rust_helper_inode_call_evict(inode) };
    } else {
        // SAFETY: The generic eviction path owns the inode until return.
        unsafe { bindings::rust_helper_inode_default_evict(inode) };
    }
    // SAFETY: A character-device inode owns a live `cdev` association.
    if unsafe { bindings::rust_helper_inode_has_cdev(inode) } {
        // SAFETY: The inode is still live during eviction.
        unsafe { bindings::rust_helper_inode_forget_cdev(inode) };
    }
    // SAFETY: Hash removal occurs while the inode is unreachable.
    unsafe { bindings::rust_helper_inode_remove_hash(inode) };
    // SAFETY: The inode is still live and now unhashed.
    unsafe { bindings::rust_helper_inode_wake_new(inode) };
    // SAFETY: The inode remains live until final storage release.
    let state = unsafe { bindings::rust_helper_inode_state_read_once(inode) };
    if state != (I_FREEING | I_CLEAR) {
        inode_bug();
    }
    // SAFETY: The inode is fully evicted and owned by this path.
    unsafe { destroy_inode_internal(inode) };
}

/// Finishes the final inode reference.
///
/// # Safety
///
/// `inode` must have zero references and its `i_lock` must be held.
unsafe fn iput_final(inode: *mut Inode) {
    // SAFETY: The caller holds `i_lock`.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
    // SAFETY: This preserves the original I_NEW diagnostic.
    unsafe { bindings::rust_helper_inode_warn_once(state & I_NEW != 0) };
    // SAFETY: The caller holds `i_lock` and transferred the final reference.
    #[cfg(CONFIG_DEBUG_VFS)]
    if unsafe { bindings::rust_helper_inode_count_read(inode) } != 0 {
        vfs_bug_inode(inode);
    }

    // SAFETY: The inode remains live during the drop callback.
    let drop = if unsafe { bindings::rust_helper_inode_has_drop(inode) } {
        // SAFETY: The superblock advertises a drop callback.
        unsafe { bindings::rust_helper_inode_call_drop(inode) }
    } else {
        // SAFETY: The inode remains live and initialized.
        unsafe { bindings::rust_helper_inode_generic_drop(inode) }
    };
    // SAFETY: The caller holds `i_lock` after the callback.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };

    if drop == 0
        && state & I_DONTCACHE == 0
        // SAFETY: The inode remains live and connected to its superblock.
        && unsafe { bindings::rust_helper_inode_sb_active(inode) }
    {
        // SAFETY: The caller holds `i_lock`.
        unsafe { inode_lru_list_add_internal(inode, true) };
        // SAFETY: The inode was retained in the cache.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        return;
    }

    // SAFETY: The callback could have changed the reference count.
    #[cfg(CONFIG_DEBUG_VFS)]
    if unsafe { bindings::rust_helper_inode_count_read(inode) } != 0 {
        vfs_bug_inode(inode);
    }
    if drop != 0 {
        // SAFETY: The caller holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_set(inode, I_FREEING) };
    } else {
        // SAFETY: The caller holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_set(inode, I_WILL_FREE) };
        // SAFETY: Writeback runs outside `i_lock`.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        // SAFETY: The inode is kept live by the current reference owner.
        unsafe { bindings::rust_helper_inode_write_now(inode) };
        // SAFETY: The inode remains live after writeback.
        unsafe { bindings::rust_helper_inode_lock(inode) };
        // SAFETY: The caller now holds `i_lock`.
        let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
        // SAFETY: This preserves the original I_NEW diagnostic.
        unsafe { bindings::rust_helper_inode_warn_once(state & I_NEW != 0) };
        // SAFETY: The caller holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_replace(inode, I_WILL_FREE, I_FREEING) };
    }

    // SAFETY: The caller holds `i_lock`.
    unsafe { inode_lru_list_del(inode) };
    // SAFETY: Eviction runs without `i_lock`.
    unsafe { bindings::rust_helper_inode_unlock(inode) };
    // SAFETY: The inode is now exclusively owned by this eviction path.
    unsafe { evict(inode) };
}

/// Drops one inode reference.
///
/// # Safety
///
/// `inode` must be null or contain a live reference owned by the caller.
#[export]
unsafe extern "C" fn iput(inode: *mut Inode) {
    // SAFETY: This preserves the original sleeping-context contract.
    unsafe { bindings::rust_helper_inode_might_sleep() };
    if inode.is_null() {
        return;
    }

    loop {
        // SAFETY: The caller must not enter with `i_lock`.
        unsafe { bindings::rust_helper_inode_verify_lock_not_held(inode) };
        #[cfg(CONFIG_DEBUG_VFS)]
        {
            // SAFETY: The caller owns a live inode reference.
            let state = unsafe { bindings::rust_helper_inode_state_read_once(inode) };
            if state & (I_FREEING | I_CLEAR) != 0 {
                vfs_bug_inode(inode);
            }
            // SAFETY: The caller owns a live inode reference.
            if unsafe { bindings::rust_helper_inode_count_read_once(inode) } < 1 {
                vfs_bug_inode(inode);
            }
        }
        // SAFETY: The caller owns one reference in a live inode.
        if unsafe { bindings::rust_helper_inode_atomic_add_unless_dec(inode) } {
            return;
        }

        // SAFETY: The inode remains live because this path still owns a ref.
        let nlink = unsafe { bindings::rust_helper_inode_nlink(inode) };
        if nlink != 0 {
            // SAFETY: The inode remains live and is not locked.
            if unsafe { bindings::rust_helper_inode_sync_lazytime(inode) } {
                continue;
            }
        }

        // SAFETY: The final-reference transition is serialized by `i_lock`.
        unsafe { bindings::rust_helper_inode_lock(inode) };
        // SAFETY: This function holds `i_lock`.
        let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
        // SAFETY: The inode remains live under `i_lock`.
        let nlink = unsafe { bindings::rust_helper_inode_nlink(inode) };
        if state & I_DIRTY_TIME != 0 && nlink != 0 {
            // SAFETY: This function acquired `i_lock`.
            unsafe { bindings::rust_helper_inode_unlock(inode) };
            continue;
        }
        // SAFETY: The caller owns the potential final reference under `i_lock`.
        if !unsafe { bindings::rust_helper_inode_atomic_dec_and_test(inode) } {
            // SAFETY: This function acquired `i_lock`.
            unsafe { bindings::rust_helper_inode_unlock(inode) };
            return;
        }

        // SAFETY: `i_lock` is held and the reference count is zero.
        unsafe { iput_final(inode) };
        return;
    }
}

/// Adds a reference to an inode already held by the caller.
///
/// # Safety
///
/// `inode` must be live and the caller must already own one reference.
#[export]
unsafe extern "C" fn ihold(inode: *mut Inode) {
    #[cfg(CONFIG_DEBUG_VFS)]
    {
        // SAFETY: The caller owns a live reference.
        if unsafe { bindings::rust_helper_inode_count_read_once(inode) } < 1 {
            vfs_bug_inode(inode);
        }
    }
    // SAFETY: The caller owns a live reference.
    let count = unsafe { bindings::rust_helper_inode_atomic_inc_return(inode) };
    // SAFETY: This preserves the original underflow diagnostic.
    unsafe { bindings::rust_helper_inode_warn_once(count < 2) };
}

/// Drops one reference with a guarantee that it is not the final reference.
///
/// # Safety
///
/// `inode` must be live and the caller must own at least two references.
#[export]
unsafe extern "C" fn iput_not_last(inode: *mut Inode) {
    #[cfg(CONFIG_DEBUG_VFS)]
    {
        // SAFETY: The caller owns a live inode reference.
        let state = unsafe { bindings::rust_helper_inode_state_read_once(inode) };
        if state & (I_FREEING | I_CLEAR) != 0 {
            vfs_bug_inode(inode);
        }
        // SAFETY: The caller owns at least two references.
        if unsafe { bindings::rust_helper_inode_count_read_once(inode) } < 2 {
            vfs_bug_inode(inode);
        }
    }
    // SAFETY: The caller owns two references, so one can be dropped.
    let count = unsafe { bindings::rust_helper_inode_atomic_sub_return(inode) };
    // SAFETY: This preserves the original final-reference diagnostic.
    unsafe { bindings::rust_helper_inode_warn_once(count == 0) };
}

/// Disposes every inode on a private eviction list.
///
/// # Safety
///
/// `head` must contain inodes marked `I_FREEING` and isolated from the LRU.
unsafe fn dispose_list(head: *mut ListHead) {
    // SAFETY: The caller guarantees a private live list.
    while !unsafe { bindings::rust_helper_inode_list_empty(head) } {
        // SAFETY: The list is non-empty and contains inodes through `i_lru`.
        let inode = unsafe { bindings::rust_helper_inode_list_first(head) };
        // SAFETY: The private list owns the `i_lru` membership.
        unsafe { bindings::rust_helper_inode_list_del_init(ptr::addr_of_mut!((*inode).i_lru)) };
        // SAFETY: The inode was removed from the private list.
        unsafe { evict(inode) };
        // SAFETY: This preserves the original scheduling point.
        unsafe { bindings::rust_helper_inode_cond_resched() };
    }
}

/// Disposes a C-owned private eviction list.
///
/// # Safety
///
/// `head` must contain inodes marked `I_FREEING` and isolated from the LRU.
#[export]
unsafe extern "C" fn rust_inode_dispose_list(head: *mut ListHead) {
    // SAFETY: The caller guarantees the private-list contract.
    unsafe { dispose_list(head) };
}

/// Isolates reclaimable inodes from the superblock LRU.
///
/// # Safety
///
/// The list-LRU walker supplies a live item and holds the required LRU lock.
#[export]
unsafe extern "C" fn rust_inode_lru_isolate(
    item: *mut ListHead,
    lru: *mut ListLruOne,
    arg: *mut core::ffi::c_void,
) -> bindings::lru_status {
    // SAFETY: The LRU contains inodes through their `i_lru` member.
    let inode = unsafe { crate::container_of!(item, Inode, i_lru) };
    // SAFETY: The LRU walker guarantees inode lifetime.
    if !unsafe { bindings::rust_helper_inode_trylock(inode) } {
        return bindings::lru_status::LRU_SKIP;
    }
    // SAFETY: This callback holds `i_lock`.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
    if state & I_LRU_ISOLATING != 0 {
        // SAFETY: This callback acquired `i_lock`.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        return bindings::lru_status::LRU_SKIP;
    }

    // SAFETY: This callback holds `i_lock`.
    let count = unsafe { bindings::rust_helper_inode_count_read(inode) };
    // SAFETY: This callback holds `i_lock`.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
    // SAFETY: This callback holds `i_lock`.
    let shrinkable = unsafe { bindings::rust_helper_inode_mapping_shrinkable(inode) };
    if count != 0 || state & !I_REFERENCED != 0 || !shrinkable {
        // SAFETY: The LRU walker holds the list lock.
        unsafe { bindings::list_lru_isolate(lru, ptr::addr_of_mut!((*inode).i_lru)) };
        // SAFETY: This callback acquired `i_lock`.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        // SAFETY: The inode left the unused LRU.
        unsafe { bindings::rust_helper_inode_account_unused(-1) };
        return bindings::lru_status::LRU_REMOVED;
    }

    if state & I_REFERENCED != 0 {
        // SAFETY: This callback holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_clear(inode, I_REFERENCED) };
        // SAFETY: This callback acquired `i_lock`.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        return bindings::lru_status::LRU_ROTATE;
    }

    // SAFETY: This callback holds `i_lock`.
    if !unsafe { bindings::rust_helper_inode_mapping_empty(inode) } {
        // SAFETY: This callback holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_set(inode, I_LRU_ISOLATING) };
        // SAFETY: The helper releases both locks during page-cache reclaim and
        // reacquires `i_lock` before return.
        unsafe { bindings::rust_helper_inode_reclaim_mapping(inode, lru) };
        // SAFETY: The helper returned with `i_lock` held.
        if !unsafe { bindings::rust_helper_inode_mapping_empty(inode) } {
            // SAFETY: This callback holds `i_lock`.
            unsafe { bindings::rust_helper_inode_state_set(inode, I_REFERENCED) };
        }
        // SAFETY: This callback holds `i_lock`.
        unsafe { bindings::rust_helper_inode_state_clear(inode, I_LRU_ISOLATING) };
        // SAFETY: The inode is about to release `i_lock`.
        unsafe { bindings::rust_helper_inode_wake_lru_isolating(inode) };
        // SAFETY: The LRU walk restarts after the retry status.
        unsafe { bindings::rust_helper_inode_unlock(inode) };
        return bindings::lru_status::LRU_RETRY;
    }

    // SAFETY: This callback holds `i_lock`.
    let state = unsafe { bindings::rust_helper_inode_state_read(inode) };
    // SAFETY: This preserves the original allocation-state diagnostic.
    unsafe { bindings::rust_helper_inode_warn_once(state & I_NEW != 0) };
    // SAFETY: This callback holds `i_lock`.
    unsafe { bindings::rust_helper_inode_state_set(inode, I_FREEING) };
    // SAFETY: The LRU walker holds the list lock.
    unsafe { bindings::rust_helper_inode_lru_isolate_move(lru, inode, arg.cast()) };
    // SAFETY: This callback acquired `i_lock`.
    unsafe { bindings::rust_helper_inode_unlock(inode) };
    // SAFETY: The inode left the unused LRU.
    unsafe { bindings::rust_helper_inode_account_unused(-1) };
    bindings::lru_status::LRU_REMOVED
}

/// Shrinks the superblock inode LRU and disposes isolated inodes.
///
/// # Safety
///
/// `sb` must remain live and `sc` must be a valid shrinker control block.
#[export]
unsafe extern "C" fn prune_icache_sb(sb: *mut SuperBlock, sc: *mut ShrinkControl) -> isize {
    let mut freeable = MaybeUninit::<ListHead>::uninit();
    // SAFETY: The uninitialized list storage has enough space for a list head.
    unsafe { bindings::rust_helper_inode_list_init(freeable.as_mut_ptr()) };
    let freeable = freeable.as_mut_ptr();
    // SAFETY: The caller supplies a live superblock and shrinker control block.
    let freed = unsafe { bindings::rust_helper_inode_prune_lru(sb, sc, freeable) };
    // SAFETY: The walk left all isolated inodes on the private list.
    unsafe { dispose_list(freeable) };
    freed as isize
}
