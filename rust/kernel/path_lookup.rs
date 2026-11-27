// SPDX-License-Identifier: GPL-2.0

//! Native path lookup orchestration.
//!
//! Rust owns the public lookup sequence, parent lookup sequence, RCU retry
//! policy, revalidation retry, scoped completion checks, directory checks,
//! audit dispatch, path transfer, and cleanup ordering. C retains the
//! nameidata layout, RCU and ref-walk steps, filesystem callbacks, permission
//! checks, and name-copy primitives.

use crate::bindings;
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};
use core::{
    ffi::{c_int, c_uchar, c_uint},
    mem::MaybeUninit,
    ptr,
};
use macros::export;

type Dentry = bindings::dentry;
type Filename = bindings::filename;
type Nameidata = bindings::nameidata;
type Path = bindings::path;
type Qstr = bindings::qstr;
type VfsMount = bindings::vfsmount;

#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
const TEST_FAIL_ECHILD: u32 = 1;
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
const TEST_FAIL_ESTALE: u32 = 2;

#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static LOOKUP_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static PARENT_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static LOOKUP_ATTEMPTS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static LOOKUP_FLAGS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static PARENT_ATTEMPTS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
static PARENT_FLAGS: AtomicU32 = AtomicU32::new(0);

fn is_error<T>(value: *const T) -> bool {
    // SAFETY: The helper only classifies the pointer value.
    unsafe { bindings::rust_helper_path_is_err(value.cast()) }
}

fn error_ptr<T>(error: c_int) -> *mut T {
    // SAFETY: The helper encodes a negative errno in pointer form.
    unsafe { bindings::rust_helper_path_err_ptr(error as isize).cast() }
}

fn read_error<T>(value: *const T) -> c_int {
    // SAFETY: The caller only uses this for an error pointer.
    unsafe { bindings::rust_helper_path_ptr_err(value.cast()) as c_int }
}

#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
fn record_attempt(counter: &AtomicU32, flags_counter: &AtomicU32, flags: c_uint) {
    counter.fetch_add(1, Ordering::Relaxed);
    flags_counter.store(flags, Ordering::Release);
}

#[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
fn record_attempt(_counter: &(), _flags_counter: &(), _flags: c_uint) {}

#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
fn injected_failure(failure: &AtomicU32) -> c_int {
    match failure.swap(0, Ordering::AcqRel) {
        TEST_FAIL_ECHILD => -(bindings::ECHILD as c_int),
        TEST_FAIL_ESTALE => -(bindings::ESTALE as c_int),
        _ => 0,
    }
}

/// Returns one path lookup test counter.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_lookup_attempts() -> c_uint {
    LOOKUP_ATTEMPTS.load(Ordering::Acquire)
}

/// Returns the flags from the most recent filename lookup attempt.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_lookup_flags() -> c_uint {
    LOOKUP_FLAGS.load(Ordering::Acquire)
}

/// Returns one parent lookup test counter.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_parent_attempts() -> c_uint {
    PARENT_ATTEMPTS.load(Ordering::Acquire)
}

/// Returns the flags from the most recent parent lookup attempt.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_parent_flags() -> c_uint {
    PARENT_FLAGS.load(Ordering::Acquire)
}

/// Arms one filename lookup retry result.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_fail_lookup(error: c_uint) {
    LOOKUP_FAILURE.store(error, Ordering::Release);
}

/// Arms one parent lookup retry result.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_fail_parent(error: c_uint) {
    PARENT_FAILURE.store(error, Ordering::Release);
}

/// Clears path lookup test counters and fault injection state.
#[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
#[export]
extern "C" fn rust_path_test_reset() {
    LOOKUP_FAILURE.store(0, Ordering::Relaxed);
    PARENT_FAILURE.store(0, Ordering::Relaxed);
    LOOKUP_ATTEMPTS.store(0, Ordering::Relaxed);
    LOOKUP_FLAGS.store(0, Ordering::Relaxed);
    PARENT_ATTEMPTS.store(0, Ordering::Relaxed);
    PARENT_FLAGS.store(0, Ordering::Relaxed);
}

/// Runs one complete path lookup attempt.
///
/// # Safety
///
/// `nd`, `path`, and every object referenced by them must satisfy the path
/// lookup contracts. The caller owns the nameidata lifecycle.
unsafe fn path_lookupat_inner(nd: *mut Nameidata, flags: c_uint, path: *mut Path) -> c_int {
    // SAFETY: The caller supplies initialized lookup state and output storage.
    let mut name = unsafe { bindings::rust_helper_path_init(nd, flags) };

    if flags & (bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_DOWN as c_uint) != 0
        && !is_error(name)
    {
        // SAFETY: The RCU or ref-walk state is active at this point.
        let down_error = unsafe { bindings::rust_helper_path_handle_lookup_down(nd) };
        if down_error < 0 {
            name = error_ptr(down_error);
        }
    }

    let mut error;
    loop {
        // SAFETY: The lookup state is active and `name` is valid.
        error = unsafe { bindings::rust_helper_path_link_path_walk(name, nd) };
        if error != 0 {
            break;
        }
        // SAFETY: The path walk reached a stable component boundary.
        name = unsafe { bindings::rust_helper_path_lookup_last(nd) };
        if name.is_null() {
            break;
        }
    }

    // SAFETY: The lookup state remains live until terminate_walk.
    if error == 0 && unsafe { bindings::rust_helper_path_has_mountpoint(nd) } {
        // SAFETY: Mountpoint traversal uses the same active lookup state.
        error = unsafe { bindings::rust_helper_path_handle_lookup_down(nd) };
        // SAFETY: The mountpoint traversal replaces the final weak check.
        unsafe { bindings::rust_helper_path_clear_jumped(nd) };
    }
    if error == 0 {
        // SAFETY: The lookup state is active.
        error = unsafe { bindings::rust_helper_path_complete_walk(nd) };
    }
    // SAFETY: The lookup state is active.
    let requires_directory = unsafe { bindings::rust_helper_path_requires_directory(nd) };
    // SAFETY: The lookup state is active.
    let can_lookup = unsafe { bindings::rust_helper_path_can_lookup(nd) };
    if error == 0 && requires_directory && !can_lookup {
        error = -(bindings::ENOTDIR as c_int);
    }
    if error == 0 {
        // SAFETY: The output path is writable and the walk state is valid.
        unsafe { bindings::rust_helper_path_take_path(nd, path) };
    }
    // SAFETY: Every lookup attempt owns one nameidata cleanup.
    unsafe { bindings::rust_helper_path_terminate_walk(nd) };
    error
}

/// Resolves a pathname with the same RCU and revalidation retry order as C.
///
/// # Safety
///
/// `name`, `path`, and optional `root` must satisfy the namei contracts.
#[export]
unsafe extern "C" fn filename_lookup(
    dfd: c_int,
    name: *mut Filename,
    flags: c_uint,
    path: *mut Path,
    root: *const Path,
) -> c_int {
    if is_error(name) {
        return read_error(name);
    }

    let mut nd = MaybeUninit::<Nameidata>::uninit();
    // SAFETY: The local storage is writable and this path owns cleanup.
    unsafe { bindings::rust_helper_path_set_nameidata(nd.as_mut_ptr(), dfd, name, root) };

    let first_flags = flags | bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_RCU as c_uint;
    #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
    record_attempt(&LOOKUP_ATTEMPTS, &LOOKUP_FLAGS, first_flags);
    #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
    record_attempt(&(), &(), first_flags);
    #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
    let mut retval = match injected_failure(&LOOKUP_FAILURE) {
        0 => {
            // SAFETY: The local nameidata is initialized and this path owns it.
            unsafe { path_lookupat_inner(nd.as_mut_ptr(), first_flags, path) }
        }
        error => error,
    };
    #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
    let mut retval = unsafe { path_lookupat_inner(nd.as_mut_ptr(), first_flags, path) };

    if retval == -(bindings::ECHILD as c_int) {
        #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
        record_attempt(&LOOKUP_ATTEMPTS, &LOOKUP_FLAGS, flags);
        #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
        record_attempt(&(), &(), flags);
        // SAFETY: The previous attempt terminated its walk state.
        retval = unsafe { path_lookupat_inner(nd.as_mut_ptr(), flags, path) };
    }
    if retval == -(bindings::ESTALE as c_int) {
        let reval_flags = flags | bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_REVAL as c_uint;
        #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
        record_attempt(&LOOKUP_ATTEMPTS, &LOOKUP_FLAGS, reval_flags);
        #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
        record_attempt(&(), &(), reval_flags);
        // SAFETY: The previous attempt terminated its walk state.
        retval = unsafe { path_lookupat_inner(nd.as_mut_ptr(), reval_flags, path) };
    }

    if retval == 0 {
        let audit_flags = if flags
            & (bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_MOUNTPOINT as c_uint)
            != 0
        {
            bindings::rust_path_audit_flags_RUST_PATH_AUDIT_NOEVAL as c_uint
        } else {
            0
        };
        // SAFETY: A successful lookup transferred a referenced path.
        unsafe { bindings::rust_helper_path_audit_inode(name, (*path).dentry, audit_flags) };
    }
    // SAFETY: This restores the nameidata linked by set_nameidata.
    unsafe { bindings::rust_helper_path_restore_nameidata() };
    retval
}

/// Runs one parent lookup attempt.
///
/// # Safety
///
/// `nd`, `parent`, and every object referenced by them must satisfy the
/// parent lookup contracts.
unsafe fn path_parentat_inner(nd: *mut Nameidata, flags: c_uint, parent: *mut Path) -> c_int {
    // SAFETY: The caller supplies initialized lookup state and output storage.
    let name = unsafe { bindings::rust_helper_path_init(nd, flags) };
    // SAFETY: The lookup state is active and `name` is valid.
    let mut error = unsafe { bindings::rust_helper_path_link_path_walk(name, nd) };
    if error == 0 {
        // SAFETY: The lookup state reached the parent boundary.
        error = unsafe { bindings::rust_helper_path_complete_walk(nd) };
    }
    if error == 0 {
        // SAFETY: The output path is writable and the walk state is valid.
        unsafe { bindings::rust_helper_path_take_path(nd, parent) };
    }
    // SAFETY: Every parent lookup attempt owns one nameidata cleanup.
    unsafe { bindings::rust_helper_path_terminate_walk(nd) };
    error
}

/// Resolves a parent path with RCU and revalidation retries.
///
/// # Safety
///
/// `name`, `parent`, `last`, and optional `root` must satisfy the namei
/// contracts.
unsafe fn filename_parentat_inner(
    dfd: c_int,
    name: *mut Filename,
    flags: c_uint,
    parent: *mut Path,
    last: *mut Qstr,
    last_type: *mut c_uint,
    root: *const Path,
) -> c_int {
    if is_error(name) {
        return read_error(name);
    }

    let mut nd = MaybeUninit::<Nameidata>::uninit();
    // SAFETY: The local storage is writable and this path owns cleanup.
    unsafe { bindings::rust_helper_path_set_nameidata(nd.as_mut_ptr(), dfd, name, root) };

    let first_flags = flags | bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_RCU as c_uint;
    #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
    record_attempt(&PARENT_ATTEMPTS, &PARENT_FLAGS, first_flags);
    #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
    record_attempt(&(), &(), first_flags);
    #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
    let mut retval = match injected_failure(&PARENT_FAILURE) {
        0 => {
            // SAFETY: The local nameidata is initialized and this path owns it.
            unsafe { path_parentat_inner(nd.as_mut_ptr(), first_flags, parent) }
        }
        error => error,
    };
    #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
    let mut retval = unsafe { path_parentat_inner(nd.as_mut_ptr(), first_flags, parent) };

    if retval == -(bindings::ECHILD as c_int) {
        #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
        record_attempt(&PARENT_ATTEMPTS, &PARENT_FLAGS, flags);
        #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
        record_attempt(&(), &(), flags);
        // SAFETY: The previous attempt terminated its walk state.
        retval = unsafe { path_parentat_inner(nd.as_mut_ptr(), flags, parent) };
    }
    if retval == -(bindings::ESTALE as c_int) {
        let reval_flags = flags | bindings::rust_path_lookup_flags_RUST_PATH_LOOKUP_REVAL as c_uint;
        #[cfg(CONFIG_PATH_LOOKUP_KUNIT_TEST)]
        record_attempt(&PARENT_ATTEMPTS, &PARENT_FLAGS, reval_flags);
        #[cfg(not(CONFIG_PATH_LOOKUP_KUNIT_TEST))]
        record_attempt(&(), &(), reval_flags);
        // SAFETY: The previous attempt terminated its walk state.
        retval = unsafe { path_parentat_inner(nd.as_mut_ptr(), reval_flags, parent) };
    }

    if retval == 0 {
        // SAFETY: A successful parent walk transferred one referenced path.
        unsafe {
            bindings::rust_helper_path_get_last(nd.as_mut_ptr(), last);
            *last_type = bindings::rust_helper_path_last_type(nd.as_mut_ptr()) as c_uint;
            bindings::rust_helper_path_audit_inode(
                name,
                (*parent).dentry,
                bindings::rust_path_audit_flags_RUST_PATH_AUDIT_PARENT as c_uint,
            );
        }
    }
    // SAFETY: This restores the nameidata linked by set_nameidata.
    unsafe { bindings::rust_helper_path_restore_nameidata() };
    retval
}

/// Defines the production `path_lookupat()` entry.
///
/// # Safety
///
/// The caller must provide live nameidata and output path objects.
#[export]
unsafe extern "C" fn path_lookupat(nd: *mut Nameidata, flags: c_uint, path: *mut Path) -> c_int {
    // SAFETY: The caller supplies the namei contracts.
    unsafe { path_lookupat_inner(nd, flags, path) }
}

/// Defines the production `filename_parentat()` entry.
///
/// # Safety
///
/// The caller must provide live lookup and output objects.
#[export]
unsafe extern "C" fn filename_parentat(
    dfd: c_int,
    name: *mut Filename,
    flags: c_uint,
    parent: *mut Path,
    last: *mut Qstr,
    last_type: *mut c_uint,
) -> c_int {
    // SAFETY: The caller supplies the namei contracts.
    unsafe { filename_parentat_inner(dfd, name, flags, parent, last, last_type, ptr::null()) }
}

/// Looks up a path relative to a dentry and vfsmount pair.
///
/// # Safety
///
/// `filename`, `parent`, `last`, and optional `root` must remain live.
#[export]
unsafe extern "C" fn vfs_path_parent_lookup(
    filename: *mut Filename,
    flags: c_uint,
    parent: *mut Path,
    last: *mut Qstr,
    root: *const Path,
) -> c_int {
    let mut last_type = 0;
    // SAFETY: The caller supplies live lookup and output objects.
    let error = unsafe {
        filename_parentat_inner(
            bindings::AT_FDCWD as c_int,
            filename,
            flags,
            parent,
            last,
            &mut last_type,
            root,
        )
    };
    if error != 0 {
        return error;
    }
    if last_type != bindings::last_type_LAST_NORM {
        // SAFETY: The successful parent lookup returned a referenced path.
        unsafe { bindings::rust_helper_path_put(parent) };
        return -(bindings::EINVAL as c_int);
    }
    0
}

/// Looks up a path relative to a dentry and vfsmount pair.
///
/// # Safety
///
/// `name` must be a live NUL-terminated string, and `path` must be writable.
#[export]
unsafe extern "C" fn vfs_path_lookup(
    dentry: *mut Dentry,
    mnt: *mut VfsMount,
    name: *const c_uchar,
    flags: c_uint,
    path: *mut Path,
) -> c_int {
    // SAFETY: The caller supplies a live kernel string.
    let filename = unsafe { bindings::rust_helper_path_getname_kernel(name) };
    if is_error(filename) {
        return read_error(filename);
    }

    let mut root = MaybeUninit::<Path>::uninit();
    // SAFETY: The root storage is writable and the caller owns both objects.
    unsafe { bindings::rust_helper_path_init_root(root.as_mut_ptr(), dentry, mnt) };
    // SAFETY: The filename and root remain live through the lookup.
    let error = unsafe {
        filename_lookup(
            bindings::AT_FDCWD as c_int,
            filename,
            flags,
            path,
            root.as_ptr(),
        )
    };
    // SAFETY: The lookup no longer uses the acquired filename.
    unsafe { bindings::rust_helper_path_putname(filename) };
    error
}

/// Looks up an absolute kernel pathname.
///
/// # Safety
///
/// `name` must be a live NUL-terminated string, and `path` must be writable.
#[export]
unsafe extern "C" fn kern_path(name: *const c_uchar, flags: c_uint, path: *mut Path) -> c_int {
    // SAFETY: The caller supplies a live kernel string.
    let filename = unsafe { bindings::rust_helper_path_getname_kernel(name) };
    if is_error(filename) {
        return read_error(filename);
    }
    // SAFETY: The filename remains live through the lookup.
    let error = unsafe {
        filename_lookup(
            bindings::AT_FDCWD as c_int,
            filename,
            flags,
            path,
            ptr::null(),
        )
    };
    // SAFETY: The lookup no longer uses the acquired filename.
    unsafe { bindings::rust_helper_path_putname(filename) };
    error
}
