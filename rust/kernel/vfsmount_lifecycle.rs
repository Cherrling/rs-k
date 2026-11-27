// SPDX-License-Identifier: GPL-2.0

//! Native vfsmount lifecycle.
//!
//! Rust owns mount allocation and rollback, setup, cloning, reference
//! acquisition and release, the final-reference decision, detached cleanup,
//! and the choice between task work and delayed work. C retains the mount
//! layouts, per-CPU reference counters, mount hash and namespace locks,
//! identifiers, dentry and superblock primitives, fsnotify, and propagation
//! primitives.

use crate::bindings;
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
use core::ffi::c_uint;
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use core::{
    ffi::{c_int, c_uchar},
    mem::MaybeUninit,
    ptr,
};
use macros::export;

type CallbackHead = bindings::callback_head;
type Dentry = bindings::dentry;
type FsContext = bindings::fs_context;
type ListHead = bindings::list_head;
type Mount = bindings::mount;
type Path = bindings::path;
type VfsMount = bindings::vfsmount;

const FAIL_ALLOC: u32 = 1;
const FAIL_COVER: u32 = 2;
const FAIL_ID: u32 = 3;
const FAIL_DEVNAME: u32 = 4;
const FAIL_PCP: u32 = 5;
const FAIL_GROUP: u32 = 6;

const COUNTER_ALLOC: usize = 0;
const COUNTER_ALLOC_FAILURE: usize = 1;
const COUNTER_CLEANUP: usize = 2;
const COUNTER_FREE: usize = 3;
const COUNTER_TASK_WORK: usize = 4;
const COUNTER_DELAYED: usize = 5;
const COUNTER_CLONE: usize = 6;

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
static FORCE_TASK_WORK: AtomicBool = AtomicBool::new(false);

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
static COUNTERS: [AtomicU32; 7] = [
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
    AtomicU32::new(0),
];

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
fn record_counter(index: usize) {
    COUNTERS[index].fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST))]
fn record_counter(_index: usize) {}

fn record_alloc() {
    record_counter(COUNTER_ALLOC);
}

fn record_alloc_failure() {
    record_counter(COUNTER_ALLOC_FAILURE);
}

fn record_cleanup() {
    record_counter(COUNTER_CLEANUP);
}

fn record_free() {
    record_counter(COUNTER_FREE);
}

fn record_task_work() {
    record_counter(COUNTER_TASK_WORK);
}

fn record_delayed() {
    record_counter(COUNTER_DELAYED);
}

fn record_clone() {
    record_counter(COUNTER_CLONE);
}

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

fn injected_failure(stage: u32) -> bool {
    requested_failure(stage)
}

#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
fn forced_task_work() -> bool {
    FORCE_TASK_WORK.swap(false, Ordering::AcqRel)
}

#[cfg(not(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST))]
fn forced_task_work() -> bool {
    false
}

fn is_error<T>(value: *const T) -> bool {
    // SAFETY: The helper only classifies the pointer value.
    unsafe { bindings::rust_helper_mnt_is_err(value.cast()) }
}

fn error_ptr<T>(error: c_int) -> *mut T {
    // SAFETY: The helper encodes a negative errno in pointer form.
    unsafe { bindings::rust_helper_mnt_err_ptr(error as isize).cast() }
}

/// Returns one vfsmount lifecycle counter for the KUnit harness.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_counter(index: c_uint) -> c_uint {
    match index as usize {
        COUNTER_ALLOC
        | COUNTER_ALLOC_FAILURE
        | COUNTER_CLEANUP
        | COUNTER_FREE
        | COUNTER_TASK_WORK
        | COUNTER_DELAYED
        | COUNTER_CLONE => COUNTERS[index as usize].load(Ordering::Relaxed),
        _ => 0,
    }
}

/// Clears the KUnit lifecycle counters and failure injection state.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_reset() {
    NEXT_FAILURE.store(0, Ordering::Relaxed);
    FORCE_TASK_WORK.store(false, Ordering::Relaxed);
    for counter in &COUNTERS {
        counter.store(0, Ordering::Relaxed);
    }
}

/// Arms one allocation rollback stage.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_fail_next(stage: c_uint) {
    NEXT_FAILURE.store(stage, Ordering::Relaxed);
}

/// Arms the task-work cleanup path for the next final drop.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_force_task_work() {
    FORCE_TASK_WORK.store(true, Ordering::Release);
}

/// Returns a synchronized mount reference count for KUnit.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_count(mnt: *mut Mount) -> c_int {
    // SAFETY: The caller supplies a live mount.
    unsafe { bindings::rust_helper_mnt_lock_hash() };
    // SAFETY: The mount hash lock serializes the per-CPU count sample.
    let count = unsafe { bindings::rust_helper_mnt_get_count(mnt) };
    // SAFETY: This unlocks the lock acquired above.
    unsafe { bindings::rust_helper_mnt_unlock_hash() };
    count
}

/// Flushes the native delayed mount cleanup work.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_flush_delayed() {
    // SAFETY: The delayed work is initialized before KUnit can call this.
    unsafe { bindings::rust_helper_mnt_flush_delayed() };
}

/// Waits for all queued final-release RCU callbacks.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_mnt_test_synchronize_rcu() {
    // SAFETY: This waits for callbacks registered by the mount lifecycle.
    unsafe { bindings::rust_helper_mnt_synchronize_rcu() };
}

/// Releases the fields and slab object of an unpublished mount.
///
/// # Safety
///
/// `mnt` must be fully initialized and unreachable.
unsafe fn free_vfsmnt_internal(mnt: *mut Mount) {
    record_free();
    // SAFETY: The caller owns the final, unreachable mount.
    unsafe {
        bindings::rust_helper_mnt_idmap_put(mnt);
        bindings::rust_helper_mnt_free_cover(mnt);
        bindings::rust_helper_mnt_free_devname(mnt);
        bindings::rust_helper_mnt_free_pcp(mnt);
        bindings::rust_helper_mnt_free_object(mnt);
    }
}

/// Aborts a partially initialized mount allocation.
///
/// # Safety
///
/// `mnt` is a live slab object owned by this allocation path. The boolean
/// arguments describe exactly which resources were installed in the mount.
unsafe fn abort_alloc(
    mnt: *mut Mount,
    cover: bool,
    id: bool,
    devname: bool,
    pcp: bool,
) -> *mut Mount {
    record_alloc_failure();
    // SAFETY: The caller describes every initialized resource exactly.
    unsafe {
        if pcp {
            bindings::rust_helper_mnt_free_pcp(mnt);
        }
        if devname {
            bindings::rust_helper_mnt_free_devname(mnt);
        }
        if id {
            bindings::rust_helper_mnt_free_id(mnt);
        }
        if cover {
            bindings::rust_helper_mnt_free_cover(mnt);
        }
        bindings::rust_helper_mnt_free_object(mnt);
    }
    record_free();
    ptr::null_mut()
}

/// Allocates and initializes an unpublished vfsmount.
///
/// # Safety
///
/// `name` must be null or point to a live NUL-terminated string.
unsafe fn alloc_vfsmnt(name: *const c_uchar) -> *mut Mount {
    record_alloc();
    if injected_failure(FAIL_ALLOC) {
        record_alloc_failure();
        return ptr::null_mut();
    }

    // SAFETY: The mount slab cache is initialized before this path runs.
    let mnt = unsafe { bindings::rust_helper_mnt_alloc_object() };
    if mnt.is_null() {
        record_alloc_failure();
        return mnt;
    }

    if injected_failure(FAIL_COVER) {
        // SAFETY: Only the slab object has been installed.
        return unsafe { abort_alloc(mnt, false, false, false, false) };
    }
    // SAFETY: The newly allocated mount owns its cover.
    if unsafe { bindings::rust_helper_mnt_alloc_cover(mnt) } != 0 {
        // SAFETY: Only the slab object has been installed.
        return unsafe { abort_alloc(mnt, false, false, false, false) };
    }

    if injected_failure(FAIL_ID) {
        // SAFETY: The cover is installed and no other resource exists.
        return unsafe { abort_alloc(mnt, true, false, false, false) };
    }
    // SAFETY: The cover is installed and the mount owns its identifier.
    let err = unsafe { bindings::rust_helper_mnt_alloc_id(mnt) };
    if err != 0 {
        // SAFETY: The cover is installed and no other resource exists.
        return unsafe { abort_alloc(mnt, true, false, false, false) };
    }

    if injected_failure(FAIL_DEVNAME) {
        // SAFETY: The slab, cover, and identifier are installed.
        return unsafe { abort_alloc(mnt, true, true, false, false) };
    }
    // SAFETY: `name` follows the caller contract.
    let devname = unsafe { bindings::rust_helper_mnt_dup_devname(name) };
    if devname.is_null() {
        // SAFETY: The slab, cover, and identifier are installed.
        return unsafe { abort_alloc(mnt, true, true, false, false) };
    }
    // SAFETY: The returned name and new mount are owned by this path.
    unsafe { bindings::rust_helper_mnt_set_devname(mnt, devname) };

    if injected_failure(FAIL_PCP) {
        // SAFETY: The slab, cover, identifier, and device name are installed.
        return unsafe { abort_alloc(mnt, true, true, true, false) };
    }
    // SAFETY: The mount owns the new per-CPU reference counter.
    if !unsafe { bindings::rust_helper_mnt_alloc_pcp(mnt) } {
        // SAFETY: The slab, cover, identifier, and device name are installed.
        return unsafe { abort_alloc(mnt, true, true, true, false) };
    }

    // SAFETY: The mount has all allocating fields initialized.
    unsafe {
        bindings::rust_helper_mnt_init_layout(mnt);
        bindings::rust_helper_mnt_set_default_idmap(mnt);
    }
    mnt
}

/// Initializes the mounted root, superblock, and instance membership.
///
/// # Safety
///
/// `mnt` must be an unpublished fully allocated mount and `root` must remain
/// live for the call.
unsafe fn setup_mnt(mnt: *mut Mount, root: *mut Dentry) {
    // SAFETY: The dentry holds its inode and superblock for the call.
    let sb = unsafe { bindings::rust_helper_mnt_dentry_super(root) };
    // SAFETY: The new mount takes an active superblock reference.
    unsafe { bindings::rust_helper_mnt_active_inc(sb) };
    // SAFETY: The new mount owns the root reference and superblock reference.
    unsafe {
        bindings::rust_helper_mnt_set_super(mnt, sb);
        bindings::rust_helper_mnt_set_root(mnt, root);
        bindings::rust_helper_mnt_set_parent_self(mnt);
        bindings::rust_helper_mnt_lock_instance();
        bindings::rust_helper_mnt_add_instance(mnt, sb);
        bindings::rust_helper_mnt_unlock_instance();
    }
}

/// Creates a detached mount for a configured superblock.
///
/// # Safety
///
/// `fc` must be a live filesystem context.
#[export]
unsafe extern "C" fn vfs_create_mount(fc: *mut FsContext) -> *mut VfsMount {
    // SAFETY: The caller supplies a live filesystem context.
    let root = unsafe { bindings::rust_helper_mnt_fc_root(fc) };
    if root.is_null() {
        return error_ptr(-(bindings::EINVAL as c_int));
    }

    // SAFETY: The filesystem context remains live through the call.
    let name = unsafe { bindings::rust_helper_mnt_fc_source(fc) };
    // SAFETY: The source is null or NUL-terminated by the filesystem context.
    let mnt = unsafe { alloc_vfsmnt(name) };
    if mnt.is_null() {
        return error_ptr(-(bindings::ENOMEM as c_int));
    }

    // SAFETY: The filesystem context remains live through the call.
    let kern_mount = unsafe { bindings::rust_helper_mnt_fc_kern_mount(fc) };
    if kern_mount {
        // SAFETY: The new mount is owned by this path.
        unsafe {
            bindings::rust_helper_mnt_or_flags(mnt, bindings::mount_flags_MNT_INTERNAL as c_int)
        };
    }

    // SAFETY: The filesystem context retains a live root dentry.
    unsafe { setup_mnt(mnt, root) };
    // SAFETY: The helper computes the embedded vfsmount address.
    unsafe { bindings::rust_helper_mnt_vfsmount(mnt) }
}

/// Clones an existing mount without attaching it to a namespace.
///
/// # Safety
///
/// `old` and `root` must remain live for the call.
#[export]
unsafe extern "C" fn clone_mnt(old: *mut Mount, root: *mut Dentry, flag: c_int) -> *mut Mount {
    record_clone();
    // SAFETY: The old mount remains live for the clone.
    let name = unsafe { bindings::rust_helper_mnt_devname(old) };
    // SAFETY: The old mount and its device name remain live.
    let mnt = unsafe { alloc_vfsmnt(name) };
    if mnt.is_null() {
        return error_ptr(-(bindings::ENOMEM as c_int));
    }

    let private = flag & (bindings::RUST_MNT_CL_PRIVATE as c_int) != 0;
    let slave = flag & (bindings::RUST_MNT_CL_SLAVE as c_int) != 0;
    let make_shared = flag & (bindings::RUST_MNT_CL_MAKE_SHARED as c_int) != 0;

    // SAFETY: Both mounts are live and the source fields are stable.
    let old_flags = unsafe { bindings::rust_helper_mnt_flags(old) };
    // SAFETY: The source mount remains live through the clone.
    let old_t_flags = unsafe { bindings::rust_helper_mnt_t_flags(old) };
    // SAFETY: The source mount remains live through the clone.
    let old_group_id = unsafe { bindings::rust_helper_mnt_group_id(old) };
    // SAFETY: The new mount is fully allocated.
    unsafe {
        bindings::rust_helper_mnt_set_flags(
            mnt,
            old_flags & !(bindings::mount_flags_MNT_INTERNAL_FLAGS as c_int),
        );
        bindings::rust_helper_mnt_set_t_flags(
            mnt,
            old_t_flags & (bindings::RUST_MNT_T_UNBINDABLE as c_int),
        );
    }

    if slave || private {
        // SAFETY: The new mount owns its peer group state.
        unsafe { bindings::rust_helper_mnt_set_group_id(mnt, 0) };
    } else {
        // SAFETY: The new mount shares the source peer group identifier.
        unsafe { bindings::rust_helper_mnt_set_group_id(mnt, old_group_id) };
    }

    // SAFETY: The new mount is fully allocated.
    let group_id = unsafe { bindings::rust_helper_mnt_group_id(mnt) };
    if make_shared && group_id == 0 {
        if injected_failure(FAIL_GROUP) {
            // SAFETY: Every allocation before the peer group is installed.
            unsafe { abort_alloc(mnt, true, true, true, true) };
            return error_ptr(-(bindings::ENOMEM as c_int));
        }
        // SAFETY: The new mount is fully allocated and owns a new group ID.
        let err = unsafe { bindings::rust_helper_mnt_alloc_group_id(mnt) };
        if err != 0 {
            // SAFETY: Every allocation before the peer group is installed.
            unsafe { abort_alloc(mnt, true, true, true, true) };
            return error_ptr(err);
        }
    }

    // SAFETY: The new mount is fully allocated.
    if unsafe { bindings::rust_helper_mnt_group_id(mnt) } != 0 {
        // SAFETY: A nonzero peer group is published as shared.
        unsafe { bindings::rust_helper_mnt_set_shared(mnt) };
    }

    // SAFETY: The old mount's idmap remains live through the reference.
    let idmap = unsafe { bindings::rust_helper_mnt_idmap_get(old) };
    // SAFETY: The new mount owns the acquired idmap reference.
    unsafe { bindings::rust_helper_mnt_set_idmap(mnt, idmap) };
    // SAFETY: The source mount and root remain live for the clone.
    unsafe { setup_mnt(mnt, root) };

    if private {
        return mnt;
    }

    // SAFETY: The source and clone fields are initialized.
    if unsafe { bindings::rust_helper_mnt_peers(mnt, old) } {
        // SAFETY: The clone joins the source peer list.
        unsafe { bindings::rust_helper_mnt_add_peer(mnt, old) };
    }

    // SAFETY: The clone has the source's slave state.
    if slave && old_group_id != 0 {
        // SAFETY: Both mounts are live and initialized.
        unsafe {
            bindings::rust_helper_mnt_add_slave(mnt, old);
            bindings::rust_helper_mnt_set_master(mnt, old);
        }
    // SAFETY: The source mount remains live through the clone.
    } else if unsafe { bindings::rust_helper_mnt_is_slave(old) } {
        // SAFETY: Both mounts are live and initialized.
        unsafe {
            bindings::rust_helper_mnt_add_slave_behind(mnt, old);
            bindings::rust_helper_mnt_set_master(mnt, bindings::rust_helper_mnt_master(old));
        }
    }
    mnt
}

/// Creates a private internal clone of a path's mount.
///
/// # Safety
///
/// `path` and its mount and dentry must remain live for the call.
unsafe extern "C" fn mnt_clone_internal_internal(path: *const Path) -> *mut VfsMount {
    // SAFETY: The caller supplies a live path.
    let old = unsafe { bindings::rust_helper_mnt_path_mount(path) };
    // SAFETY: The caller supplies a live path.
    let root = unsafe { bindings::rust_helper_mnt_path_dentry(path) };
    // SAFETY: The path remains live through the call.
    let old = unsafe { bindings::rust_helper_mnt_from_vfsmount(old) };
    // SAFETY: The source mount and dentry remain live.
    let mnt = unsafe { clone_mnt(old, root, bindings::RUST_MNT_CL_PRIVATE as c_int) };
    if is_error(mnt) {
        return error_ptr(read_error(mnt));
    }
    // SAFETY: The clone is fully initialized and owned by this path.
    unsafe { bindings::rust_helper_mnt_or_flags(mnt, bindings::mount_flags_MNT_INTERNAL as c_int) };
    // SAFETY: The helper computes the embedded vfsmount address.
    unsafe { bindings::rust_helper_mnt_vfsmount(mnt) }
}

fn read_error<T>(value: *const T) -> c_int {
    // SAFETY: The caller only uses this for an error pointer.
    unsafe { bindings::rust_helper_mnt_ptr_err(value.cast()) as c_int }
}

/// Creates a private internal clone of a path's mount.
///
/// # Safety
///
/// `path` and its mount and dentry must remain live for the call.
#[export]
unsafe extern "C" fn mnt_clone_internal(path: *const Path) -> *mut VfsMount {
    // SAFETY: The caller supplies a live path.
    unsafe { mnt_clone_internal_internal(path) }
}

/// Releases the mounted root and all non-RCU dependencies of a mount.
///
/// # Safety
///
/// `mnt` must be detached, unreachable by new lookups, and owned by this
/// cleanup path.
unsafe fn cleanup_mnt(mnt: *mut Mount) {
    record_cleanup();
    // SAFETY: The final mount reference serializes writer cleanup.
    unsafe {
        bindings::rust_helper_mnt_warn_writers(mnt);
        if !bindings::rust_helper_mnt_pins_empty(mnt) {
            bindings::rust_helper_mnt_kill_pins(mnt);
        }
        bindings::rust_helper_mnt_fsnotify_delete(mnt);
        bindings::rust_helper_mnt_dput_root(mnt);
        bindings::rust_helper_mnt_deactivate_super(mnt);
        bindings::rust_helper_mnt_free_id(mnt);
        bindings::rust_helper_mnt_call_rcu(mnt, Some(free_vfsmnt_rcu));
    }
}

/// Runs mount cleanup from task work.
///
/// # Safety
///
/// `head` must be the `mnt_rcu` union member of a doomed mount.
unsafe extern "C" fn cleanup_mnt_task_work(head: *mut CallbackHead) {
    // SAFETY: Task work currently uses the embedded union member.
    let mnt = unsafe { bindings::rust_helper_mnt_from_callback_head(head) };
    // SAFETY: Task work owns the final mount reference.
    unsafe { cleanup_mnt(mnt) };
}

/// Runs mount cleanup for the delayed workqueue.
///
/// # Safety
///
/// `mnt` must be on the delayed list and already detached.
#[export]
unsafe extern "C" fn rust_mnt_cleanup_delayed(mnt: *mut Mount) {
    // SAFETY: The delayed list transfers its ownership to this callback.
    unsafe { cleanup_mnt(mnt) };
}

/// Frees a mount after the final RCU grace period.
///
/// # Safety
///
/// `head` must be the `mnt_rcu` union member of an unreachable mount.
unsafe extern "C" fn free_vfsmnt_rcu(head: *mut CallbackHead) {
    // SAFETY: The RCU head is embedded in the unreachable mount.
    let mnt = unsafe { bindings::rust_helper_mnt_from_callback_head(head) };
    // SAFETY: The final RCU callback owns all remaining fields.
    unsafe { free_vfsmnt_internal(mnt) };
}

/// Releases a final mount reference after the detached mount is doomed.
///
/// # Safety
///
/// `mnt` must be detached and the caller must have already decremented the
/// final reference count while holding the mount hash lock.
unsafe fn cleanup_detached_mnt(mnt: *mut Mount) {
    // SAFETY: The final reference is detached and no namespace owns it.
    if unsafe { bindings::rust_helper_mnt_flags(mnt) }
        & (bindings::mount_flags_MNT_INTERNAL as c_int)
        != 0
    {
        // SAFETY: Internal mounts perform cleanup synchronously.
        unsafe { cleanup_mnt(mnt) };
        return;
    }

    // SAFETY: The current task context is stable for the scheduling decision.
    if unsafe { bindings::rust_helper_mnt_can_use_task_work() } || forced_task_work() {
        // SAFETY: The doomed mount is owned by this cleanup path.
        let queued =
            unsafe { bindings::rust_helper_mnt_queue_task_work(mnt, Some(cleanup_mnt_task_work)) };
        if queued {
            record_task_work();
            return;
        }
    }

    // SAFETY: The doomed mount is owned by this cleanup path.
    unsafe { bindings::rust_helper_mnt_queue_delayed(mnt) };
    record_delayed();
}

/// Preserves the original debug-only detached mount invariant.
///
/// # Safety
///
/// `mnt` must be live and the caller must hold its reference.
#[inline]
unsafe fn assert_detached_from_namespace(mnt: *mut Mount) {
    #[cfg(CONFIG_DEBUG_VFS)]
    if unsafe { bindings::rust_helper_mnt_has_namespace(mnt) } {
        // SAFETY: This is the original VFS_BUG_ON(mnt->mnt_ns) invariant.
        unsafe { bindings::BUG() }
    }

    #[cfg(not(CONFIG_DEBUG_VFS))]
    let _ = mnt;
}

/// Handles the slow path of dropping a detached mount reference.
///
/// # Safety
///
/// `mnt` must be a live mount whose caller holds a reference. RCU read
/// locking starts here and ends before the mount is scheduled for cleanup.
unsafe fn mntput_no_expire_slowpath(mnt: *mut Mount) {
    let mut shrink_list = MaybeUninit::<ListHead>::uninit();
    // SAFETY: The local list storage is writable and initialized before use.
    unsafe { bindings::rust_helper_mnt_init_list(shrink_list.as_mut_ptr()) };

    // SAFETY: The caller owns a reference and the original debug invariant
    // applies before the mount hash lock is acquired.
    unsafe { assert_detached_from_namespace(mnt) };
    // SAFETY: The mount remains live while its final reference is dropped.
    unsafe { bindings::rust_helper_mnt_lock_hash() };
    // SAFETY: The mount hash lock serializes the per-CPU count update.
    unsafe { bindings::rust_helper_mnt_dec_count(mnt) };
    // SAFETY: The mount hash lock serializes the per-CPU count sample.
    let count = unsafe { bindings::rust_helper_mnt_get_count(mnt) };
    if count != 0 {
        if count < 0 {
            // SAFETY: The helper emits the original VFS warning.
            unsafe { bindings::rust_helper_mnt_warn_negative_count(count) };
        }
        // SAFETY: This ends the RCU read-side section opened by the caller.
        unsafe { bindings::rust_helper_mnt_rcu_read_unlock() };
        // SAFETY: This releases the mount hash lock acquired above.
        unsafe { bindings::rust_helper_mnt_unlock_hash() };
        return;
    }

    // SAFETY: The final reference is protected by the mount hash lock.
    let flags = unsafe { bindings::rust_helper_mnt_flags(mnt) };
    if flags & (bindings::mount_flags_MNT_DOOMED as c_int) != 0 {
        // SAFETY: This ends the RCU read-side section opened by the caller.
        unsafe { bindings::rust_helper_mnt_rcu_read_unlock() };
        // SAFETY: This releases the mount hash lock acquired above.
        unsafe { bindings::rust_helper_mnt_unlock_hash() };
        return;
    }

    // SAFETY: The mount is doomed before new lookup paths can resume.
    unsafe {
        bindings::rust_helper_mnt_or_flags(mnt, bindings::mount_flags_MNT_DOOMED as c_int);
        bindings::rust_helper_mnt_rcu_read_unlock();
        bindings::rust_helper_mnt_del_instance(mnt);
    }

    // SAFETY: The mount is live and its expiry and cover lists are stable.
    if !unsafe { bindings::rust_helper_mnt_expire_empty(mnt) } {
        // SAFETY: The mount is live under the final-reference protocol.
        unsafe { bindings::rust_helper_mnt_remove_expire(mnt) };
    }
    // SAFETY: This preserves the original VFS invariant warning.
    unsafe { bindings::rust_helper_mnt_warn_mounted_children(mnt) };
    // SAFETY: The mount remains live and owns all covers.
    unsafe {
        bindings::rust_helper_mnt_drop_covers(mnt, shrink_list.as_mut_ptr());
        bindings::rust_helper_mnt_unlock_hash();
        bindings::rust_helper_mnt_shrink_dentries(shrink_list.as_mut_ptr());
    }

    // SAFETY: The detached mount is now owned by this cleanup path.
    unsafe { cleanup_detached_mnt(mnt) };
}

/// Drops a detached mount reference and runs the final cleanup when needed.
///
/// # Safety
///
/// `mnt` must be a live mount reference owned by the caller.
#[export]
unsafe extern "C" fn mntput_no_expire(mnt: *mut Mount) {
    // SAFETY: The caller owns a reference to the mount.
    unsafe { bindings::rust_helper_mnt_rcu_read_lock() };
    // SAFETY: The mount remains live under the caller's reference.
    if unsafe { bindings::rust_helper_mnt_has_namespace(mnt) } {
        // SAFETY: A namespace reference keeps this from being the final drop.
        unsafe { bindings::rust_helper_mnt_dec_put_fast(mnt) };
        // SAFETY: This ends the RCU read-side section opened above.
        unsafe { bindings::rust_helper_mnt_rcu_read_unlock() };
        return;
    }
    // SAFETY: The caller's reference remains live until the slow path.
    unsafe { mntput_no_expire_slowpath(mnt) };
}

/// Drops a vfsmount reference.
///
/// # Safety
///
/// `mnt` must be null or a live vfsmount reference owned by the caller.
#[export]
unsafe extern "C" fn mntput(mnt: *mut VfsMount) {
    if mnt.is_null() {
        return;
    }
    // SAFETY: The helper computes the containing mount address.
    let mnt = unsafe { bindings::rust_helper_mnt_from_vfsmount(mnt) };
    // SAFETY: The caller owns a live mount reference.
    unsafe {
        if bindings::rust_helper_mnt_expiry_mark(mnt) {
            bindings::rust_helper_mnt_clear_expiry_mark(mnt);
        }
        mntput_no_expire(mnt);
    }
}

/// Acquires a vfsmount reference.
///
/// # Safety
///
/// `mnt` must be null or a live vfsmount.
#[export]
unsafe extern "C" fn mntget(mnt: *mut VfsMount) -> *mut VfsMount {
    if !mnt.is_null() {
        // SAFETY: The vfsmount is live and embeds a mount.
        let mount = unsafe { bindings::rust_helper_mnt_from_vfsmount(mnt) };
        // SAFETY: The caller holds a reference that keeps the mount live.
        unsafe { bindings::rust_helper_mnt_inc_count(mount) };
    }
    mnt
}

/// Makes a mount inaccessible to new lookups.
///
/// # Safety
///
/// `mnt` must be null or a live vfsmount.
#[export]
unsafe extern "C" fn mnt_make_shortterm(mnt: *mut VfsMount) {
    if !mnt.is_null() {
        // SAFETY: The vfsmount is live and embeds a mount.
        let mount = unsafe { bindings::rust_helper_mnt_from_vfsmount(mnt) };
        // SAFETY: The caller serializes the shortterm transition.
        unsafe { bindings::rust_helper_mnt_make_shortterm(mount) };
    }
}

/// Allocates a raw mount for KUnit rollback coverage.
///
/// # Safety
///
/// `name` must be null or point to a live NUL-terminated string.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_mnt_test_alloc(name: *const c_uchar) -> *mut Mount {
    let stage = NEXT_FAILURE.load(Ordering::Acquire);
    if !(FAIL_ALLOC..=FAIL_PCP).contains(&stage) {
        return error_ptr(-(bindings::EINVAL as c_int));
    }

    // SAFETY: The caller supplies a null or live C string.
    let mnt = unsafe { alloc_vfsmnt(name) };
    if !mnt.is_null() {
        // SAFETY: The test helper owns this fully allocated unexpected mount.
        unsafe { abort_alloc(mnt, true, true, true, true) };
    }
    ptr::null_mut()
}

/// Clones a mount for KUnit failure coverage.
///
/// # Safety
///
/// `old` and `root` must remain live for the call.
#[cfg(CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_mnt_test_clone(
    old: *mut Mount,
    root: *mut Dentry,
    flags: c_int,
) -> *mut Mount {
    // SAFETY: The caller supplies live source objects.
    unsafe { clone_mnt(old, root, flags) }
}
