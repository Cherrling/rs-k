// SPDX-License-Identifier: GPL-2.0

//! Native superblock lifecycle.
//!
//! Rust owns allocation rollback, initial setup sequencing, lookup and reuse,
//! publication, active and passive reference transitions, deactivation, death
//! notification, and final RCU/workqueue destruction. C retains the shared
//! layouts, slab, locks, LSM and fsnotify cleanup, shrinker and LRU primitives,
//! superblock device table, and filesystem callbacks.

use crate::bindings;
#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};
use core::{ffi::c_int, ffi::c_uint, ptr};
use macros::export;

type FileSystemType = bindings::file_system_type;
type FsContext = bindings::fs_context;
type RcuHead = bindings::callback_head;
type SuperBlock = bindings::super_block;
type UserNamespace = bindings::user_namespace;
type WorkStruct = bindings::work_struct;

type SuperCallback = Option<unsafe extern "C" fn(*mut SuperBlock, *mut FsContext) -> c_int>;

const FAIL_ALLOC: u32 = 1;
const FAIL_INIT: u32 = 2;
const FAIL_SECURITY: u32 = 3;
const FAIL_WRITER_BASE: u32 = 4;
const FAIL_SHRINKER: u32 = FAIL_WRITER_BASE + bindings::SB_FREEZE_LEVELS;
const FAIL_DENTRY_LRU: u32 = FAIL_SHRINKER + 1;
const FAIL_INODE_LRU: u32 = FAIL_DENTRY_LRU + 1;
const FAIL_DEV: u32 = FAIL_INODE_LRU + 1;

const COUNTER_ALLOC: usize = 0;
const COUNTER_ALLOC_FAILURE: usize = 1;
const COUNTER_DESTROY_WORK: usize = 2;
const COUNTER_UNUSED: usize = 3;
const COUNTER_SGET_NEW: usize = 4;
const COUNTER_SGET_SHARE: usize = 5;
const COUNTER_KILL_NOTIFY: usize = 6;

#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
static COUNTERS: [AtomicU32; 7] = [
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
fn record_counter(index: usize) {
    COUNTERS[index].fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST))]
fn record_counter(_index: usize) {}

fn record_alloc() {
    record_counter(COUNTER_ALLOC);
}

fn record_alloc_failure() {
    record_counter(COUNTER_ALLOC_FAILURE);
}

fn record_destroy_work() {
    record_counter(COUNTER_DESTROY_WORK);
}

fn record_unused() {
    record_counter(COUNTER_UNUSED);
}

fn record_sget_new() {
    record_counter(COUNTER_SGET_NEW);
}

fn record_sget_share() {
    record_counter(COUNTER_SGET_SHARE);
}

fn record_kill_notify() {
    record_counter(COUNTER_KILL_NOTIFY);
}

#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

fn injected_failure(stage: u32) -> bool {
    requested_failure(stage)
}

/// Returns up to seven lifecycle counters for the KUnit harness.
#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_super_test_counter(index: c_uint) -> c_uint {
    match index as usize {
        COUNTER_ALLOC
        | COUNTER_ALLOC_FAILURE
        | COUNTER_DESTROY_WORK
        | COUNTER_UNUSED
        | COUNTER_SGET_NEW
        | COUNTER_SGET_SHARE
        | COUNTER_KILL_NOTIFY => COUNTERS[index as usize].load(Ordering::Relaxed),
        _ => 0,
    }
}

/// Clears the KUnit lifecycle counters.
#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_super_test_reset() {
    NEXT_FAILURE.store(0, Ordering::Relaxed);
    for counter in &COUNTERS {
        counter.store(0, Ordering::Relaxed);
    }
}

/// Arms one KUnit failure stage.
#[cfg(CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_super_test_fail(stage: c_uint) {
    NEXT_FAILURE.store(stage, Ordering::Relaxed);
}

/// Releases all resources owned by a fully dead superblock.
///
/// # Safety
///
/// `superblock` must be fully unreachable and owned by this final worker.
unsafe fn destroy_super_work_internal(superblock: *mut SuperBlock) {
    record_destroy_work();
    // SAFETY: The final worker owns the unreachable superblock.
    unsafe { bindings::rust_helper_super_free_fsnotify(superblock) };
    // SAFETY: The final worker owns the unreachable superblock.
    unsafe { bindings::rust_helper_super_free_security(superblock) };
    // SAFETY: The final worker owns the unreachable superblock.
    unsafe { bindings::rust_helper_super_put_user_ns(superblock) };
    // SAFETY: The final worker owns the unreachable superblock.
    unsafe { bindings::rust_helper_super_free_dev(superblock) };
    // SAFETY: The final worker owns the unreachable superblock.
    unsafe { bindings::rust_helper_super_free_subtype(superblock) };
    for index in 0..bindings::SB_FREEZE_LEVELS {
        // SAFETY: The final worker owns every writer semaphore.
        unsafe { bindings::rust_helper_super_free_writer(superblock, index) };
    }
    // SAFETY: All contained resources have been released.
    unsafe { bindings::rust_helper_super_free(superblock) };
}

/// Runs the final superblock release from a workqueue.
///
/// # Safety
///
/// `work` must be the `destroy_work` member of a superblock owned by this
/// callback.
unsafe extern "C" fn super_destroy_work(work: *mut WorkStruct) {
    // SAFETY: The work item is embedded in an unreachable superblock.
    let superblock = unsafe { crate::container_of!(work, SuperBlock, destroy_work) };
    // SAFETY: The final work item owns the superblock.
    unsafe { destroy_super_work_internal(superblock) };
}

/// Schedules final release after an RCU grace period.
///
/// # Safety
///
/// `head` must be the `rcu` member of a superblock owned by this callback.
unsafe extern "C" fn super_destroy_rcu(head: *mut RcuHead) {
    // SAFETY: The RCU head is embedded in an unreachable superblock.
    let superblock = unsafe { crate::container_of!(head, SuperBlock, rcu) };
    // SAFETY: The callback owns the superblock until the worker runs.
    unsafe { bindings::rust_helper_super_init_destroy_work(superblock, Some(super_destroy_work)) };
    // SAFETY: The work item is initialized and the superblock stays live.
    unsafe { bindings::rust_helper_super_schedule_destroy_work(superblock) };
}

/// Destroys a superblock that was never published.
///
/// # Safety
///
/// `superblock` must be null or a newly allocated superblock whose exclusive
/// lock is held and which was never visible to another user.
unsafe fn destroy_unused_super_internal(superblock: *mut SuperBlock) {
    if superblock.is_null() {
        return;
    }
    record_unused();
    // SAFETY: The caller owns the unpublished superblock and its lock.
    unsafe { bindings::rust_helper_super_unlock_excl(superblock) };
    // SAFETY: The caller owns every unpublished LRU and shrinker.
    unsafe { bindings::rust_helper_super_destroy_lrus(superblock) };
    // SAFETY: The caller owns the unpublished shrinker.
    unsafe { bindings::rust_helper_super_free_shrinker(superblock) };
    // SAFETY: No delay is needed before the unpublished object is freed.
    unsafe { destroy_super_work_internal(superblock) };
}

/// Releases a partially initialized superblock and returns a null pointer.
///
/// # Safety
///
/// `superblock` must be null or an unpublished superblock whose exclusive
/// lock is held by this allocation path.
unsafe fn super_setup_failure(superblock: *mut SuperBlock) -> *mut SuperBlock {
    record_alloc_failure();
    // SAFETY: The allocation path still owns the partially initialized object.
    unsafe { destroy_unused_super_internal(superblock) };
    ptr::null_mut()
}

/// Allocates and initializes a new unpublished superblock.
///
/// # Safety
///
/// `filesystem_type` and `user_namespace` must remain live for the call.
unsafe fn alloc_super(
    filesystem_type: *mut FileSystemType,
    flags: c_int,
    user_namespace: *mut UserNamespace,
) -> *mut SuperBlock {
    record_alloc();
    if injected_failure(FAIL_ALLOC) {
        record_alloc_failure();
        return ptr::null_mut();
    }

    // SAFETY: The generic superblock cache is initialized before use.
    let superblock = unsafe { bindings::rust_helper_super_alloc_slab() };
    if superblock.is_null() {
        return superblock;
    }

    // SAFETY: The caller guarantees a live user namespace.
    let user_namespace = unsafe { bindings::rust_helper_super_get_user_ns(user_namespace) };
    // SAFETY: The new superblock is owned by this allocation path.
    unsafe { bindings::rust_helper_super_set_user_ns(superblock, user_namespace) };
    // SAFETY: The allocation path owns the new superblock.
    unsafe { bindings::rust_helper_super_init_umount(superblock, filesystem_type) };
    // SAFETY: The allocation path owns the new superblock.
    unsafe { bindings::rust_helper_super_lock_alloc(superblock) };

    if injected_failure(FAIL_INIT) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }

    if injected_failure(FAIL_SECURITY) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The caller guarantees a live filesystem type.
    if unsafe { bindings::rust_helper_super_security_alloc(superblock) } != 0 {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }

    for index in 0..bindings::SB_FREEZE_LEVELS {
        if injected_failure(FAIL_WRITER_BASE + index) {
            // SAFETY: The new superblock is owned by this allocation path.
            return unsafe { super_setup_failure(superblock) };
        }
        // SAFETY: This allocation path owns the new writer semaphore.
        if unsafe { bindings::rust_helper_super_init_writer(superblock, filesystem_type, index) }
            != 0
        {
            // SAFETY: The new superblock is owned by this allocation path.
            return unsafe { super_setup_failure(superblock) };
        }
    }

    // SAFETY: The allocation path owns the new superblock.
    unsafe { bindings::rust_helper_super_set_bdi_flags(superblock, flags as usize) };
    // SAFETY: The initial namespace pointer remains live.
    let init_user_ns = unsafe { bindings::rust_helper_super_init_user_ns() };
    if user_namespace != init_user_ns {
        // SAFETY: A non-initial user namespace requires SB_I_NODEV.
        unsafe { bindings::rust_helper_super_set_nodev(superblock) };
    }
    // SAFETY: The allocation path owns the uninitialized embedded objects.
    unsafe { bindings::rust_helper_super_init_layout(superblock) };
    // SAFETY: The allocation path owns the new superblock.
    unsafe { bindings::rust_helper_super_fserror_mount(superblock) };
    // SAFETY: The allocation path owns the new reference counts.
    unsafe { bindings::rust_helper_super_set_refs(superblock) };
    // SAFETY: The allocation path owns the rename mutex and quota semaphore.
    unsafe { bindings::rust_helper_super_init_rename_quota(superblock, filesystem_type) };
    // SAFETY: The allocation path owns the uninitialized limits and callbacks.
    unsafe { bindings::rust_helper_super_set_limits(superblock) };

    if injected_failure(FAIL_SHRINKER) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The caller guarantees a live filesystem type.
    if !unsafe { bindings::rust_helper_super_alloc_shrinker(superblock, filesystem_type) } {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    if injected_failure(FAIL_DENTRY_LRU) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The shrinker is initialized and owned by the new superblock.
    if unsafe { bindings::rust_helper_super_init_dentry_lru(superblock) } != 0 {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    if injected_failure(FAIL_INODE_LRU) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The shrinker is initialized and owned by the new superblock.
    if unsafe { bindings::rust_helper_super_init_inode_lru(superblock) } != 0 {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    if injected_failure(FAIL_DEV) {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The new superblock is owned by this allocation path.
    if !unsafe { bindings::rust_helper_super_alloc_dev(superblock) } {
        // SAFETY: The new superblock is owned by this allocation path.
        return unsafe { super_setup_failure(superblock) };
    }
    // SAFETY: The new superblock is fully initialized.
    unsafe { bindings::rust_helper_super_set_min_writeback_pages(superblock) };
    superblock
}

/// Drops a future notification for a superblock that is already dead.
///
/// # Safety
///
/// `superblock` must remain live and `s_umount` must not be held.
unsafe fn kill_super_notify_internal(superblock: *mut SuperBlock) {
    record_kill_notify();
    // SAFETY: The caller satisfies the notification locking contract.
    unsafe { bindings::rust_helper_super_lockdep_assert_unlocked(superblock) };
    // SAFETY: The superblock remains live during notification.
    if unsafe { bindings::rust_helper_super_flags(superblock, bindings::SB_DEAD) } {
        return;
    }

    // SAFETY: The superblock remains live during notification.
    if unsafe { bindings::rust_helper_super_has_dev_claim(superblock) } {
        // SAFETY: Notification owns the one device-table claim.
        unsafe { bindings::rust_helper_super_put_dev_claim(superblock) };
    }

    // SAFETY: SB_DEAD publication is serialized by the global superblock lock.
    unsafe { bindings::rust_helper_super_lock_global() };
    // SAFETY: This call holds the global superblock lock.
    unsafe { bindings::rust_helper_super_wake(superblock, bindings::SB_DEAD) };
    // SAFETY: This call acquired the global superblock lock.
    unsafe { bindings::rust_helper_super_unlock_global() };
}

/// Announces that a superblock has reached SB_DEAD.
///
/// # Safety
///
/// `superblock` must remain live and `s_umount` must not be held.
#[export]
unsafe extern "C" fn rust_kill_super_notify(superblock: *mut SuperBlock) {
    // SAFETY: The caller satisfies the notification contract.
    unsafe { kill_super_notify_internal(superblock) };
}

/// Drops the final passive reference and schedules release.
///
/// # Safety
///
/// `superblock` must contain a live passive reference and the global
/// superblock lock must not be held.
unsafe fn put_super_internal(superblock: *mut SuperBlock) {
    // SAFETY: The caller owns one passive reference.
    if !unsafe { bindings::rust_helper_super_passive_dec_and_test(superblock) } {
        return;
    }

    // SAFETY: The superblock remains live until its RCU callback runs.
    let filesystem_type = unsafe { bindings::rust_helper_super_type(superblock) };
    // SAFETY: The global list and instance list are protected by this lock.
    unsafe { bindings::rust_helper_super_lock_global() };
    // SAFETY: The lock is held.
    unsafe { bindings::rust_helper_super_unlink_global(superblock) };
    // SAFETY: This call acquired the global superblock lock.
    unsafe { bindings::rust_helper_super_unlock_global() };
    // SAFETY: No LRU or mount may remain at the last passive reference.
    unsafe { bindings::rust_helper_super_warn_teardown(superblock) };
    // SAFETY: The superblock is unreachable and can wait for an RCU grace period.
    unsafe { bindings::rust_helper_super_call_rcu(superblock, Some(super_destroy_rcu)) };
    // SAFETY: The global instance list no longer refers to this filesystem type.
    unsafe { bindings::rust_helper_super_put_filesystem(filesystem_type) };
}

/// Drops one passive superblock reference.
///
/// # Safety
///
/// `superblock` must contain a live passive reference and must not be
/// protected by the global superblock lock.
#[export]
unsafe extern "C" fn put_super(superblock: *mut SuperBlock) {
    // SAFETY: The caller satisfies the passive-reference contract.
    unsafe { put_super_internal(superblock) };
}

/// Releases the final active reference while `s_umount` is held.
///
/// # Safety
///
/// `superblock` must contain a live active reference and its exclusive lock
/// must be held. The call consumes the lock.
unsafe fn deactivate_locked_super_internal(superblock: *mut SuperBlock) {
    // SAFETY: The caller owns one active reference.
    if unsafe { bindings::rust_helper_super_active_dec_and_test(superblock) } {
        // SAFETY: No active user remains to access the shrinker.
        unsafe { bindings::rust_helper_super_free_shrinker(superblock) };
        // SAFETY: The filesystem owns shutdown of non-VFS superblock state.
        unsafe { bindings::rust_helper_super_kill_sb(superblock) };
        // SAFETY: The filesystem shutdown completed and released `s_umount`.
        unsafe { kill_super_notify_internal(superblock) };
        // SAFETY: No user remains, and LRU destruction may sleep.
        unsafe { bindings::rust_helper_super_destroy_lrus(superblock) };
        // SAFETY: The initial passive reference is the last one.
        unsafe { put_super_internal(superblock) };
    } else {
        // SAFETY: The caller transferred the exclusive lock.
        unsafe { bindings::rust_helper_super_unlock_excl(superblock) };
    }
}

/// Drops one active superblock reference with `s_umount` held.
///
/// # Safety
///
/// `superblock` must contain a live active reference and its exclusive lock
/// must be held. The call consumes the lock.
#[export]
unsafe extern "C" fn deactivate_locked_super(superblock: *mut SuperBlock) {
    // SAFETY: The caller satisfies the active-reference contract.
    unsafe { deactivate_locked_super_internal(superblock) };
}

/// Drops one active superblock reference without holding `s_umount`.
///
/// # Safety
///
/// `superblock` must contain a live active reference, and `s_umount` must not
/// be held.
#[export]
unsafe extern "C" fn deactivate_super(superblock: *mut SuperBlock) {
    // SAFETY: The caller owns one active reference.
    if !unsafe { bindings::rust_helper_super_active_dec_not_last(superblock) } {
        // SAFETY: The final active reference requires the exclusive lock.
        unsafe { bindings::rust_helper_super_lock_excl_unconditional(superblock) };
        // SAFETY: This path now holds the lock and the final active reference.
        unsafe { deactivate_locked_super_internal(superblock) };
    }
}

/// Trades a passive reference for an active reference.
///
/// # Safety
///
/// `superblock` must contain a passive reference and the global superblock
/// lock must be held. This function releases that lock.
unsafe fn grab_super(superblock: *mut SuperBlock) -> bool {
    // SAFETY: The caller guarantees a live passive reference.
    unsafe { bindings::rust_helper_super_passive_inc(superblock) };
    // SAFETY: The caller transferred the global superblock lock.
    unsafe { bindings::rust_helper_super_unlock_global() };
    // SAFETY: This path holds a temporary passive reference.
    let locked = unsafe { bindings::rust_helper_super_lock_excl(superblock) };
    if locked {
        // SAFETY: The temporary reference prevents final release.
        if unsafe { bindings::rust_helper_super_active_inc_not_zero(superblock) } {
            // SAFETY: The caller's temporary reference is no longer needed.
            unsafe { put_super_internal(superblock) };
            return true;
        }
        // SAFETY: No active reference exists.
        unsafe { bindings::rust_helper_super_unlock_excl(superblock) };
    }
    // SAFETY: The temporary reference keeps the superblock alive until dead.
    unsafe { bindings::rust_helper_super_wait_dead(superblock) };
    // SAFETY: The temporary reference is no longer needed.
    unsafe { put_super_internal(superblock) };
    false
}

/// Finds or creates a superblock.
///
/// # Safety
///
/// `filesystem_context`, its filesystem type, and both callbacks must remain
/// valid for the call. `set` must not be null.
unsafe fn sget_fc_internal(
    filesystem_context: *mut FsContext,
    test: SuperCallback,
    set: SuperCallback,
) -> *mut SuperBlock {
    // SAFETY: The caller guarantees a live filesystem context.
    let user_namespace = if unsafe { bindings::rust_helper_super_fc_global(filesystem_context) } {
        // SAFETY: The initial user namespace is always live.
        unsafe { bindings::rust_helper_super_init_user_ns() }
    } else {
        // SAFETY: The caller guarantees a valid context namespace.
        unsafe { bindings::rust_helper_super_fc_user_ns(filesystem_context) }
    };
    // SAFETY: The caller guarantees a live filesystem type.
    let filesystem_type = unsafe { bindings::rust_helper_super_fc_type(filesystem_context) };
    // SAFETY: The caller guarantees a live filesystem type.
    let init_user_ns = unsafe { bindings::rust_helper_super_init_user_ns() };
    if user_namespace != init_user_ns {
        // SAFETY: The caller guarantees a live filesystem type.
        let mount_flags = unsafe { bindings::rust_helper_super_fs_type_flags(filesystem_type) };
        let allowed = bindings::FS_USERNS_MOUNT | bindings::FS_USERNS_DELEGATABLE;
        if mount_flags as u32 & allowed == 0 {
            // SAFETY: The caller guarantees a live filesystem context.
            unsafe { bindings::rust_helper_super_fc_error_userns(filesystem_context) };
            // SAFETY: Construct the kernel error-pointer representation.
            return unsafe { bindings::rust_helper_super_error_ptr(-(bindings::EPERM as c_int)) };
        }
    }

    let mut new_superblock: *mut SuperBlock = ptr::null_mut();
    loop {
        // SAFETY: This path serializes lookup and publication.
        unsafe { bindings::rust_helper_super_lock_global() };
        let mut old_superblock: *mut SuperBlock = ptr::null_mut();
        if let Some(test_callback) = test {
            // SAFETY: The global lock protects the filesystem instance list.
            old_superblock = unsafe { bindings::rust_helper_super_first_sb(filesystem_type) };
            while !old_superblock.is_null() {
                // SAFETY: The global lock protects these superblocks.
                if !unsafe { bindings::rust_helper_super_dead(old_superblock) }
                    // SAFETY: The test callback is valid for this call.
                    && unsafe {
                        bindings::rust_helper_super_call_test(
                            Some(test_callback),
                            old_superblock,
                            filesystem_context,
                        )
                    } != 0
                {
                    break;
                }
                // SAFETY: The global lock protects the instance list.
                old_superblock = unsafe { bindings::rust_helper_super_next_sb(old_superblock) };
            }
        }

        if !old_superblock.is_null() {
            // SAFETY: The old superblock is protected by the global lock.
            let old_user_namespace = unsafe { bindings::rust_helper_super_user_ns(old_superblock) };
            // SAFETY: The caller guarantees a live context.
            let exclusive = unsafe { bindings::rust_helper_super_fc_exclusive(filesystem_context) };
            if user_namespace != old_user_namespace || exclusive {
                // SAFETY: This path acquired the global superblock lock.
                unsafe { bindings::rust_helper_super_unlock_global() };
                // SAFETY: A newly allocated object was never published.
                unsafe { destroy_unused_super_internal(new_superblock) };
                if exclusive {
                    // SAFETY: The caller guarantees a live context.
                    unsafe { bindings::rust_helper_super_fc_warn_exclusive(filesystem_context) };
                } else {
                    // SAFETY: The caller guarantees a live context.
                    unsafe { bindings::rust_helper_super_fc_warn_userns(filesystem_context) };
                }
                // SAFETY: Construct the kernel error-pointer representation.
                return unsafe {
                    bindings::rust_helper_super_error_ptr(-(bindings::EBUSY as c_int))
                };
            }

            // SAFETY: This transfers the global lock and returns with the
            // superblock active when it became ready.
            if unsafe { grab_super(old_superblock) } {
                // SAFETY: An unused object is no longer needed.
                unsafe { destroy_unused_super_internal(new_superblock) };
                record_sget_share();
                return old_superblock;
            }
            continue;
        }

        if new_superblock.is_null() {
            // SAFETY: Allocation does not need the global superblock lock.
            unsafe { bindings::rust_helper_super_unlock_global() };
            // SAFETY: The caller guarantees a live filesystem type and namespace.
            new_superblock = unsafe {
                alloc_super(
                    filesystem_type,
                    bindings::rust_helper_super_fc_sb_flags(filesystem_context) as c_int,
                    user_namespace,
                )
            };
            if new_superblock.is_null() {
                // SAFETY: Construct the kernel error-pointer representation.
                return unsafe {
                    bindings::rust_helper_super_error_ptr(-(bindings::ENOMEM as c_int))
                };
            }
            continue;
        }

        // SAFETY: Both objects are live and the global lock is held.
        unsafe { bindings::rust_helper_super_set_fs_info(new_superblock, filesystem_context) };
        // SAFETY: The caller guarantees a valid set callback and context.
        let error = unsafe {
            bindings::rust_helper_super_call_set(set, new_superblock, filesystem_context)
        };
        if error != 0 {
            // SAFETY: The new superblock is owned by this path.
            let registered = unsafe { bindings::rust_helper_super_dev_registered(new_superblock) };
            // SAFETY: A failed filesystem set callback must not register a device.
            unsafe { bindings::rust_helper_super_warn_once(registered) };
            // SAFETY: The new superblock is owned by this path.
            unsafe { bindings::rust_helper_super_clear_fs_info(new_superblock) };
            // SAFETY: This path acquired the global superblock lock.
            unsafe { bindings::rust_helper_super_unlock_global() };
            // SAFETY: The unpublished object is still owned by this path.
            unsafe { super_setup_failure(new_superblock) };
            // SAFETY: Construct the kernel error-pointer representation.
            return unsafe { bindings::rust_helper_super_error_ptr(error) };
        }

        // SAFETY: The new superblock is owned by this path.
        let registered = unsafe { bindings::rust_helper_super_dev_registered(new_superblock) };
        // SAFETY: A successful set callback must register a device.
        unsafe { bindings::rust_helper_super_warn_once(!registered) };
        // SAFETY: Ownership of the filesystem-private data transferred.
        unsafe { bindings::rust_helper_super_clear_fc_fs_info(filesystem_context) };
        // SAFETY: The new superblock is now ready for publication.
        unsafe { bindings::rust_helper_super_set_type(new_superblock, filesystem_type) };
        // SAFETY: The caller guarantees a live context.
        let iflags = unsafe { bindings::rust_helper_super_fc_iflags(filesystem_context) };
        // SAFETY: The new superblock is still owned by this path.
        unsafe { bindings::rust_helper_super_or_iflags(new_superblock, iflags) };
        // SAFETY: The type and identifier buffers are initialized.
        unsafe { bindings::rust_helper_super_set_id(new_superblock) };
        // SAFETY: Publication is serialized by the global superblock lock.
        unsafe { bindings::rust_helper_super_add_global(new_superblock) };
        // SAFETY: Publication is serialized by the global superblock lock.
        unsafe { bindings::rust_helper_super_add_instances(new_superblock) };
        // SAFETY: This path acquired the global superblock lock.
        unsafe { bindings::rust_helper_super_unlock_global() };
        // SAFETY: The published superblock owns a filesystem-type reference.
        unsafe { bindings::rust_helper_super_get_filesystem(filesystem_type) };
        // SAFETY: The published superblock owns a registered shrinker.
        unsafe { bindings::rust_helper_super_register_shrinker(new_superblock) };
        record_sget_new();
        return new_superblock;
    }
}

/// Finds or creates a superblock.
///
/// # Safety
///
/// `filesystem_context` and its filesystem type must remain live for the call.
/// `set` must not be null.
#[export]
unsafe extern "C" fn sget_fc(
    filesystem_context: *mut FsContext,
    test: SuperCallback,
    set: SuperCallback,
) -> *mut SuperBlock {
    // SAFETY: The caller guarantees valid callbacks and context.
    unsafe { sget_fc_internal(filesystem_context, test, set) }
}
