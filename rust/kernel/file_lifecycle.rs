// SPDX-License-Identifier: GPL-2.0

//! Native file lifecycle.
//!
//! Rust owns file limit policy, allocation and initialization rollback,
//! pseudo-file composition, cloning, final release ordering, and the decision
//! to run a final close synchronously, through task work, or through delayed
//! work. C retains the file layout, slab allocations, security hooks, VFS
//! callback dispatch, task work, delayed work, and path and mount primitives.

use crate::bindings;
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::AtomicU32;
use core::{
    ffi::{c_int, c_uchar},
    mem::MaybeUninit,
    ptr,
    sync::atomic::{AtomicIsize, Ordering},
};
use macros::export;

type Cred = bindings::cred;
type File = bindings::file;
type FileOperations = bindings::file_operations;
type Inode = bindings::inode;
type Path = bindings::path;
type VfsMount = bindings::vfsmount;

const FAIL_LIMIT: u32 = 1;
const FAIL_ALLOC: u32 = 2;
const FAIL_INIT: u32 = 3;
const FAIL_PSEUDO_PATH: u32 = 4;

static OLD_MAX: AtomicIsize = AtomicIsize::new(0);

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static ALLOC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static INIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static PATH_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static FREE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static RELEASE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static TASK_WORK_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static DELAYED_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_alloc() {
    ALLOC_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_alloc() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_init() {
    INIT_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_init() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_path() {
    PATH_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_path() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_free() {
    FREE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_free() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_release() {
    RELEASE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_release() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_task_work() {
    TASK_WORK_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_task_work() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_delayed() {
    DELAYED_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_delayed() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
fn record_failure() {}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_FILE_LIFECYCLE_KUNIT_TEST))]
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

fn is_error<T>(value: *const T) -> bool {
    // SAFETY: The helper only classifies the pointer value.
    unsafe { bindings::rust_helper_file_is_err(value.cast()) }
}

fn error_ptr<T>(error: c_int) -> *mut T {
    // SAFETY: The helper encodes a small negative errno in pointer form.
    unsafe { bindings::rust_helper_file_err_ptr(error as isize).cast() }
}

/// Releases all dependencies and storage owned by a fully released file.
///
/// # Safety
///
/// `file` must contain the final reference and no later access is allowed.
unsafe fn file_free(file: *mut File) {
    record_free();

    // SAFETY: The caller owns the final reference and a fully initialized file.
    unsafe {
        bindings::rust_helper_file_security_free(file);
        if !bindings::rust_helper_file_noaccount(file) {
            bindings::rust_helper_file_account_put();
        }
        bindings::rust_helper_file_put_cred(file);
        if bindings::rust_helper_file_backing(file) {
            bindings::rust_helper_file_backing_free(file);
        } else {
            bindings::rust_helper_file_free_object(file);
        }
    }
}

fn over_limit() -> *mut File {
    // SAFETY: The helper returns a monotonic read of the file population.
    let nr = unsafe { bindings::rust_helper_file_nr_files() };
    let old_max = OLD_MAX.load(Ordering::Relaxed);
    if nr > old_max {
        // SAFETY: The helper has the original file limit logging contract.
        unsafe { bindings::rust_helper_file_limit_warn() };
        OLD_MAX.store(nr, Ordering::Relaxed);
    }
    error_ptr(-(bindings::ENFILE as c_int))
}

/// Allocates and initializes a counted file object.
///
/// # Safety
///
/// `cred` must contain a live credential reference.
unsafe fn alloc_empty_file_internal(flags: c_int, cred: *const Cred) -> *mut File {
    record_alloc();

    if !injected_failure(FAIL_LIMIT) {
        // SAFETY: The helpers read only the global file accounting state.
        let at_limit = unsafe {
            bindings::rust_helper_file_nr_files() >= bindings::rust_helper_file_max_files() as isize
        };
        if at_limit {
            // SAFETY: Capability checks have the original current-task context.
            let admin = unsafe { bindings::rust_helper_file_admin_capable() };
            if !admin {
                // SAFETY: The helper performs the original precise recount.
                let counted = unsafe {
                    bindings::rust_helper_file_nr_files_sum()
                        >= bindings::rust_helper_file_max_files() as isize
                };
                if counted {
                    return over_limit();
                }
            }
        }
    } else {
        return over_limit();
    }

    let file = if injected_failure(FAIL_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The file slab cache is initialized before file allocation.
        unsafe { bindings::rust_helper_file_alloc_object() }
    };
    if file.is_null() {
        return error_ptr(-(bindings::ENOMEM as c_int));
    }

    record_init();
    let error = if injected_failure(FAIL_INIT) {
        -(bindings::ENOMEM as c_int)
    } else {
        // SAFETY: The slab object is uninitialized and the credential is live.
        unsafe { bindings::rust_helper_file_init_object(file, flags, cred) }
    };
    if error != 0 {
        // SAFETY: Initialization failed before any counted reference was added.
        unsafe { bindings::rust_helper_file_free_object(file) };
        return error_ptr(error);
    }

    // SAFETY: The file is now a fully initialized counted file object.
    unsafe { bindings::rust_helper_file_account_get() };
    file
}

/// Allocates an empty counted file object.
///
/// # Safety
///
/// `cred` must contain a live credential reference.
#[export]
unsafe extern "C" fn alloc_empty_file(flags: c_int, cred: *const Cred) -> *mut File {
    // SAFETY: The caller provides a live credential.
    unsafe { alloc_empty_file_internal(flags, cred) }
}

/// Allocates a file object for an existing path.
///
/// # Safety
///
/// `path` and `fops` must remain live through initialization.
unsafe fn alloc_file_internal(
    path: *const Path,
    flags: c_int,
    fops: *const FileOperations,
) -> *mut File {
    // SAFETY: The current task has a live subjective credential.
    let cred = unsafe { bindings::rust_helper_file_current_cred() };
    // SAFETY: The credential remains live through allocation and initialization.
    let file = unsafe { alloc_empty_file_internal(flags, cred) };
    if !is_error(file) {
        // SAFETY: The new file is initialized and the path and fops are live.
        unsafe { bindings::rust_helper_file_init_path(file, path, fops) };
    }
    file
}

/// Allocates a counted pseudo file.
///
/// # Safety
///
/// `inode`, `mnt`, and `fops` must remain live for the operation.
#[export]
unsafe extern "C" fn alloc_file_pseudo(
    inode: *mut Inode,
    mnt: *mut VfsMount,
    name: *const c_uchar,
    flags: c_int,
    fops: *const FileOperations,
) -> *mut File {
    let mut path = MaybeUninit::<Path>::uninit();
    let error = if injected_failure(FAIL_PSEUDO_PATH) {
        -(bindings::ENOMEM as c_int)
    } else {
        // SAFETY: The caller provides live inode, mount, and name objects.
        unsafe { bindings::rust_helper_file_alloc_pseudo_path(name, inode, mnt, path.as_mut_ptr()) }
    };
    if error != 0 {
        return error_ptr(error);
    }
    record_path();
    // SAFETY: `alloc_path_pseudo()` completed successfully, so `path` is initialized.
    let mut path = unsafe { path.assume_init() };

    // SAFETY: The initialized path and file operations remain live.
    let file = unsafe { alloc_file_internal(ptr::addr_of!(path), flags, fops) };
    if is_error(file) {
        // SAFETY: The failed file did not consume the pseudo path.
        unsafe {
            bindings::rust_helper_file_abort_pseudo_path(ptr::addr_of_mut!(path).cast(), inode)
        };
        return file;
    }

    // SAFETY: The new opened file is live and owned by this path.
    unsafe { bindings::rust_helper_file_set_fsnotify_nonotify(file) };
    file
}

/// Clones a file from an existing path.
///
/// # Safety
///
/// `base` and `fops` must remain live for the operation.
#[export]
unsafe extern "C" fn alloc_file_clone(
    base: *mut File,
    flags: c_int,
    fops: *const FileOperations,
) -> *mut File {
    // SAFETY: The caller provides a live opened base file.
    let path = unsafe { ptr::addr_of!((*base).__bindgen_anon_1.f_path) };
    // SAFETY: The base path and file operations remain live.
    let file = unsafe { alloc_file_internal(path, flags, fops) };
    if !is_error(file) {
        // SAFETY: The clone owns an initialized path copied from the base file.
        unsafe {
            bindings::rust_helper_file_get_path(file);
            bindings::rust_helper_file_set_mapping(file, base);
        }
    }
    file
}

/// Releases the final file reference and all associated resources.
///
/// # Safety
///
/// `file` must own the final reference and must not be accessed after return.
#[export]
unsafe extern "C" fn __fput(file: *mut File) {
    // SAFETY: The caller owns the final reference.
    let opened = unsafe { bindings::rust_helper_file_opened(file) };
    if !opened {
        // SAFETY: The unopened file needs only object and dependency release.
        unsafe { file_free(file) };
        return;
    }
    record_release();

    // SAFETY: The final file reference makes the saved fields immutable.
    let (dentry, mnt, inode, mode) = unsafe {
        (
            bindings::rust_helper_file_dentry(file),
            bindings::rust_helper_file_mount(file),
            bindings::rust_helper_file_inode(file),
            bindings::rust_helper_file_mode(file),
        )
    };

    // SAFETY: This is the original sleepable final-release context.
    unsafe { bindings::rust_helper_file_might_sleep() };
    // SAFETY: The final file reference keeps all callback owners live.
    unsafe {
        bindings::rust_helper_file_fsnotify_close(file);
        bindings::rust_helper_file_eventpoll_release(file);
        bindings::rust_helper_file_locks_remove(file);
        bindings::rust_helper_file_security_release(file);
    }
    // SAFETY: The final file reference keeps the operation table live.
    if unsafe { bindings::rust_helper_file_needs_fasync(file) } {
        // SAFETY: The helper checks that fasync exists before this call.
        unsafe { bindings::rust_helper_file_call_fasync(file) };
    }
    // SAFETY: The final file reference keeps the operation table live.
    if unsafe { bindings::rust_helper_file_has_release(file) } {
        // SAFETY: The helper checks that release exists before this call.
        unsafe { bindings::rust_helper_file_call_release(file, inode) };
    }
    // SAFETY: Character-device membership is immutable after the final put.
    if unsafe { bindings::rust_helper_file_needs_cdev_put(file, inode, mode) } {
        // SAFETY: The helper confirmed that the inode owns a cdev reference.
        unsafe { bindings::rust_helper_file_cdev_put(inode) };
    }
    // SAFETY: The release chain still owns the operation table and file access.
    unsafe {
        bindings::rust_helper_file_fops_put(file);
        bindings::rust_helper_file_owner_release(file);
        bindings::rust_helper_file_put_access(file);
        bindings::rust_helper_file_dput(dentry);
    }
    // SAFETY: The saved mode selects the original mount release transition.
    if unsafe { bindings::rust_helper_file_needs_unmount(mode) } {
        // SAFETY: The file owns the mount reference selected for dissolution.
        unsafe { bindings::rust_helper_file_dissolve_mount(mnt) };
    } else {
        // SAFETY: The file owns one mount reference.
        unsafe { bindings::rust_helper_file_mntput(mnt) };
    }

    // SAFETY: All file, path, mount, operation, and security references are
    // released before the slab object.
    unsafe { file_free(file) };
}

/// Schedules final opened-file cleanup after the last reference is released.
///
/// # Safety
///
/// `file` must contain the final released reference and no later access is allowed.
unsafe fn fput_deferred(file: *mut File) {
    // SAFETY: The final reference was just released.
    if unsafe { bindings::rust_helper_file_early_release(file) } {
        // SAFETY: No opened-file cleanup is required.
        unsafe { file_free(file) };
        return;
    }

    // SAFETY: The helper has the original current-task classification.
    if unsafe { bindings::rust_helper_file_can_use_task_work() } {
        // SAFETY: The file is waiting for its final opened-file cleanup.
        if unsafe { bindings::rust_helper_file_queue_task_work(file) } {
            record_task_work();
            return;
        }
    }

    // SAFETY: Delayed work is the fallback when task work cannot be queued.
    unsafe { bindings::rust_helper_file_queue_delayed(file) };
    record_delayed();
}

/// Releases one file reference.
///
/// # Safety
///
/// `file` must contain a live reference owned by the caller.
#[export]
unsafe extern "C" fn fput(file: *mut File) {
    // SAFETY: The caller transfers one live reference.
    if unsafe { bindings::rust_helper_file_ref_put(file) } {
        // SAFETY: This call owns the final released reference.
        unsafe { fput_deferred(file) };
    }
}

/// Releases one file reference and performs final release synchronously.
///
/// # Safety
///
/// `file` must contain a live reference owned by the caller.
#[export]
unsafe extern "C" fn __fput_sync(file: *mut File) {
    // SAFETY: The caller transfers one live reference.
    if unsafe { bindings::rust_helper_file_ref_put(file) } {
        // SAFETY: This call owns the final released reference.
        unsafe { __fput(file) };
    }
}

/// Releases one file reference using the close fast path and synchronous
/// final release.
///
/// # Safety
///
/// `file` must contain a live reference owned by the caller.
#[export]
unsafe extern "C" fn fput_close_sync(file: *mut File) {
    // SAFETY: The caller transfers one live reference.
    if unsafe { bindings::rust_helper_file_ref_put_close(file) } {
        // SAFETY: This call owns the final released reference.
        unsafe { __fput(file) };
    }
}

/// Releases one file reference using the close fast path.
///
/// # Safety
///
/// `file` must contain a live reference owned by the caller.
#[export]
unsafe extern "C" fn fput_close(file: *mut File) {
    // SAFETY: The caller transfers one live reference.
    if unsafe { bindings::rust_helper_file_ref_put_close(file) } {
        // SAFETY: This call owns the final released reference.
        unsafe { fput_deferred(file) };
    }
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_alloc_count() -> u32 {
    ALLOC_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_init_count() -> u32 {
    INIT_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_path_count() -> u32 {
    PATH_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_free_count() -> u32 {
    FREE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_release_count() -> u32 {
    RELEASE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_task_work_count() -> u32 {
    TASK_WORK_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_delayed_count() -> u32 {
    DELAYED_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_file_test_fail_next(stage: u32) {
    NEXT_FAILURE.store(stage, Ordering::Release);
}

#[cfg(CONFIG_FILE_LIFECYCLE_KUNIT_TEST)]
/// Queues a file through the production deferred-release path for testing.
///
/// # Safety
///
/// `file` must contain a live reference transferred to the deferred cleanup path.
#[export]
unsafe extern "C" fn rust_file_test_queue_task_work(file: *mut File) -> bool {
    // SAFETY: The test transfers a live final file reference.
    if unsafe { bindings::rust_helper_file_queue_task_work(file) } {
        record_task_work();
        true
    } else {
        false
    }
}
