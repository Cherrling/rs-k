// SPDX-License-Identifier: GPL-2.0

//! Native file open orchestration.
//!
//! Rust owns ordinary, O_PATH, and O_TMPFILE dispatch, the open RCU and
//! revalidation retry sequence, final-component lookup and creation, atomic
//! open coordination, file-open initialization and rollback, permission
//! check ordering, truncation ordering, and the completion handoff.
//!
//! C retains the file, dentry, inode, path, and nameidata layouts. It also
//! retains dentry and inode locking, RCU and ref-walk traversal, filesystem
//! callbacks, permission and security hooks, fsnotify delivery, mount writes,
//! and low-level allocation primitives.

use crate::bindings;
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};
use core::{
    ffi::{c_int, c_uchar, c_uint},
    mem::MaybeUninit,
    ptr,
};
use macros::export;

type Dentry = bindings::dentry;
type Cred = bindings::cred;
type File = bindings::file;
type Filename = bindings::filename;
type Inode = bindings::inode;
type Nameidata = bindings::nameidata;
type OpenCallback = Option<unsafe extern "C" fn(*mut Inode, *mut File) -> c_int>;
type OpenFlags = bindings::open_flags;
type Path = bindings::path;
type Qstr = bindings::qstr;
type Umode = bindings::umode_t;

const MODE_READ: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_READ as bindings::fmode_t;
const MODE_WRITE: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_WRITE as bindings::fmode_t;
const MODE_LSEEK: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_LSEEK as bindings::fmode_t;
const MODE_PREAD: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_PREAD as bindings::fmode_t;
const MODE_PWRITE: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_PWRITE as bindings::fmode_t;
const MODE_PATH: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_PATH as bindings::fmode_t;
const MODE_ATOMIC_POS: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_ATOMIC_POS as bindings::fmode_t;
const MODE_WRITER: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_WRITER as bindings::fmode_t;
const MODE_CAN_READ: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_CAN_READ as bindings::fmode_t;
const MODE_CAN_WRITE: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_CAN_WRITE as bindings::fmode_t;
const MODE_OPENED: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_OPENED as bindings::fmode_t;
const MODE_CREATED: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_CREATED as bindings::fmode_t;
const MODE_CAN_ODIRECT: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_CAN_ODIRECT as bindings::fmode_t;
const MODE_NONOTIFY: bindings::fmode_t =
    bindings::rust_open_entry_mode_RUST_OPEN_MODE_NONOTIFY as bindings::fmode_t;

const FLAG_CREAT: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_CREAT as c_int;
const FLAG_EXCL: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_EXCL as c_int;
const FLAG_TRUNC: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_TRUNC as c_int;
const FLAG_PATH: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_PATH as c_int;
const FLAG_DIRECTORY: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_DIRECTORY as c_int;
const FLAG_WRONLY: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_WRONLY as c_int;
const FLAG_RDWR: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_RDWR as c_int;
const FLAG_DIRECT: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_DIRECT as c_int;
const FLAG_NOCTTY: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_NOCTTY as c_int;
const FLAG_TMPFILE: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_TMPFILE as c_int;
const FLAG_REGULAR: c_int = bindings::rust_open_entry_flags_RUST_OPEN_FLAG_REGULAR as c_int;

const LOOKUP_FOLLOW: c_uint = bindings::rust_open_entry_lookup_RUST_OPEN_LOOKUP_FOLLOW as c_uint;
const LOOKUP_DIRECTORY: c_uint =
    bindings::rust_open_entry_lookup_RUST_OPEN_LOOKUP_DIRECTORY as c_uint;
const LOOKUP_RCU: c_uint = bindings::rust_open_entry_lookup_RUST_OPEN_LOOKUP_RCU as c_uint;
const LOOKUP_REVAL: c_uint = bindings::rust_open_entry_lookup_RUST_OPEN_LOOKUP_REVAL as c_uint;

const TYPE_MASK: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_MASK as Umode;
const TYPE_DIR: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_DIR as Umode;
const TYPE_REGULAR: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_REGULAR as Umode;
const TYPE_LINK: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_LINK as Umode;
const TYPE_BLOCK: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_BLOCK as Umode;
const TYPE_CHAR: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_CHAR as Umode;
const TYPE_FIFO: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_FIFO as Umode;
const TYPE_SOCKET: Umode = bindings::rust_open_entry_type_RUST_OPEN_TYPE_SOCKET as Umode;

#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
const TEST_FAIL_ECHILD: u32 = bindings::RUST_OPEN_TEST_FAIL_ECHILD;
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
const TEST_FAIL_EOPENSTALE: u32 = bindings::RUST_OPEN_TEST_FAIL_EOPENSTALE;

#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
static RETRY_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
static ATTEMPTS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
static ATTEMPT_FLAGS: AtomicU32 = AtomicU32::new(0);

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

fn negative(error: u32) -> c_int {
    -(error as c_int)
}

/// Returns the mutable path field of a file.
///
/// # Safety
///
/// `file` must be a live initialized file.
unsafe fn file_path(file: *mut File) -> *mut Path {
    // SAFETY: Every initialized file contains the path union.
    unsafe { ptr::addr_of_mut!((*file).__bindgen_anon_1.__f_path) }
}

/// Returns the mutable read-ahead field of an opening file.
///
/// # Safety
///
/// `file` must be a live file with the read-ahead union active.
unsafe fn file_ra(file: *mut File) -> *mut bindings::file_ra_state {
    // SAFETY: The file is fully allocated and the requested union variant is
    // active while the file is being opened.
    unsafe { ptr::addr_of_mut!((*file).__bindgen_anon_3.f_ra) }
}

/// Returns the current task credential.
///
/// # Safety
///
/// The caller must use the returned borrowed credential while the current
/// task keeps its subjective credential live.
unsafe fn current_cred() -> *const bindings::cred {
    // SAFETY: The current task has a live subjective credential.
    unsafe { bindings::rust_helper_file_current_cred() }
}

/// Releases an opening file path and clears the file references to it.
///
/// # Safety
///
/// `file` must contain a path and access state that this attempt owns.
unsafe fn cleanup_file_path(file: *mut File) {
    // SAFETY: The path reference was acquired by this attempt.
    unsafe {
        bindings::path_put(file_path(file));
        (*file_path(file)).mnt = ptr::null_mut();
        (*file_path(file)).dentry = ptr::null_mut();
        (*file).f_inode = ptr::null_mut();
    }
}

/// Initializes the open state of a newly allocated file.
///
/// # Safety
///
/// `file` must be a live, empty file object with a referenced final path.
unsafe fn do_dentry_open(file: *mut File, callback: OpenCallback) -> c_int {
    // SAFETY: The caller supplies an initialized empty file and a live path.
    let inode = unsafe { (*(*file_path(file)).dentry).d_inode };

    // SAFETY: The file path contains a newly transferred path reference.
    unsafe {
        bindings::path_get(file_path(file));
        (*file).f_inode = inode;
        (*file).f_mapping = (*inode).i_mapping;
        (*file).f_wb_err = bindings::rust_helper_open_sample_wb_err((*file).f_mapping);
        (*file).f_sb_err = bindings::rust_helper_open_sample_sb_err(file);
    }

    // SAFETY: The file is exclusively owned by this open attempt.
    if unsafe { (*file).f_flags } & FLAG_PATH as c_uint != 0 {
        // SAFETY: The O_PATH completion has no filesystem callbacks.
        unsafe {
            (*file).f_mode = MODE_PATH | MODE_OPENED;
            bindings::rust_helper_open_set_fsnotify_mode(file, MODE_NONOTIFY);
            (*file).f_op = bindings::rust_helper_open_empty_fops();
        }
        return 0;
    }

    // SAFETY: The inode is live and the mode was initialized by the path walk.
    let access = unsafe { (*file).f_mode } & (MODE_READ | MODE_WRITE);
    if access == MODE_READ {
        // SAFETY: The read access count is paired with final file release.
        unsafe { bindings::rust_helper_open_readcount_inc(inode) };
    } else {
        // SAFETY: The file mode was initialized by the path walk.
        let file_mode = unsafe { (*file).f_mode };
        // SAFETY: The inode is live and the open mode is known.
        let special = unsafe { bindings::rust_helper_open_special_file(inode) };
        if file_mode & MODE_WRITE != 0 && !special {
            // SAFETY: The helper acquires inode and mount write access.
            let error = unsafe { bindings::rust_helper_open_get_write_access(file) };
            if error != 0 {
                // SAFETY: No access or file operations were acquired.
                unsafe { cleanup_file_path(file) };
                return error;
            }
            // SAFETY: The write access acquisition succeeded.
            unsafe { (*file).f_mode |= MODE_WRITER };
        }
    }

    // SAFETY: The open mode has been established.
    let inode_mode = unsafe { (*inode).i_mode };
    if inode_mode & TYPE_MASK == TYPE_REGULAR || inode_mode & TYPE_MASK == TYPE_DIR {
        // SAFETY: The file mode is owned by this open attempt.
        unsafe { (*file).f_mode |= MODE_ATOMIC_POS };
    }

    // SAFETY: The inode operation table is live while the inode is referenced.
    let fops = unsafe { bindings::rust_helper_open_get_fops(inode) };
    // SAFETY: The file is owned by this open attempt.
    unsafe { (*file).f_op = fops };
    if fops.is_null() {
        // SAFETY: The helper emits the original WARN_ON diagnostic.
        unsafe {
            bindings::rust_helper_open_warn_missing_fops();
            bindings::rust_helper_open_put_fops(fops);
            bindings::rust_helper_open_put_file_access(file);
            cleanup_file_path(file);
        }
        return negative(bindings::ENODEV);
    }

    // SAFETY: The file and inode remain live through the security hook.
    let mut error = unsafe { bindings::security_file_open(file) };
    if error == 0 {
        // SAFETY: Permission watches are applied before leases and callbacks.
        error = unsafe { bindings::rust_helper_open_fsnotify_perm(file) };
    }
    if error == 0 {
        // SAFETY: The file has a live inode and open flags.
        error = unsafe { bindings::rust_helper_open_break_lease(file) };
    }
    if error != 0 {
        // SAFETY: The operations reference and file access are still owned.
        unsafe {
            bindings::rust_helper_open_put_fops(fops);
            bindings::rust_helper_open_put_file_access(file);
            cleanup_file_path(file);
        }
        // SAFETY: The helper preserves the original warning and normalization.
        return unsafe { bindings::rust_helper_open_normalize_cleanup_error(error) };
    }

    // SAFETY: The file operations reference and access are now durable.
    unsafe { (*file).f_mode |= MODE_LSEEK | MODE_PREAD | MODE_PWRITE };
    // SAFETY: The file operation table is live and referenced by this file.
    let callback = callback.or(unsafe { (*(*file).f_op).open });
    if let Some(open) = callback {
        // SAFETY: The filesystem callback receives the original file context.
        error = unsafe { open(inode, file) };
        if error != 0 {
            // SAFETY: The operation reference and access remain owned.
            unsafe {
                bindings::rust_helper_open_put_fops(fops);
                bindings::rust_helper_open_put_file_access(file);
                cleanup_file_path(file);
            }
            // SAFETY: The helper preserves the original warning and
            // normalization.
            return unsafe { bindings::rust_helper_open_normalize_cleanup_error(error) };
        }
    }

    // SAFETY: The file remains exclusively owned by this open attempt.
    unsafe {
        (*file).f_mode |= MODE_OPENED;
        let fops = &*(*file).f_op;
        if (*file).f_mode & MODE_READ != 0 && (fops.read.is_some() || fops.read_iter.is_some()) {
            (*file).f_mode |= MODE_CAN_READ;
        }
        if (*file).f_mode & MODE_WRITE != 0 && (fops.write.is_some() || fops.write_iter.is_some()) {
            (*file).f_mode |= MODE_CAN_WRITE;
        }
        if (*file).f_mode & MODE_LSEEK != 0 && fops.llseek.is_none() {
            (*file).f_mode &= !MODE_LSEEK;
        }
        if !(*file).f_mapping.is_null() {
            let a_ops = (*(*file).f_mapping).a_ops;
            if !a_ops.is_null() && (*a_ops).direct_IO.is_some() {
                (*file).f_mode |= MODE_CAN_ODIRECT;
            }
        }
        (*file).f_flags &=
            !(FLAG_CREAT | FLAG_EXCL | FLAG_NOCTTY | FLAG_TRUNC | FLAG_REGULAR) as c_uint;
        (*file).f_iocb_flags = bindings::rust_helper_open_iocb_flags(file) as c_uint;
        let host_mapping = (*(*(*file).f_mapping).host).i_mapping;
        bindings::file_ra_state_init(file_ra(file), host_mapping);
    }

    // SAFETY: The file is fully initialized by this open attempt.
    let direct = unsafe { (*file).f_flags } & FLAG_DIRECT as c_uint != 0;
    // SAFETY: The file mode is fully initialized by this open attempt.
    let can_odirect = unsafe { (*file).f_mode } & MODE_CAN_ODIRECT != 0;
    if direct && !can_odirect {
        return negative(bindings::EINVAL);
    }
    0
}

/// Defines `finish_open()` in Rust.
///
/// # Safety
///
/// `file` must be a live empty open result and `dentry` must stay referenced
/// for the duration of the callback.
#[export]
unsafe extern "C" fn finish_open(
    file: *mut File,
    dentry: *mut Dentry,
    callback: OpenCallback,
) -> c_int {
    // SAFETY: The helper reads the open state of the live empty file.
    if unsafe { bindings::rust_helper_open_is_opened(file) } {
        // SAFETY: The helper emits a warning for the invalid caller state.
        unsafe { bindings::rust_helper_open_warn_invalid_result() };
        return negative(bindings::EBUSY);
    }
    // SAFETY: The caller transfers a referenced dentry into the open path.
    unsafe { (*file_path(file)).dentry = dentry };
    // SAFETY: The caller supplies the open contracts.
    unsafe { do_dentry_open(file, callback) }
}

/// Defines `finish_no_open()` in Rust.
///
/// # Safety
///
/// `file` must be a live empty open result and `dentry` must be a live dentry
/// or an error pointer.
#[export]
unsafe extern "C" fn finish_no_open(file: *mut File, dentry: *mut Dentry) -> c_int {
    if is_error(dentry) {
        return read_error(dentry);
    }
    // SAFETY: The caller transfers the dentry ownership through the file path.
    unsafe { (*file_path(file)).dentry = dentry };
    0
}

/// Defines `vfs_open()` in Rust.
///
/// # Safety
///
/// `path` and `file` must satisfy the VFS open contracts.
#[export]
unsafe extern "C" fn vfs_open(path: *const Path, file: *mut File) -> c_int {
    // SAFETY: The caller transfers a referenced path into the file.
    unsafe { *file_path(file) = *path };
    // SAFETY: The file is otherwise initialized and empty.
    let error = unsafe { do_dentry_open(file, None) };
    if error == 0 {
        // SAFETY: FMODE_OPENED makes the matching close notification valid.
        unsafe { bindings::rust_helper_open_fsnotify_open(file) };
    }
    error
}

/// Runs atomic filesystem open for one negative final component.
///
/// # Safety
///
/// `nd`, `dentry`, and `file` must satisfy the filesystem atomic-open ABI.
unsafe fn atomic_open(
    path: *const Path,
    dentry: *mut Dentry,
    file: *mut File,
    open_flag: c_int,
    mode: Umode,
    create_error: c_int,
) -> *mut Dentry {
    let dentry_not_set = (-1isize) as *mut Dentry;
    // SAFETY: The path contains the referenced parent dentry and live inode.
    let dir_inode = unsafe { (*(*path).dentry).d_inode };

    // SAFETY: The file is empty and this callback owns its temporary path.
    unsafe {
        (*file_path(file)).dentry = dentry_not_set;
        (*file_path(file)).mnt = (*path).mnt;
        let open = (*(*dir_inode).i_op)
            .atomic_open
            .expect("atomic_open callback must remain installed");
        let error = open(
            dir_inode,
            dentry,
            file,
            bindings::rust_helper_open_namei_flags(open_flag),
            mode,
        );
        bindings::rust_helper_open_d_lookup_done(dentry);

        if error == 0 {
            if (*file).f_mode & MODE_OPENED != 0 {
                let opened = (*file_path(file)).dentry;
                if opened != dentry {
                    bindings::dput(dentry);
                    return bindings::rust_helper_open_dget(opened);
                }
                return dentry;
            } else if (*file_path(file)).dentry != dentry_not_set {
                let replaced = (*file_path(file)).dentry;
                let mut result = dentry;
                if !replaced.is_null() {
                    bindings::dput(dentry);
                    result = replaced;
                }
                if bindings::rust_helper_open_dentry_negative(result) {
                    bindings::dput(result);
                    return error_ptr(negative(bindings::ENOENT));
                }
                return result;
            } else {
                bindings::rust_helper_open_warn_atomic_unset(dentry);
                return error_ptr(negative(bindings::EIO));
            }
        }

        let mut error = error;
        if create_error != 0 && error == negative(bindings::ENOENT) {
            bindings::rust_helper_open_audit_child_create(dir_inode, dentry);
            error = create_error;
        }
        bindings::dput(dentry);
        error_ptr(error)
    }
}

struct LookupResult {
    dentry: *mut Dentry,
    retry: bool,
}

/// Runs one lookup and creation attempt for the final open component.
///
/// # Safety
///
/// `nd` and `file` must satisfy the final-component lookup contracts.
unsafe fn lookup_open_once(
    nd: *mut Nameidata,
    file: *mut File,
    op: *const OpenFlags,
) -> LookupResult {
    // SAFETY: The caller keeps the open-flag descriptor live for this attempt.
    let open_flags = unsafe { *op };
    let mut open_flag = open_flags.open_flag;
    let mut got_write = false;
    let mut mode = open_flags.mode;
    let mut create_error = 0;
    let mut delegated = ptr::null_mut();

    if open_flag & (FLAG_CREAT | FLAG_TRUNC | FLAG_WRONLY | FLAG_RDWR) != 0 {
        // SAFETY: The helper obtains the matching mount write reference.
        got_write = unsafe { bindings::rust_helper_open_nd_want_write(nd) } == 0;
    }

    let exclusive = open_flag & FLAG_CREAT != 0;
    // SAFETY: The helper takes the parent inode lock in the requested mode.
    unsafe { bindings::rust_helper_open_nd_inode_lock(nd, exclusive) };

    // SAFETY: The nameidata owns a referenced and locked parent dentry.
    let dir_inode = unsafe { bindings::rust_helper_open_nd_dentry_inode(nd) };
    // SAFETY: The parent dentry was checked by the active final-component walk.
    if unsafe { bindings::rust_helper_open_nd_dead_dir(nd) } {
        // SAFETY: This attempt owns the parent lock and optional mount write.
        unsafe {
            bindings::rust_helper_open_nd_inode_unlock(nd, exclusive);
            if got_write {
                bindings::rust_helper_open_nd_drop_write(nd);
            }
        }
        return LookupResult {
            dentry: error_ptr(negative(bindings::ENOENT)),
            retry: false,
        };
    }

    // SAFETY: The file mode is owned by this open attempt.
    unsafe { (*file).f_mode &= !MODE_CREATED };
    // SAFETY: The final qstr remains live in the active nameidata.
    let mut dentry = unsafe { bindings::rust_helper_open_d_lookup(nd) };
    let mut lookup_error = 0;

    'resolve: loop {
        if dentry.is_null() {
            // SAFETY: Parallel lookup allocates a child of the locked parent.
            dentry = unsafe { bindings::rust_helper_open_d_alloc_parallel(nd) };
            if is_error(dentry) {
                lookup_error = read_error(dentry);
                break 'resolve;
            }
        }
        // SAFETY: The dentry is either just allocated or returned by lookup.
        if !unsafe { bindings::rust_helper_open_d_in_lookup(dentry) } {
            // SAFETY: The dentry is live and the final name is still owned by
            // the active nameidata.
            let error = unsafe { bindings::rust_helper_open_d_revalidate(nd, dentry) };
            if error > 0 {
                break 'resolve;
            }
            if error != 0 {
                lookup_error = error;
                break 'resolve;
            }
            // SAFETY: A failed positive revalidation invalidates and releases
            // the cached dentry before a new parallel lookup.
            unsafe {
                bindings::d_invalidate(dentry);
                bindings::dput(dentry);
            }
            dentry = ptr::null_mut();
            continue 'resolve;
        }
        break 'resolve;
    }

    // SAFETY: A successful resolve loop returns a live dentry or error pointer.
    if lookup_error == 0 && unsafe { bindings::rust_helper_open_dentry_positive(dentry) } {
        // SAFETY: The positive dentry is opened after these checks.
        unsafe {
            if got_write {
                bindings::rust_helper_open_nd_drop_write(nd);
            }
            bindings::rust_helper_open_nd_inode_unlock(nd, exclusive);
        }
        return LookupResult {
            dentry,
            retry: false,
        };
    }

    if lookup_error == 0 && open_flag & FLAG_CREAT != 0 {
        // SAFETY: Parent audit and the idmap are read from the active lookup.
        unsafe { bindings::rust_helper_open_nd_audit_parent(nd) };
    }

    if !got_write {
        open_flag &= !FLAG_TRUNC;
    }
    // SAFETY: The active lookup retains its mount idmap for this attempt.
    let idmap = unsafe { bindings::rust_helper_open_nd_idmap(nd) };
    if lookup_error == 0 && open_flag & FLAG_CREAT != 0 {
        if open_flag & FLAG_EXCL != 0 {
            open_flag &= !FLAG_TRUNC;
        }
        // SAFETY: The helper applies the VFS mode policy to the final parent.
        mode = unsafe { bindings::rust_helper_open_prepare_mode(idmap, dir_inode, mode) };
        if got_write {
            // SAFETY: Permission checks use the active parent path and dentry.
            create_error =
                unsafe { bindings::rust_helper_open_may_o_create(idmap, nd, dentry, mode) };
        } else {
            create_error = negative(bindings::EROFS);
        }
    }
    if create_error != 0 {
        open_flag &= !FLAG_CREAT;
    }

    // SAFETY: The parent inode and its operation table remain live under lock.
    if lookup_error == 0 && unsafe { (*(*dir_inode).i_op).atomic_open.is_some() } {
        // SAFETY: The requested lookup flags belong to the active nameidata.
        if unsafe { bindings::rust_helper_path_requires_directory(nd) } {
            open_flag |= FLAG_DIRECTORY;
        }
        // SAFETY: The parent path, final dentry, and file satisfy atomic-open
        // input ownership.
        dentry = unsafe {
            atomic_open(
                bindings::rust_helper_open_nd_path(nd),
                dentry,
                file,
                open_flag,
                mode,
                create_error,
            )
        };
        lookup_error = if is_error(dentry) {
            read_error(dentry)
        } else {
            0
        };
    }

    // SAFETY: The resolved dentry remains live while the parent is locked.
    if lookup_error == 0 && unsafe { bindings::rust_helper_open_d_in_lookup(dentry) } {
        // SAFETY: The filesystem lookup owns the in-progress dentry.
        let replacement = unsafe { bindings::rust_helper_open_fs_lookup(dir_inode, dentry, nd) };
        // SAFETY: The filesystem has completed its lookup operation.
        unsafe { bindings::rust_helper_open_d_lookup_done(dentry) };
        if !replacement.is_null() {
            if is_error(replacement) {
                lookup_error = read_error(replacement);
            } else {
                // SAFETY: The filesystem returned a replacement dentry.
                unsafe {
                    bindings::dput(dentry);
                    dentry = replacement;
                }
            }
        }
    }

    if lookup_error == 0
        // SAFETY: The resolved dentry remains live until the final cleanup.
        && (unsafe { bindings::rust_helper_open_dentry_positive(dentry) }
            || open_flags.open_flag & FLAG_CREAT == 0)
    {
        // Intentionally fall through to the common completion block.
    } else if lookup_error == 0 {
        // SAFETY: The negative child is about to be created.
        unsafe {
            bindings::rust_helper_open_audit_child_create(dir_inode, dentry);
        }

        if create_error != 0 {
            lookup_error = create_error;
        } else {
            // SAFETY: The helper returns any delegated inode while retaining
            // its references for the matching wait operation.
            let error =
                unsafe { bindings::rust_helper_open_try_break_deleg(dir_inode, &mut delegated) };
            if error != 0 {
                lookup_error = error;
            } else {
                // SAFETY: The file creation bit is owned by this attempt.
                unsafe { (*file).f_mode |= MODE_CREATED };
                // SAFETY: The parent inode and create callback remain live
                // under the parent lock.
                let has_create = unsafe { (*(*dir_inode).i_op).create.is_some() };
                if !has_create {
                    lookup_error = negative(bindings::EACCES);
                } else {
                    // SAFETY: The filesystem receives the prepared create mode.
                    lookup_error = unsafe {
                        bindings::rust_helper_open_fs_create(idmap, dir_inode, dentry, mode)
                    };
                }
            }
        }
    }

    if is_error(dentry) {
        // The lookup or atomic-open path already produced its final error.
    } else if lookup_error != 0 {
        // SAFETY: A failed final-component lookup releases its dentry.
        unsafe { bindings::dput(dentry) };
        dentry = error_ptr(lookup_error);
    } else {
        // SAFETY: Creation and atomic-open notifications are selected by the
        // final file mode.
        unsafe {
            if (*file).f_mode & MODE_CREATED != 0 {
                bindings::rust_helper_open_fsnotify_create(dir_inode, dentry);
            }
            if (*file).f_mode & MODE_OPENED != 0 {
                bindings::rust_helper_open_fsnotify_open(file);
            }
        }
    }

    // SAFETY: The parent lock mode and optional mount write follow the path
    // taken by this attempt.
    unsafe {
        if open_flag & FLAG_CREAT != 0 || create_error != 0 {
            bindings::rust_helper_open_nd_inode_unlock(nd, true);
        } else {
            bindings::rust_helper_open_nd_inode_unlock(nd, false);
        }
        if got_write {
            bindings::rust_helper_open_nd_drop_write(nd);
        }
    }

    if !delegated.is_null() {
        // SAFETY: The pointer owns the inode and superblock references taken by
        // try_break_deleg().
        let error = unsafe { bindings::rust_helper_open_break_deleg_wait(delegated) };
        if error == 0 {
            return LookupResult {
                dentry: error_ptr(negative(bindings::EAGAIN)),
                retry: true,
            };
        }
        return LookupResult {
            dentry: error_ptr(error),
            retry: false,
        };
    }

    LookupResult {
        dentry,
        retry: false,
    }
}

/// Looks up and creates the final open component, including delegation retry.
///
/// # Safety
///
/// `nd` and `file` must satisfy the final-component lookup contracts.
unsafe fn lookup_open(nd: *mut Nameidata, file: *mut File, op: *const OpenFlags) -> *mut Dentry {
    loop {
        // SAFETY: The caller supplies the final-component lookup state.
        let result = unsafe { lookup_open_once(nd, file, op) };
        if !result.retry {
            return result.dentry;
        }
    }
}

/// Runs the fast lookup used before the final open walk.
///
/// # Safety
///
/// `nd` must contain the active final-component lookup state.
unsafe fn lookup_fast_for_open(nd: *mut Nameidata, open_flag: c_int) -> *mut Dentry {
    if open_flag & FLAG_CREAT != 0 {
        // SAFETY: The trailing-slash flags belong to the active nameidata.
        if unsafe { bindings::rust_helper_open_nd_trailing_slashes(nd) } {
            return error_ptr(negative(bindings::EISDIR));
        }
        if open_flag & FLAG_EXCL != 0 {
            return ptr::null_mut();
        }
    }

    // SAFETY: The trailing-slash flags belong to the active nameidata.
    if unsafe { bindings::rust_helper_open_nd_trailing_slashes(nd) } {
        // SAFETY: Trailing slashes require a followed directory.
        unsafe {
            bindings::rust_helper_open_nd_add_flags(nd, LOOKUP_FOLLOW | LOOKUP_DIRECTORY);
        }
    }

    // SAFETY: The nameidata owns a live final name and parent path.
    let dentry = unsafe { bindings::rust_helper_open_lookup_fast(nd) };
    if is_error(dentry) || dentry.is_null() {
        return dentry;
    }

    // SAFETY: Fast lookup returned a live dentry or error pointer.
    if open_flag & FLAG_CREAT != 0 && unsafe { bindings::rust_helper_open_dentry_negative(dentry) }
    {
        // SAFETY: The nameidata reports whether it owns an RCU read section.
        if !unsafe { bindings::rust_helper_open_nd_in_rcu(nd) } {
            // SAFETY: A ref-walk negative dentry carries its own reference.
            unsafe { bindings::dput(dentry) };
        }
        return ptr::null_mut();
    }
    dentry
}

/// Looks up the final component and transfers it into the open walk.
///
/// # Safety
///
/// `nd` and `file` must satisfy the open lookup contracts.
unsafe fn open_last_lookups(
    nd: *mut Nameidata,
    file: *mut File,
    op: *const OpenFlags,
) -> *const c_uchar {
    // SAFETY: The caller keeps the open-flag descriptor live for this walk.
    let open_flag = unsafe { (*op).open_flag };
    // SAFETY: The operation intent belongs to the active lookup.
    unsafe { bindings::rust_helper_open_nd_add_flags(nd, (*op).intent as c_uint) };

    // SAFETY: The active nameidata records its current final-component type.
    let last_type = unsafe { bindings::rust_helper_path_last_type(nd) };
    if last_type != bindings::last_type_LAST_NORM {
        // SAFETY: A non-normal final component releases any nested link.
        unsafe { bindings::rust_helper_open_nd_put_link_if_needed(nd) };
        // SAFETY: The nameidata retains the final type and dot handling state.
        return unsafe { bindings::rust_helper_open_nd_handle_dots(nd, last_type as c_uint) }
            .cast();
    }

    // SAFETY: The nameidata owns the active final-component lookup state.
    let mut dentry = unsafe { lookup_fast_for_open(nd, open_flag) };
    if is_error(dentry) {
        return dentry.cast();
    }
    if dentry.is_null() {
        if open_flag & FLAG_CREAT == 0 {
            // SAFETY: The nameidata reports whether fast lookup used RCU.
            if unsafe { bindings::rust_helper_open_nd_in_rcu(nd) } {
                // SAFETY: The helper emits the original WARN_ON_ONCE.
                unsafe { bindings::rust_helper_open_nd_warn_rcu_without_create() };
                return error_ptr::<Dentry>(negative(bindings::ECHILD)).cast();
            }
        } else if
        // SAFETY: The nameidata reports whether fast lookup used RCU.
        unsafe { bindings::rust_helper_open_nd_in_rcu(nd) }
            // SAFETY: A successful unlazy attempt establishes a ref-walk state.
            && !unsafe { bindings::rust_helper_open_nd_try_to_unlazy(nd) }
        {
            return error_ptr::<Dentry>(negative(bindings::ECHILD)).cast();
        }

        // SAFETY: The active nameidata and allocated file own the lookup state.
        dentry = unsafe { lookup_open(nd, file, op) };
        if is_error(dentry) {
            return dentry.cast();
        }

        // SAFETY: The file is live and owned by this open attempt.
        let mode = unsafe { (*file).f_mode };
        if mode & (MODE_OPENED | MODE_CREATED) != 0 {
            // SAFETY: The final component becomes the active open path.
            unsafe { bindings::rust_helper_open_nd_replace_final_dentry(nd, dentry) };
            return ptr::null();
        }
    }

    // SAFETY: The final dentry is referenced for trailing traversal.
    unsafe { bindings::rust_helper_open_nd_put_link_if_needed(nd) };
    // SAFETY: The nameidata and final dentry remain live for traversal.
    let result = unsafe { bindings::rust_helper_open_nd_step_into(nd, dentry) };
    if !result.is_null() {
        // SAFETY: Failed trailing traversal returns an error pointer.
        unsafe { bindings::rust_helper_open_nd_clear_lookup_flags(nd) };
    }
    result.cast()
}

/// Completes the final open checks and invokes the file open path.
///
/// # Safety
///
/// `nd` and `file` must contain a successful final-component lookup.
unsafe fn do_open(nd: *mut Nameidata, file: *mut File, op: *const OpenFlags) -> c_int {
    // SAFETY: The caller keeps the open-flag descriptor live for this step.
    let open_flags = unsafe { *op };
    let mut open_flag = open_flags.open_flag;
    // SAFETY: The file is live and owned by this open attempt.
    let file_mode = unsafe { (*file).f_mode };

    let mut error;
    if file_mode & (MODE_OPENED | MODE_CREATED) == 0 {
        // SAFETY: The path walk is completed by the original helper.
        error = unsafe { bindings::rust_helper_path_complete_walk(nd) };
        if error != 0 {
            return error;
        }
    }
    if file_mode & MODE_CREATED == 0 {
        // SAFETY: Successful lookup records the final inode in audit.
        unsafe {
            bindings::rust_helper_open_nd_audit_inode(nd, bindings::rust_helper_open_nd_dentry(nd))
        };
    }

    // SAFETY: The active lookup retains its mount idmap.
    let idmap = unsafe { bindings::rust_helper_open_nd_idmap(nd) };
    // SAFETY: A completed path walk owns a referenced final dentry.
    let dentry = unsafe { bindings::rust_helper_open_nd_dentry(nd) };
    if open_flag & FLAG_CREAT != 0 {
        if open_flag & FLAG_EXCL != 0 && file_mode & MODE_CREATED == 0 {
            return negative(bindings::EEXIST);
        }
        // SAFETY: The final dentry remains live for the completed walk.
        if unsafe { bindings::rust_helper_open_dentry_dir(dentry) } {
            return negative(bindings::EISDIR);
        }
        // SAFETY: The final dentry remains live and may have no backing inode.
        let inode = unsafe { bindings::rust_helper_open_dentry_backing_inode(dentry) };
        // SAFETY: Sticky permission checks use the active lookup and inode.
        error = unsafe { bindings::rust_helper_open_may_create_in_sticky(idmap, nd, inode) };
        if error != 0 {
            return error;
        }
    }

    if open_flag & FLAG_REGULAR != 0
        // SAFETY: The final dentry remains live for the completed walk.
        && !unsafe { bindings::rust_helper_open_dentry_regular(dentry) }
    {
        return negative(bindings::EFTYPE);
    }
    // SAFETY: The active lookup retains its requested final path type.
    if unsafe { bindings::rust_helper_path_requires_directory(nd) }
        // SAFETY: The final dentry remains live and is checked only after the
        // lookup succeeded.
        && !unsafe { bindings::rust_helper_open_dentry_can_lookup(dentry) }
    {
        return negative(bindings::ENOTDIR);
    }

    let mut do_truncate = false;
    let mut acc_mode = open_flags.acc_mode;
    if file_mode & MODE_CREATED != 0 {
        open_flag &= !FLAG_TRUNC;
        acc_mode = 0;
    // SAFETY: The final dentry remains live and determines truncate handling.
    } else if unsafe { bindings::rust_helper_open_dentry_regular(dentry) }
        && open_flag & FLAG_TRUNC != 0
    {
        // SAFETY: Truncation requires a mount write reference.
        error = unsafe { bindings::rust_helper_open_nd_want_write(nd) };
        if error != 0 {
            return error;
        }
        do_truncate = true;
    }

    // SAFETY: The completed lookup retains its idmap, path, and intent.
    error = unsafe { bindings::rust_helper_open_may_open(idmap, nd, acc_mode, open_flag) };
    // SAFETY: The file is live and has not completed the open operation.
    if error == 0 && unsafe { (*file).f_mode } & MODE_OPENED == 0 {
        // SAFETY: The path remains referenced by nameidata during open.
        error = unsafe { vfs_open(bindings::rust_helper_open_nd_path(nd), file) };
    }
    if error == 0 {
        // SAFETY: The security hook receives the completed open file.
        error = unsafe { bindings::security_file_post_open(file, acc_mode) };
    }
    if error == 0 && do_truncate {
        // SAFETY: The file has completed open and owns the write reference.
        error = unsafe { bindings::rust_helper_open_handle_truncate(idmap, file) };
    }
    if error > 0 {
        // SAFETY: The helper preserves the original positive-error warning.
        unsafe { bindings::rust_helper_open_warn_invalid_result() };
        error = negative(bindings::EINVAL);
    }
    if do_truncate {
        // SAFETY: This pairs the successful mount write acquisition.
        unsafe { bindings::rust_helper_open_nd_drop_write(nd) };
    }
    error
}

/// Runs one O_TMPFILE open attempt.
///
/// # Safety
///
/// `nd` and `file` must satisfy the O_TMPFILE open contracts.
unsafe fn do_tmpfile(
    nd: *mut Nameidata,
    flags: c_uint,
    op: *const OpenFlags,
    file: *mut File,
) -> c_int {
    let mut path = MaybeUninit::<Path>::uninit();
    // SAFETY: The temporary path is writable and the lookup owns cleanup.
    let error = unsafe { bindings::path_lookupat(nd, flags | LOOKUP_DIRECTORY, path.as_mut_ptr()) };
    if error != 0 {
        return error;
    }
    // SAFETY: A successful path lookup initialized the temporary path.
    let path = unsafe { path.assume_init() };

    // SAFETY: The parent mount is live and the temporary directory is writable.
    let error = unsafe { bindings::mnt_want_write(path.mnt) };
    if error != 0 {
        // SAFETY: The lookup transferred a referenced path.
        unsafe { bindings::path_put(&path) };
        return error;
    }

    // SAFETY: The temporary path retains its mount and dentry references.
    let idmap = unsafe { bindings::rust_helper_open_path_idmap(&path) };
    // SAFETY: The temporary parent path and empty file are live.
    let error = unsafe { bindings::vfs_tmpfile(idmap, &path, file, (*op).mode) };
    if error == 0 {
        // SAFETY: A tmpfile open records the actual final dentry.
        unsafe { bindings::rust_helper_path_audit_inode((*nd).name, (*file_path(file)).dentry, 0) };
    }
    // SAFETY: This pairs the successful mount write acquisition.
    unsafe {
        bindings::mnt_drop_write(path.mnt);
        bindings::path_put(&path);
    }
    error
}

/// Runs one O_PATH open attempt.
///
/// # Safety
///
/// `nd` and `file` must satisfy the O_PATH open contracts.
unsafe fn do_o_path(nd: *mut Nameidata, flags: c_uint, file: *mut File) -> c_int {
    let mut path = MaybeUninit::<Path>::uninit();
    // SAFETY: The temporary path is writable and the lookup owns cleanup.
    let mut error = unsafe { bindings::path_lookupat(nd, flags, path.as_mut_ptr()) };
    if error == 0 {
        // SAFETY: A successful path lookup initialized the temporary path.
        let path = unsafe { path.assume_init() };
        // SAFETY: A successful O_PATH lookup produced a referenced path.
        unsafe {
            bindings::rust_helper_path_audit_inode((*nd).name, path.dentry, 0);
            error = vfs_open(&path, file);
            bindings::path_put(&path);
        }
    }
    error
}

#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
fn record_attempt(flags: c_uint) {
    ATTEMPTS.fetch_add(1, Ordering::Relaxed);
    ATTEMPT_FLAGS.store(flags, Ordering::Release);
}

#[cfg(not(CONFIG_OPEN_ENTRY_KUNIT_TEST))]
fn record_attempt(_flags: c_uint) {}

#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
fn injected_failure() -> c_int {
    let failure = match NEXT_FAILURE.swap(0, Ordering::AcqRel) {
        0 => RETRY_FAILURE.swap(0, Ordering::AcqRel),
        failure => failure,
    };
    match failure {
        TEST_FAIL_ECHILD => negative(bindings::ECHILD),
        TEST_FAIL_EOPENSTALE => negative(bindings::EOPENSTALE),
        _ => 0,
    }
}

#[cfg(not(CONFIG_OPEN_ENTRY_KUNIT_TEST))]
fn injected_failure() -> c_int {
    0
}

/// Runs one complete open path attempt.
///
/// # Safety
///
/// `nd` must contain initialized open lookup state and `op` must remain live.
unsafe fn path_openat(nd: *mut Nameidata, op: *const OpenFlags, flags: c_uint) -> *mut File {
    record_attempt(flags);

    // SAFETY: The current task has a live credential and the file allocator
    // returns an independent file reference.
    let file = unsafe { bindings::alloc_empty_file((*op).open_flag, current_cred()) };
    if is_error(file) {
        return file;
    }

    let failure = injected_failure();
    if failure != 0 {
        // SAFETY: This attempt owns the newly allocated file.
        unsafe { bindings::fput_close(file) };
        let error = if failure == negative(bindings::EOPENSTALE) {
            if flags & LOOKUP_RCU != 0 {
                negative(bindings::ECHILD)
            } else {
                negative(bindings::ESTALE)
            }
        } else {
            failure
        };
        return error_ptr(error);
    }

    let mut error;
    // SAFETY: The file was allocated for this attempt and remains live.
    if unsafe { (*file).f_flags } & FLAG_TMPFILE as c_uint != 0 {
        // SAFETY: The file and final path state are live.
        error = unsafe { do_tmpfile(nd, flags, op, file) };
    // SAFETY: The file was not consumed by the previous branch.
    } else if unsafe { (*file).f_flags } & FLAG_PATH as c_uint != 0 {
        // SAFETY: The file and final path state are live.
        error = unsafe { do_o_path(nd, flags, file) };
    } else {
        // SAFETY: The helper starts one RCU or ref-walk.
        let mut name = unsafe { bindings::rust_helper_path_init(nd, flags) };
        loop {
            // SAFETY: The lookup state is active and `name` is valid.
            error = unsafe { bindings::rust_helper_path_link_path_walk(name, nd) };
            if error != 0 {
                break;
            }
            // SAFETY: The path walk reached the final component.
            name = unsafe { open_last_lookups(nd, file, op) };
            if name.is_null() {
                break;
            }
        }
        if error == 0 {
            // SAFETY: The final path and file state are ready for completion.
            error = unsafe { do_open(nd, file, op) };
        }
        // SAFETY: Every path_openat attempt owns one termination.
        unsafe { bindings::rust_helper_path_terminate_walk(nd) };
    }

    if error == 0 {
        // SAFETY: The file remains owned by this attempt after completion.
        if unsafe { (*file).f_mode } & MODE_OPENED != 0 {
            return file;
        }
        // SAFETY: The helper emits the original impossible-result warning.
        unsafe { bindings::rust_helper_open_warn_invalid_result() };
        error = negative(bindings::EINVAL);
    }

    // SAFETY: The failed open no longer exposes the partially initialized file.
    unsafe { bindings::fput_close(file) };
    if error == negative(bindings::EOPENSTALE) {
        error = if flags & LOOKUP_RCU != 0 {
            negative(bindings::ECHILD)
        } else {
            negative(bindings::ESTALE)
        };
    }
    error_ptr(error)
}

/// Defines `do_file_open()` in Rust.
///
/// # Safety
///
/// `pathname` and `op` must satisfy the filename open contracts.
#[export]
unsafe extern "C" fn do_file_open(
    dfd: c_int,
    pathname: *mut Filename,
    op: *const OpenFlags,
) -> *mut File {
    if is_error(pathname) {
        return pathname.cast();
    }

    let mut nd = MaybeUninit::<Nameidata>::uninit();
    // SAFETY: The local storage is writable and this entry owns cleanup.
    unsafe {
        bindings::rust_helper_path_set_nameidata(nd.as_mut_ptr(), dfd, pathname, ptr::null())
    };
    // SAFETY: The caller keeps the open-flag descriptor live for this open.
    let flags = unsafe { (*op).lookup_flags as c_uint };
    // SAFETY: The local nameidata belongs to this open sequence.
    let mut file = unsafe { path_openat(nd.as_mut_ptr(), op, flags | LOOKUP_RCU) };
    if file == error_ptr(negative(bindings::ECHILD)) {
        // SAFETY: The previous attempt terminated its path walk.
        file = unsafe { path_openat(nd.as_mut_ptr(), op, flags) };
    }
    if file == error_ptr(negative(bindings::ESTALE)) {
        // SAFETY: The previous attempt terminated its path walk.
        file = unsafe { path_openat(nd.as_mut_ptr(), op, flags | LOOKUP_REVAL) };
    }
    // SAFETY: This restores the saved nameidata before returning.
    unsafe { bindings::rust_helper_path_restore_nameidata() };
    file
}

/// Defines `do_file_open_root()` in Rust.
///
/// # Safety
///
/// `root` and `name` must satisfy the scoped kernel open contracts.
#[export]
unsafe extern "C" fn do_file_open_root(
    root: *const Path,
    name: *const c_uchar,
    op: *const OpenFlags,
) -> *mut File {
    // SAFETY: The caller supplies live root and open-flag state.
    if unsafe { bindings::rust_helper_open_root_rejects_symlink(root, (*op).intent) } {
        return error_ptr(negative(bindings::ELOOP));
    }

    // SAFETY: The caller supplies a live NUL-terminated kernel name.
    let filename = unsafe { bindings::rust_helper_path_getname_kernel(name) };
    if is_error(filename) {
        return filename.cast();
    }

    let mut nd = MaybeUninit::<Nameidata>::uninit();
    // SAFETY: The local storage is writable and this entry owns cleanup.
    unsafe { bindings::rust_helper_path_set_nameidata(nd.as_mut_ptr(), -1, filename, root) };
    // SAFETY: The caller keeps the open-flag descriptor live for this open.
    let flags = unsafe { (*op).lookup_flags as c_uint };
    // SAFETY: The local nameidata belongs to this open sequence.
    let mut file = unsafe { path_openat(nd.as_mut_ptr(), op, flags | LOOKUP_RCU) };
    if file == error_ptr(negative(bindings::ECHILD)) {
        // SAFETY: The previous attempt terminated its path walk.
        file = unsafe { path_openat(nd.as_mut_ptr(), op, flags) };
    }
    if file == error_ptr(negative(bindings::ESTALE)) {
        // SAFETY: The previous attempt terminated its path walk.
        file = unsafe { path_openat(nd.as_mut_ptr(), op, flags | LOOKUP_REVAL) };
    }
    // SAFETY: This restores the saved nameidata before returning.
    unsafe { bindings::rust_helper_path_restore_nameidata() };
    // SAFETY: The open sequence no longer uses the temporary filename.
    unsafe { bindings::rust_helper_path_putname(filename) };
    file
}

/// Defines `vfs_lookup_open()` in Rust.
///
/// # Safety
///
/// `parent` and `last` must satisfy the exported final-component open ABI.
#[export]
unsafe extern "C" fn vfs_lookup_open(
    parent: *mut Path,
    last: *mut Qstr,
    open_flag: c_int,
    mode: Umode,
) -> *mut File {
    // SAFETY: The helper preserves the exported request warnings.
    unsafe { bindings::rust_helper_open_warn_vfs_lookup(c_int::from(mode), open_flag) };

    let mode = (mode | TYPE_REGULAR) as Umode;
    let open_flag = open_flag | FLAG_REGULAR;
    // SAFETY: The caller supplies a valid parent dentry and final qstr.
    let error = unsafe { bindings::lookup_noperm_common(last, (*parent).dentry) };
    if error != 0 {
        return error_ptr(error);
    }

    // SAFETY: The current credential is live and this entry owns the file.
    let file = unsafe { bindings::alloc_empty_file(open_flag, current_cred()) };
    if is_error(file) {
        return file;
    }

    let mut nd = MaybeUninit::<Nameidata>::uninit();
    // SAFETY: The helper initializes the final-component open state.
    unsafe { bindings::rust_helper_open_init_vfs_lookup(nd.as_mut_ptr(), parent, last, open_flag) };

    let op = OpenFlags {
        open_flag,
        mode,
        acc_mode: 0,
        intent: 0,
        lookup_flags: 0,
    };
    // SAFETY: The local final-component state and file are live.
    let dentry = unsafe { lookup_open(nd.as_mut_ptr(), file, &op) };
    let mut error = if is_error(dentry) {
        read_error(dentry)
    } else {
        0
    };

    // SAFETY: The final-component lookup returned a live dentry or error.
    if error == 0 && unsafe { bindings::rust_helper_open_dentry_really_negative(dentry) } {
        error = negative(bindings::ENOENT);
    } else if error == 0
        // SAFETY: The file is live and owned by the final-component lookup.
        && unsafe { (*file).f_mode } & MODE_CREATED == 0
        && open_flag & FLAG_EXCL != 0
    {
        error = negative(bindings::EEXIST);
    } else if error == 0 {
        // SAFETY: The final dentry remains live after the completed lookup.
        let file_type = unsafe { bindings::rust_helper_open_dentry_mode(dentry) } & TYPE_MASK;
        if file_type != TYPE_REGULAR {
            error = match file_type {
                TYPE_DIR => negative(bindings::EISDIR),
                TYPE_LINK => negative(bindings::ELOOP),
                TYPE_BLOCK | TYPE_CHAR => negative(bindings::ENODEV),
                TYPE_FIFO | TYPE_SOCKET => negative(bindings::EFTYPE),
                _ => negative(bindings::EFTYPE),
            };
        // SAFETY: The file is live and has not already selected O_PATH mode.
        } else if unsafe { (*file).f_mode } & MODE_OPENED == 0 {
            // SAFETY: The final component becomes the active vfs_open path.
            unsafe {
                bindings::rust_helper_open_nd_set_dentry(nd.as_mut_ptr(), dentry);
                error = vfs_open(bindings::rust_helper_open_nd_path(nd.as_mut_ptr()), file);
            }
        }
    }
    if !is_error(dentry) {
        // SAFETY: The final-component lookup transferred one dentry reference.
        unsafe { bindings::dput(dentry) };
    }
    if error != 0 {
        // SAFETY: This entry owns the empty or partially opened file.
        unsafe { bindings::fput_close(file) };
        return error_ptr(error);
    }
    file
}

/// Defines `dentry_create()` in Rust.
///
/// # Safety
///
/// `path` must hold a referenced parent mount and negative child dentry while
/// the caller owns the parent lock. `cred` must stay live during the open.
#[export]
unsafe extern "C" fn dentry_create(
    path: *mut Path,
    flags: c_int,
    mode: Umode,
    cred: *const Cred,
) -> *mut File {
    // SAFETY: The caller supplies a referenced parent path and child dentry.
    let dentry = unsafe { (*path).dentry };
    let original = dentry;
    // SAFETY: The negative child and its parent remain referenced by the path.
    let dir = unsafe { bindings::rust_helper_open_dentry_parent(dentry) };
    // SAFETY: The parent dentry is live and was locked by the caller.
    let dir_inode = unsafe { bindings::rust_helper_open_dentry_inode(dir) };

    // SAFETY: The caller supplies a live credential and this entry owns the
    // newly allocated file.
    let file = unsafe { bindings::alloc_empty_file(flags, cred) };
    if is_error(file) {
        return file;
    }

    // SAFETY: The caller retains the path while create operations use its idmap.
    let idmap = unsafe { bindings::rust_helper_open_path_idmap(path) };
    let mut error;
    // SAFETY: The live parent inode exposes its filesystem operation table.
    if unsafe { (*(*dir_inode).i_op).atomic_open.is_some() } {
        // SAFETY: The parent path is the atomic-open base and the child dentry
        // is the negative final component supplied by the caller.
        unsafe { (*path).dentry = dir };
        // SAFETY: The caller locked the parent and supplied a regular-file mode.
        let mode =
            unsafe { bindings::rust_helper_open_prepare_regular_mode(idmap, dir_inode, mode) };
        let mut open_flag = flags;
        // SAFETY: Sticky checks use the caller's path, child, and parent lock.
        let create_error =
            unsafe { bindings::rust_helper_open_path_may_o_create(idmap, path, dentry, mode) };
        if create_error != 0 {
            open_flag &= !FLAG_CREAT;
        }

        // The atomic-open helper consumes the extra child reference but keeps
        // the caller's original reference available for restoration.
        // SAFETY: The original dentry remains referenced by the caller's path.
        unsafe { bindings::rust_helper_open_dget(original) };
        // SAFETY: The parent path, child, and file satisfy atomic-open input
        // ownership.
        let opened = unsafe { atomic_open(path, dentry, file, open_flag, mode, create_error) };
        error = if is_error(opened) {
            read_error(opened)
        } else {
            0
        };
        let final_dentry = if error != 0 {
            original
        } else {
            // SAFETY: The successfully returned dentry replaces the caller's
            // original child reference.
            unsafe { bindings::dput(original) };
            opened
        };

        if error == 0 {
            // SAFETY: The final file mode selects create and open
            // notifications.
            unsafe {
                if (*file).f_mode & MODE_CREATED != 0 {
                    bindings::rust_helper_open_fsnotify_create(dir_inode, final_dentry);
                }
                if (*file).f_mode & MODE_OPENED != 0 {
                    bindings::rust_helper_open_fsnotify_open(file);
                }
                (*path).dentry = final_dentry;
            }
        } else {
            // SAFETY: The original child remains the caller-visible path.
            unsafe { (*path).dentry = original };
        }
    } else {
        // SAFETY: The filesystem create and subsequent open use the caller's
        // prepared path and file.
        error = unsafe { bindings::vfs_create(idmap, dentry, mode, ptr::null_mut()) };
        if error == 0 {
            // SAFETY: The newly created dentry is now opened for the caller.
            error = unsafe { vfs_open(path, file) };
        }
    }

    if error != 0 {
        // SAFETY: This entry owns the empty or partially opened file.
        unsafe { bindings::fput_close(file) };
        return error_ptr(error);
    }
    file
}

/// Resets open retry test state.
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
#[export]
extern "C" fn rust_open_test_reset() {
    NEXT_FAILURE.store(0, Ordering::Relaxed);
    RETRY_FAILURE.store(0, Ordering::Relaxed);
    ATTEMPTS.store(0, Ordering::Relaxed);
    ATTEMPT_FLAGS.store(0, Ordering::Relaxed);
}

/// Arms one open path attempt result.
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
#[export]
extern "C" fn rust_open_test_fail_next(failure: c_uint) {
    NEXT_FAILURE.store(failure, Ordering::Release);
}

/// Arms the second open path attempt result.
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
#[export]
extern "C" fn rust_open_test_fail_retry(failure: c_uint) {
    RETRY_FAILURE.store(failure, Ordering::Release);
}

/// Returns the number of completed open path attempts.
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
#[export]
extern "C" fn rust_open_test_attempts() -> c_uint {
    ATTEMPTS.load(Ordering::Acquire)
}

/// Returns the flags from the most recent open path attempt.
#[cfg(CONFIG_OPEN_ENTRY_KUNIT_TEST)]
#[export]
extern "C" fn rust_open_test_flags() -> c_uint {
    ATTEMPT_FLAGS.load(Ordering::Acquire)
}
