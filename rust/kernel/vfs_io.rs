// SPDX-License-Identifier: GPL-2.0

//! Native VFS I/O orchestration.
//!
//! Rust owns the range and overflow checks, read, write, and seek dispatch,
//! operation selection, length clamping, post-I/O notification ordering, and
//! synchronous write-freeze bracketing.
//!
//! C retains user-address checks, synchronous iovec and kiocb construction,
//! LSM and fsnotify hooks, task accounting primitives, filesystem callbacks,
//! and superblock write-freeze primitives.

use crate::bindings;
use core::ffi::{c_int, c_uchar, c_void};
use macros::export;

type File = bindings::file;
type FileOperations = bindings::file_operations;
type Lofl = bindings::loff_t;
type Ssize = isize;
type Fmode = bindings::fmode_t;

const MODE_READ: Fmode = bindings::rust_vfs_io_mode_RUST_VFS_IO_MODE_READ as Fmode;
const MODE_WRITE: Fmode = bindings::rust_vfs_io_mode_RUST_VFS_IO_MODE_WRITE as Fmode;
const MODE_LSEEK: Fmode = bindings::rust_vfs_io_mode_RUST_VFS_IO_MODE_LSEEK as Fmode;
const MODE_CAN_READ: Fmode = bindings::rust_vfs_io_mode_RUST_VFS_IO_MODE_CAN_READ as Fmode;
const MODE_CAN_WRITE: Fmode = bindings::rust_vfs_io_mode_RUST_VFS_IO_MODE_CAN_WRITE as Fmode;

const READ: c_int = bindings::rust_vfs_io_access_RUST_VFS_IO_READ as c_int;
const MAY_READ: c_int = bindings::rust_vfs_io_access_RUST_VFS_IO_MAY_READ as c_int;
const MAY_WRITE: c_int = bindings::rust_vfs_io_access_RUST_VFS_IO_MAY_WRITE as c_int;

const MAX_RW_COUNT: usize = bindings::rust_vfs_io_limit_RUST_VFS_IO_MAX_RW_COUNT as usize;

/// Returns the open mode of a live file.
///
/// # Safety
///
/// `file` must remain live for the duration of this call.
unsafe fn file_mode(file: *mut File) -> Fmode {
    // SAFETY: The caller guarantees a live file object.
    unsafe { (*file).f_mode }
}

/// Returns the operation table of a live file.
///
/// # Safety
///
/// `file` must remain live and keep its operation table referenced.
unsafe fn file_operations(file: *mut File) -> *const FileOperations {
    // SAFETY: The caller guarantees a live file with stable operation state.
    unsafe { (*file).f_op }
}

fn negative(error: u32) -> c_int {
    -(error as c_int)
}

fn negative_ssize(error: u32) -> Ssize {
    -(error as Ssize)
}

fn negative_loff(error: u32) -> Lofl {
    -Lofl::from(error)
}

/// Defines `vfs_llseek()` in Rust.
///
/// # Safety
///
/// `file` must be a live open file with a stable operation table.
#[export]
unsafe extern "C" fn vfs_llseek(file: *mut File, offset: Lofl, whence: c_int) -> Lofl {
    // SAFETY: The caller supplies a live open file.
    if unsafe { file_mode(file) } & MODE_LSEEK == 0 {
        return negative_loff(bindings::ESPIPE);
    }

    // SAFETY: The live file retains its operation table.
    let fops = unsafe { file_operations(file) };
    if fops.is_null() {
        return negative_loff(bindings::EINVAL);
    }

    // SAFETY: The operation table remains live for this call.
    match unsafe { (*fops).llseek } {
        // SAFETY: The filesystem callback accepts the live file and seek
        // arguments.
        Some(llseek) => unsafe { llseek(file, offset, whence) },
        None => negative_loff(bindings::EINVAL),
    }
}

/// Defines `rw_verify_area()` in Rust.
///
/// # Safety
///
/// `file` must be a live open file. `ppos` must contain a live file position
/// when it is non-null.
#[export]
unsafe extern "C" fn rw_verify_area(
    read_write: c_int,
    file: *mut File,
    ppos: *const Lofl,
    count: usize,
) -> c_int {
    let mask = if read_write == READ {
        MAY_READ
    } else {
        MAY_WRITE
    };

    if count > isize::MAX as usize {
        return negative(bindings::EINVAL);
    }

    if !ppos.is_null() {
        // SAFETY: A non-null position points to caller-owned live storage.
        let pos = unsafe { *ppos };
        if pos < 0 {
            // SAFETY: The live operation table selects the offset contract.
            if !unsafe { bindings::rust_helper_vfs_io_unsigned_offsets(file) } {
                return negative(bindings::EINVAL);
            }
            if count as u128 >= u128::from(pos.unsigned_abs()) {
                return negative(bindings::EOVERFLOW);
            }
        } else {
            let end = (pos as u64).wrapping_add(count as u64) as i64;
            if end < 0 {
                // SAFETY: The live operation table selects the offset contract.
                if !unsafe { bindings::rust_helper_vfs_io_unsigned_offsets(file) } {
                    return negative(bindings::EINVAL);
                }
            }
        }
    }

    // SAFETY: The file remains live through the LSM permission hook.
    let error = unsafe { bindings::rust_helper_vfs_io_security_perm(file, mask) };
    if error != 0 {
        return error;
    }

    // SAFETY: The file and optional position remain live through the fsnotify
    // range permission hook.
    unsafe { bindings::rust_helper_vfs_io_fsnotify_area_perm(file, mask, ppos, count) }
}

/// Defines `vfs_read()` in Rust.
///
/// # Safety
///
/// `file` must be a live open file. `buf` and `count` must describe a user
/// buffer when the filesystem callback accesses it. `pos` must be live when
/// it is non-null.
#[export]
unsafe extern "C" fn vfs_read(
    file: *mut File,
    buf: *mut c_uchar,
    count: usize,
    pos: *mut Lofl,
) -> Ssize {
    // SAFETY: The caller supplies a live open file.
    let mode = unsafe { file_mode(file) };
    if mode & MODE_READ == 0 {
        return negative_ssize(bindings::EBADF);
    }
    if mode & MODE_CAN_READ == 0 {
        return negative_ssize(bindings::EINVAL);
    }

    // SAFETY: The helper only checks the user-address range.
    if !unsafe { bindings::rust_helper_vfs_io_access_ok(buf.cast::<c_void>(), count) } {
        return negative_ssize(bindings::EFAULT);
    }

    // SAFETY: The file and optional position satisfy the verification
    // contract.
    let error = unsafe { rw_verify_area(READ, file, pos, count) };
    if error != 0 {
        return error as Ssize;
    }

    let count = count.min(MAX_RW_COUNT);
    // SAFETY: The live file retains its operation table.
    let fops = unsafe { file_operations(file) };
    if fops.is_null() {
        // SAFETY: The task accounting helper only updates current-task state.
        unsafe { bindings::rust_helper_vfs_io_account_read(negative_ssize(bindings::EINVAL)) };
        return negative_ssize(bindings::EINVAL);
    }

    let (read, read_iter) = if fops.is_null() {
        (None, None)
    } else {
        // SAFETY: The live operation table supplies both callback pointers.
        unsafe { ((*fops).read, (*fops).read_iter) }
    };

    let result = if let Some(read) = read {
        // SAFETY: The legacy command callback receives the live file and user
        // buffer.
        unsafe { read(file, buf, count, pos) }
    } else if read_iter.is_some() {
        // SAFETY: The helper constructs the synchronous iovec and kiocb around
        // the live file and user buffer.
        unsafe { bindings::rust_helper_vfs_io_new_sync_read(file, buf, count, pos) }
    } else {
        negative_ssize(bindings::EINVAL)
    };

    if result > 0 {
        // SAFETY: The file remains live after a successful read.
        unsafe { bindings::rust_helper_vfs_io_fsnotify_access(file) };
    }
    // SAFETY: The helper updates only current-task accounting state.
    unsafe { bindings::rust_helper_vfs_io_account_read(result) };
    result
}

/// Defines `vfs_write()` in Rust.
///
/// # Safety
///
/// `file` must be a live open file. `buf` and `count` must describe a user
/// buffer when the filesystem callback accesses it. `pos` must be live when
/// it is non-null.
#[export]
unsafe extern "C" fn vfs_write(
    file: *mut File,
    buf: *const c_uchar,
    count: usize,
    pos: *mut Lofl,
) -> Ssize {
    // SAFETY: The caller supplies a live open file.
    let mode = unsafe { file_mode(file) };
    if mode & MODE_WRITE == 0 {
        return negative_ssize(bindings::EBADF);
    }
    if mode & MODE_CAN_WRITE == 0 {
        return negative_ssize(bindings::EINVAL);
    }

    // SAFETY: The helper only checks the user-address range.
    if !unsafe { bindings::rust_helper_vfs_io_access_ok(buf.cast::<c_void>(), count) } {
        return negative_ssize(bindings::EFAULT);
    }

    // SAFETY: The file and optional position satisfy the verification
    // contract.
    let error = unsafe {
        rw_verify_area(
            bindings::rust_vfs_io_access_RUST_VFS_IO_WRITE as c_int,
            file,
            pos,
            count,
        )
    };
    if error != 0 {
        return error as Ssize;
    }

    let count = count.min(MAX_RW_COUNT);
    // SAFETY: The live file retains its operation table.
    let fops = unsafe { file_operations(file) };
    let (write, write_iter) = if fops.is_null() {
        (None, None)
    } else {
        // SAFETY: The live operation table supplies both callback pointers.
        unsafe { ((*fops).write, (*fops).write_iter) }
    };

    // SAFETY: The helper starts the matching superblock write-freeze section.
    unsafe { bindings::rust_helper_vfs_io_start_write(file) };
    let result = if fops.is_null() {
        negative_ssize(bindings::EINVAL)
    } else if let Some(write) = write {
        // SAFETY: The legacy command callback receives the live file and user
        // buffer.
        unsafe { write(file, buf, count, pos) }
    } else if write_iter.is_some() {
        // SAFETY: The helper constructs the synchronous iovec and kiocb around
        // the live file and user buffer.
        unsafe { bindings::rust_helper_vfs_io_new_sync_write(file, buf, count, pos) }
    } else {
        negative_ssize(bindings::EINVAL)
    };

    if result > 0 {
        // SAFETY: The file remains live after a successful write.
        unsafe { bindings::rust_helper_vfs_io_fsnotify_modify(file) };
    }
    // SAFETY: The helper updates only current-task accounting state.
    unsafe { bindings::rust_helper_vfs_io_account_write(result) };
    // SAFETY: The helper ends the matching superblock write-freeze section.
    unsafe { bindings::rust_helper_vfs_io_end_write(file) };
    result
}
