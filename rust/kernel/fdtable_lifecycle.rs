// SPDX-License-Identifier: GPL-2.0

//! Native file descriptor table lifecycle.
//!
//! Rust owns fdtable capacity selection, expansion retry, descriptor
//! allocation, installation, close policy, files-structure copying, final
//! teardown, and RCU reference acquisition. C retains the shared layouts,
//! slab and bitmap primitives, locks, RCU publication, task access, file
//! references, and close callbacks.

use crate::{
    bindings,
    sync::barrier::{smp_mb, Read, Write},
};
#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};
use core::{
    ffi::{c_int, c_uint},
    hint::black_box,
    ptr,
};
use macros::export;

type Fd = bindings::fd;
type FdRange = bindings::fd_range;
type FdTable = bindings::fdtable;
type File = bindings::file;
type Files = bindings::files_struct;
type Task = bindings::task_struct;

const BITS_PER_LONG: c_uint = bindings::BITS_PER_LONG;
const NR_OPEN_DEFAULT: c_uint = BITS_PER_LONG;
const FDPUT_FPUT: usize = bindings::FDPUT_FPUT as usize;
const FAIL_ALLOC: u32 = 1;
const FAIL_FILES: u32 = 2;

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
static ALLOC_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
static COPY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
static CLOSE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
fn record_alloc() {
    ALLOC_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST))]
fn record_alloc() {}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
fn record_copy() {
    COPY_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST))]
fn record_copy() {}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
fn record_close() {
    CLOSE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST))]
fn record_close() {}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST))]
fn record_failure() {}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST))]
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

/// Preserves the original unconditional fdtable invariant failure.
///
/// # Safety
///
/// Callers must reach this function only after detecting an invariant
/// violation that the original implementation treated as fatal.
#[cold]
unsafe fn fdtable_bug() -> ! {
    // SAFETY: Callers reached an invariant condition that the original code
    // treated as fatal.
    unsafe { bindings::BUG() }
}

/// Preserves the original ``VFS_BUG_ON(1)`` debug-only invariant failure.
///
/// # Safety
///
/// Callers must reach this function only after detecting an invariant
/// violation that the original implementation treated as fatal.
#[inline]
unsafe fn fdtable_vfs_bug() {
    #[cfg(CONFIG_DEBUG_VFS)]
    // SAFETY: The condition was fatal in the original debug-only check.
    unsafe {
        bindings::BUG()
    };
}

/// Encodes a negative errno as an error pointer.
///
/// # Safety
///
/// `error` must fit in the kernel's error-pointer range.
unsafe fn error_ptr<T>(error: c_int) -> *mut T {
    // SAFETY: The helper encodes a small negative errno in pointer form.
    unsafe { bindings::ERR_PTR(error as isize).cast() }
}

/// Returns the current task's files structure.
///
/// # Safety
///
/// The caller must not use the pointer after the current task or its files
/// structure is replaced.
unsafe fn current_files() -> *mut Files {
    // SAFETY: The current task always has a files-structure pointer.
    unsafe { bindings::rust_helper_fdtable_current_files() }
}

/// Returns the current fdtable with the file-lock or RCU requirement checked.
///
/// # Safety
///
/// The caller must hold the files-structure lock or an RCU read-side section.
unsafe fn files_fdt(files: *mut Files) -> *mut FdTable {
    // SAFETY: The caller holds the file lock or an RCU read-side section.
    unsafe { bindings::rust_helper_fdtable_current(files) }
}

/// Returns the published fdtable without a lockdep check.
///
/// # Safety
///
/// `files` must contain a live, published fdtable for the duration of use.
unsafe fn raw_files_fdt(files: *mut Files) -> *mut FdTable {
    // SAFETY: The caller has a stable files-structure pointer.
    unsafe { bindings::rust_helper_fdtable_raw(files) }
}

/// Returns an fdtable's capacity.
///
/// # Safety
///
/// `fdt` must point to a live fdtable.
unsafe fn fdt_max(fdt: *mut FdTable) -> c_uint {
    // SAFETY: `fdt` points to a live fdtable.
    unsafe { bindings::rust_helper_fdtable_max_fds(fdt) }
}

/// Sets one bitmap bit.
///
/// # Safety
///
/// `bitmap` must contain at least `bit + 1` bits and permit modification.
unsafe fn set_bit(bitmap: *mut usize, bit: c_uint) {
    // SAFETY: The caller provides a bitmap large enough for `bit`.
    unsafe { bindings::rust_helper_fdtable_set_bit(bitmap, bit) };
}

/// Clears one bitmap bit.
///
/// # Safety
///
/// `bitmap` must contain at least `bit + 1` bits and permit modification.
unsafe fn clear_bit(bitmap: *mut usize, bit: c_uint) {
    // SAFETY: The caller provides a bitmap large enough for `bit`.
    unsafe { bindings::rust_helper_fdtable_clear_bit(bitmap, bit) };
}

/// Tests one bitmap bit.
///
/// # Safety
///
/// `bitmap` must contain at least `bit + 1` bits.
unsafe fn test_bit(bitmap: *const usize, bit: c_uint) -> bool {
    // SAFETY: The caller provides a bitmap large enough for `bit`.
    unsafe { bindings::rust_helper_fdtable_get_bit(bitmap, bit) != 0 }
}

/// Finds the next zero bit.
///
/// # Safety
///
/// `bitmap` must contain at least `size` bits.
unsafe fn find_next_zero(bitmap: *const usize, size: c_uint, offset: c_uint) -> c_uint {
    // SAFETY: The caller provides a bitmap of `size` bits.
    unsafe { bindings::rust_helper_fdtable_find_next_zero(bitmap, size, offset) }
}

/// Finds the last set bit.
///
/// # Safety
///
/// `bitmap` must contain at least `size` bits.
unsafe fn find_last_set_bit(bitmap: *const usize, size: c_uint) -> c_uint {
    // SAFETY: The caller provides a bitmap of `size` bits.
    unsafe { bindings::rust_helper_fdtable_find_last_bit(bitmap, size) }
}

/// Updates one close-on-exec bit.
///
/// # Safety
///
/// `fdt` must be live and `fd` must be inside its bitmap capacity.
unsafe fn set_close_on_exec(fd: c_uint, fdt: *mut FdTable, set: bool) {
    // SAFETY: The caller guarantees that `fdt` is live.
    let bitmap = unsafe { (*fdt).close_on_exec };
    if set {
        // SAFETY: The fd is inside the table selected by the caller.
        unsafe { set_bit(bitmap, fd) };
    } else {
        // SAFETY: The fd is inside the table selected by the caller.
        if unsafe { test_bit(bitmap, fd) } {
            // SAFETY: The fd is inside the table selected by the caller.
            unsafe { clear_bit(bitmap, fd) };
        }
    }
}

/// Marks one descriptor open and updates its close-on-exec and full-word bits.
///
/// # Safety
///
/// `fdt` must be live and `fd` must be inside its table capacity.
unsafe fn set_open_fd(fd: c_uint, fdt: *mut FdTable, set: bool) {
    // SAFETY: The caller guarantees that `fdt` is live.
    let open_fds = unsafe { (*fdt).open_fds };
    // SAFETY: The fd is inside the table selected by the caller.
    unsafe { set_bit(open_fds, fd) };
    // SAFETY: The fd is inside the table selected by the caller.
    unsafe { set_close_on_exec(fd, fdt, set) };

    let word = fd / BITS_PER_LONG;
    // SAFETY: The fd is inside the table selected by the caller.
    let value = unsafe { bindings::rust_helper_fdtable_open_word(fdt, word) };
    if value != !0 {
        // SAFETY: `word` indexes the full descriptor bitmap.
        unsafe { set_bit((*fdt).full_fds_bits, word) };
    }
}

/// Marks one descriptor closed and clears its full-word hint when necessary.
///
/// # Safety
///
/// `fdt` must be live and `fd` must be inside its table capacity.
unsafe fn clear_open_fd(fd: c_uint, fdt: *mut FdTable) {
    // SAFETY: The fd is inside the table selected by the caller.
    unsafe { clear_bit((*fdt).open_fds, fd) };
    let word = fd / BITS_PER_LONG;
    // SAFETY: `word` indexes the full descriptor bitmap.
    if unsafe { test_bit((*fdt).full_fds_bits, word) } {
        // SAFETY: `word` indexes the full descriptor bitmap.
        unsafe { clear_bit((*fdt).full_fds_bits, word) };
    }
}

/// Selects the smallest aligned table that contains the copied descriptors.
///
/// # Safety
///
/// `fdt` must be live. `punch_hole` may be null or must point to a live range.
unsafe fn sane_fdtable_size(fdt: *mut FdTable, punch_hole: *mut FdRange) -> c_uint {
    // SAFETY: The caller holds the old files-structure lock.
    let max_fds = unsafe { fdt_max(fdt) };
    // SAFETY: The bitmap contains `max_fds` bits.
    let mut last = unsafe { find_last_set_bit((*fdt).open_fds, max_fds) };

    if last == max_fds {
        return NR_OPEN_DEFAULT;
    }
    if !punch_hole.is_null() {
        // SAFETY: The caller supplied a live fd range.
        let hole = unsafe { &*punch_hole };
        if hole.to >= last && hole.from <= last {
            // SAFETY: The search is limited to the beginning of the same bitmap.
            last = unsafe { find_last_set_bit((*fdt).open_fds, hole.from) };
            if last == hole.from {
                return NR_OPEN_DEFAULT;
            }
        }
    }

    (last + 1 + BITS_PER_LONG - 1) & !(BITS_PER_LONG - 1)
}

fn allocation_slots(slots_wanted: c_uint) -> u64 {
    #[cfg(CONFIG_32BIT)]
    if slots_wanted < 256 {
        return 256;
    }

    u64::from(slots_wanted).next_power_of_two()
}

/// Allocates a dynamic fdtable with the original capacity policy.
///
/// # Safety
///
/// The caller must be prepared to free a returned table with the fdtable
/// release primitive.
unsafe fn alloc_fdtable(slots_wanted: c_uint) -> Result<*mut FdTable, c_int> {
    record_alloc();

    let mut nr = allocation_slots(slots_wanted);
    // SAFETY: The helper performs the original sysctl read.
    let nr_open = unsafe { bindings::rust_helper_fdtable_nr_open() };
    if nr > u64::from(nr_open) {
        nr = u64::from(nr_open & !(BITS_PER_LONG - 1));
        if nr < u64::from(slots_wanted) {
            return Err(-(bindings::EMFILE as c_int));
        }
    }

    if nr > i32::MAX as u64 / core::mem::size_of::<*mut File>() as u64 {
        return Err(-(bindings::EMFILE as c_int));
    }

    if injected_failure(FAIL_ALLOC) {
        return Err(-(bindings::ENOMEM as c_int));
    }

    // SAFETY: `nr` is bounded by the original allocation-size checks.
    let fdt = unsafe { bindings::rust_helper_fdtable_alloc(nr as c_uint) };
    if fdt.is_null() {
        Err(-(bindings::ENOMEM as c_int))
    } else {
        Ok(fdt)
    }
}

/// Expands the current table and preserves the caller's lock state.
///
/// # Safety
///
/// `files` must be live and its file lock must be held.
unsafe fn expand_fdtable(files: *mut Files, nr: c_uint) -> c_int {
    // SAFETY: The caller holds the files-structure lock.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
    // SAFETY: This function owns the unlock, allocation, and relock sequence.
    let allocation = unsafe { alloc_fdtable(nr.wrapping_add(1)) };

    // SAFETY: The lock is not held during allocation.
    if unsafe { bindings::rust_helper_fdtable_count(files) } > 1 {
        // SAFETY: Resizing the published table requires a grace period.
        unsafe { bindings::rust_helper_fdtable_synchronize_rcu() };
    }

    // SAFETY: The function must return with the original lock state.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    let new_fdt = match allocation {
        Ok(fdt) => fdt,
        Err(error) => return error,
    };

    // SAFETY: The lock is held and the current table is stable.
    let current_fdt = unsafe { files_fdt(files) };
    // SAFETY: The requested descriptor is outside the current capacity.
    if nr < unsafe { fdt_max(current_fdt) } {
        // SAFETY: This preserves the original invariant failure.
        unsafe { fdtable_bug() };
    }

    record_copy();
    // SAFETY: Both tables are live and the new table is larger.
    unsafe { bindings::rust_helper_fdtable_copy(new_fdt, current_fdt) };
    // SAFETY: The file lock serializes publication.
    unsafe { bindings::rust_helper_fdtable_publish(files, new_fdt) };
    // SAFETY: The caller holds the lock and owns the old dynamic table.
    if !unsafe { bindings::rust_helper_fdtable_is_embedded(files, current_fdt) } {
        // SAFETY: Readers can still access the old table until the grace period.
        unsafe { bindings::rust_helper_fdtable_free_rcu(current_fdt) };
    }
    // This pairs with the read barrier in `fd_install()`.
    smp_mb(Write);
    0
}

/// Expands a files structure if needed and preserves the caller's lock state.
///
/// # Safety
///
/// `files` must be live and its file lock must be held.
unsafe fn expand_files(files: *mut Files, nr: c_uint) -> c_int {
    loop {
        // SAFETY: The caller holds the files-structure lock.
        let fdt = unsafe { files_fdt(files) };
        // SAFETY: `fdt` is the currently published table.
        if nr < unsafe { fdt_max(fdt) } {
            return 0;
        }

        // SAFETY: The file lock serializes the resize flag.
        if unsafe { bindings::rust_helper_fdtable_resizing(files) } {
            // SAFETY: Release the lock before waiting for another resize.
            unsafe { bindings::rust_helper_fdtable_unlock(files) };
            // SAFETY: The wait condition has the original resize contract.
            unsafe { bindings::rust_helper_fdtable_wait_resize(files) };
            // SAFETY: The function must return with the lock held.
            unsafe { bindings::rust_helper_fdtable_lock(files) };
            continue;
        }

        // SAFETY: The helper reads the original sysctl limit.
        if nr >= unsafe { bindings::rust_helper_fdtable_nr_open() } {
            return -(bindings::EMFILE as c_int);
        }

        // SAFETY: This task owns the resize critical section.
        unsafe { bindings::rust_helper_fdtable_set_resizing(files, true) };
        // SAFETY: The caller passes its current lock state to resize.
        let error = unsafe { expand_fdtable(files, nr) };
        // SAFETY: This task owns the resize critical section.
        unsafe { bindings::rust_helper_fdtable_set_resizing(files, false) };
        // SAFETY: Publish completion to tasks waiting for the resize.
        unsafe { bindings::rust_helper_fdtable_wake_resize(files) };
        return error;
    }
}

/// Finds the next free descriptor.
///
/// # Safety
///
/// `fdt` must be live and its file lock must be held.
unsafe fn find_next_fd(fdt: *mut FdTable, start: c_uint) -> c_uint {
    // SAFETY: The table is at least one bitmap word long.
    let max_fds = unsafe { fdt_max(fdt) };
    let max_words = max_fds / BITS_PER_LONG;
    let word_index = start / BITS_PER_LONG;
    let word_end = (word_index + 1) * BITS_PER_LONG;

    // SAFETY: The first search is limited to the selected bitmap word.
    // SAFETY: The search stays inside the selected bitmap word.
    let bit = unsafe { find_next_zero((*fdt).open_fds, word_end, start) };
    if bit < word_end {
        return bit;
    }

    // SAFETY: The second-level bitmap contains one bit per bitmap word.
    let bit =
        BITS_PER_LONG * unsafe { find_next_zero((*fdt).full_fds_bits, max_words, word_index + 1) };
    if bit >= max_fds {
        return max_fds;
    }

    // SAFETY: The final search is bounded by the descriptor capacity.
    unsafe { find_next_zero((*fdt).open_fds, max_fds, bit) }
}

/// Reserves a descriptor from an explicit range in the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
unsafe fn alloc_fd(start: c_uint, end: c_uint, flags: c_uint) -> c_int {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The file lock protects all descriptor state below.
    unsafe { bindings::rust_helper_fdtable_lock(files) };

    let result = loop {
        // SAFETY: The file lock is held.
        let fdt = unsafe { files_fdt(files) };
        // SAFETY: The file lock protects the cached next descriptor.
        let mut fd = start.max(unsafe { bindings::rust_helper_fdtable_next_fd(files) });

        // SAFETY: The fdtable capacity is stable under the lock.
        if fd < unsafe { fdt_max(fdt) } {
            // SAFETY: The table is stable under the lock.
            fd = unsafe { find_next_fd(fdt, fd) };
        }

        if fd >= end {
            break -(bindings::EMFILE as c_int);
        }

        // SAFETY: The fdtable capacity is stable under the lock.
        if fd >= unsafe { fdt_max(fdt) } {
            // SAFETY: The caller passes its current lock state to expansion.
            let error = unsafe { expand_files(files, fd) };
            if error < 0 {
                break error;
            }
            continue;
        }

        // SAFETY: The file lock protects the cached next descriptor.
        if start <= unsafe { bindings::rust_helper_fdtable_next_fd(files) } {
            // SAFETY: The file lock protects the cached next descriptor.
            unsafe { bindings::rust_helper_fdtable_set_next_fd(files, fd + 1) };
        }

        let close_on_exec = flags & bindings::O_CLOEXEC != 0;
        // SAFETY: The selected fd is inside the current table.
        unsafe { set_open_fd(fd, fdt, close_on_exec) };
        // SAFETY: A reserved fd must still have an empty file entry.
        if !unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) }.is_null() {
            // SAFETY: This preserves the original invariant failure.
            unsafe { fdtable_vfs_bug() };
        }
        break fd as c_int;
    };

    // SAFETY: The file lock was retained through every successful path.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
    result
}

/// Allocates a descriptor without consulting the current task limit.
///
/// # Safety
///
/// The caller must use the returned descriptor with the current files table.
#[export]
unsafe extern "C" fn __get_unused_fd_flags(flags: c_uint, nofile: usize) -> c_int {
    // SAFETY: The caller supplies the original descriptor limit.
    unsafe { alloc_fd(0, nofile as c_uint, flags) }
}

/// Allocates a descriptor below the current task's open-file limit.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn get_unused_fd_flags(flags: c_uint) -> c_int {
    // SAFETY: The helper reads the current task limit.
    let nofile = unsafe { bindings::rust_helper_fdtable_nofile() };
    // SAFETY: The helper returns the original resource-limit value.
    unsafe { alloc_fd(0, nofile as c_uint, flags) }
}

/// Releases a reserved descriptor while the file lock is held.
///
/// # Safety
///
/// `files` must be live and its file lock must be held.
unsafe fn put_unused_fd_for(files: *mut Files, fd: c_uint) {
    // SAFETY: The file lock is held.
    let fdt = unsafe { files_fdt(files) };
    // SAFETY: The file lock is held and the fd belongs to this table.
    unsafe { clear_open_fd(fd, fdt) };
    // SAFETY: The file lock protects the cached next descriptor.
    if fd < unsafe { bindings::rust_helper_fdtable_next_fd(files) } {
        // SAFETY: The file lock protects the cached next descriptor.
        unsafe { bindings::rust_helper_fdtable_set_next_fd(files, fd) };
    }
}

/// Releases a reserved descriptor that was not installed.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn put_unused_fd(fd: c_uint) {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The file lock protects descriptor reservation state.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    // SAFETY: The file lock remains held through this update.
    unsafe { put_unused_fd_for(files, fd) };
    // SAFETY: Release the lock acquired above.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
}

/// Installs a file while handling an active resize.
///
/// # Safety
///
/// `fd` must be reserved in the current task and `file` must own one reference.
unsafe fn fd_install_slowpath(fd: c_uint, file: *mut File) {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The lock prevents a concurrent resize from copying stale data.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    // SAFETY: The file lock is held.
    let fdt = unsafe { files_fdt(files) };
    // SAFETY: The fd is reserved and cannot already have a file.
    if !unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) }.is_null() {
        // SAFETY: This preserves the original invariant failure.
        unsafe { fdtable_vfs_bug() };
    }
    // SAFETY: The lock publishes the complete table while holding the lock.
    unsafe { bindings::rust_helper_fdtable_set_fd(fdt, fd, file) };
    // SAFETY: Release the lock acquired above.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
}

/// Installs a file pointer into a reserved descriptor.
///
/// # Safety
///
/// `file` must own one reference and `fd` must be reserved in the current task.
#[export]
unsafe extern "C" fn fd_install(fd: c_uint, file: *mut File) {
    // SAFETY: The helper reads the immutable-or-stable backing mode.
    if unsafe { bindings::rust_helper_fdtable_file_backing(file) } {
        // SAFETY: The helper preserves the original one-time diagnostic.
        unsafe { bindings::rust_helper_fdtable_warn_backing() };
        return;
    }

    // SAFETY: Scheduler RCU protection prevents a concurrent resize from
    // replacing and freeing the current table before publication.
    unsafe { bindings::rust_helper_fdtable_read_lock_sched() };
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The scheduler RCU section protects the resize flag.
    if unsafe { bindings::rust_helper_fdtable_resizing(files) } {
        // SAFETY: The slow path acquires the file lock after leaving RCU.
        unsafe { bindings::rust_helper_fdtable_read_unlock_sched() };
        // SAFETY: The fd is reserved and the file reference is transferred.
        unsafe { fd_install_slowpath(fd, file) };
        return;
    }

    // This pairs with the write barrier in `expand_fdtable()`.
    smp_mb(Read);
    // SAFETY: The scheduler RCU section is active.
    let fdt = unsafe { raw_files_fdt(files) };
    // SAFETY: The fd is reserved and cannot already have a file.
    if !unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) }.is_null() {
        // SAFETY: This preserves the original invariant failure.
        unsafe { fdtable_vfs_bug() };
    }
    // SAFETY: Publication is protected by the scheduler RCU section.
    unsafe { bindings::rust_helper_fdtable_set_fd(fdt, fd, file) };
    // SAFETY: End the scheduler RCU section.
    unsafe { bindings::rust_helper_fdtable_read_unlock_sched() };
}

/// Removes and returns the file stored in a descriptor while the caller holds
/// the files-structure lock.
///
/// # Safety
///
/// `files` must be live and its `file_lock` must be held.
#[export]
unsafe extern "C" fn file_close_fd_locked(files: *mut Files, fd: c_uint) -> *mut File {
    // SAFETY: The caller supplies the locking contract.
    unsafe { bindings::rust_helper_fdtable_assert_locked(files) };
    // SAFETY: The caller holds the file lock.
    let fdt = unsafe { files_fdt(files) };
    // SAFETY: The table is stable under the caller's lock.
    if fd >= unsafe { fdt_max(fdt) } {
        return ptr::null_mut();
    }

    // SAFETY: The descriptor is inside the current table.
    let file = unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) };
    if !file.is_null() {
        // SAFETY: The caller holds the file lock.
        unsafe { bindings::rust_helper_fdtable_clear_fd(fdt, fd) };
        // SAFETY: The caller holds the file lock.
        unsafe { put_unused_fd_for(files, fd) };
    }
    file
}

/// Closes a descriptor in the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn close_fd(fd: c_uint) -> c_int {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The file lock protects descriptor removal.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    // SAFETY: The file lock remains held through removal.
    let file = unsafe { file_close_fd_locked(files, fd) };
    // SAFETY: Release the lock before close callbacks.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
    if file.is_null() {
        return -(bindings::EBADF as c_int);
    }

    record_close();
    // SAFETY: The caller owns one reference removed from the table.
    unsafe { bindings::rust_helper_fdtable_close_file(file, files) }
}

/// Removes a descriptor from the current task without running close callbacks.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn file_close_fd(fd: c_uint) -> *mut File {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The file lock protects descriptor removal.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    // SAFETY: The file lock remains held through removal.
    let file = unsafe { file_close_fd_locked(files, fd) };
    // SAFETY: Release the lock acquired above.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
    file
}

/// Closes every descriptor owned by a final files-structure reference.
///
/// # Safety
///
/// `files` must contain the final reference and no other task may use it.
unsafe fn close_files(files: *mut Files) -> *mut FdTable {
    // SAFETY: This is the last files-structure reference.
    let fdt = unsafe { raw_files_fdt(files) };
    // SAFETY: The last reference makes the table immutable.
    let max_fds = unsafe { fdt_max(fdt) };
    let max_words = max_fds / BITS_PER_LONG;

    for word in 0..max_words {
        // SAFETY: `word` is inside the open descriptor bitmap.
        let mut set = unsafe { bindings::rust_helper_fdtable_open_word(fdt, word) };
        let mut fd = word * BITS_PER_LONG;
        while set != 0 {
            if set & 1 != 0 {
                // SAFETY: The last files-structure reference keeps the table live.
                let file = unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) };
                if !file.is_null() {
                    record_close();
                    // SAFETY: The last reference transfers the table entry.
                    unsafe { bindings::rust_helper_fdtable_close_file(file, files) };
                    // SAFETY: Closing files may sleep.
                    unsafe { bindings::rust_helper_fdtable_resched() };
                }
            }
            fd += 1;
            set >>= 1;
        }
    }
    fdt
}

/// Releases one reference to a files structure and closes its descriptor table
/// after the final reference.
///
/// # Safety
///
/// The caller transfers one live `files_struct` reference.
#[export]
unsafe extern "C" fn put_files_struct(files: *mut Files) {
    // SAFETY: The caller transfers a live reference.
    if !unsafe { bindings::rust_helper_fdtable_put_count(files) } {
        return;
    }

    // SAFETY: The count reached zero, so this reference is final.
    let fdt = unsafe { close_files(files) };
    // SAFETY: The final reference owns any dynamic descriptor table.
    if !unsafe { bindings::rust_helper_fdtable_is_embedded(files, fdt) } {
        // SAFETY: No RCU reader can reach the final files structure.
        unsafe { bindings::rust_helper_fdtable_free(fdt) };
    }
    // SAFETY: All dependencies and descriptor tables are released.
    unsafe { bindings::rust_helper_fdtable_free_files(files) };
}

/// Detaches the current descriptor table from a task and drops its reference.
///
/// # Safety
///
/// `task` must remain live for the duration of the call.
#[export]
unsafe extern "C" fn exit_files(task: *mut Task) {
    // SAFETY: The helper performs the original task-lock protected detach.
    let files = unsafe { bindings::rust_helper_fdtable_detach_task_files(task) };
    if !files.is_null() {
        // SAFETY: The detach transferred the task's reference.
        unsafe { put_files_struct(files) };
    }
}

/// Clones a files structure and copies the selected descriptor state.
///
/// # Safety
///
/// `old_files` must be live. `punch_hole` may be null or point to a live range.
#[export]
unsafe extern "C" fn dup_fd(old_files: *mut Files, punch_hole: *mut FdRange) -> *mut Files {
    if injected_failure(FAIL_FILES) {
        // SAFETY: The helper creates the original error pointer.
        return unsafe { error_ptr(-(bindings::ENOMEM as c_int)) };
    }

    // SAFETY: The files cache is initialized before this lifecycle runs.
    let new_files = unsafe { bindings::rust_helper_fdtable_alloc_files() };
    if new_files.is_null() {
        // SAFETY: The helper creates the original error pointer.
        return unsafe { error_ptr(-(bindings::ENOMEM as c_int)) };
    }
    // SAFETY: The new object is uninitialized and owned by this function.
    unsafe { bindings::rust_helper_fdtable_init_files(new_files) };
    // SAFETY: The helper initialized the embedded table, which is not yet
    // published through `files->fdt`.
    let mut new_fdt = unsafe { ptr::addr_of_mut!((*new_files).fdtab) };

    // SAFETY: The old files structure is live.
    unsafe { bindings::rust_helper_fdtable_lock(old_files) };
    // SAFETY: The old file lock is held.
    let mut old_fdt = unsafe { files_fdt(old_files) };
    // SAFETY: The old table is stable under the lock.
    let mut open_files = unsafe { sane_fdtable_size(old_fdt, punch_hole) };

    // SAFETY: `new_fdt` is the private embedded or dynamic destination table.
    while open_files > unsafe { fdt_max(new_fdt) } {
        // SAFETY: Release the old lock during allocation.
        unsafe { bindings::rust_helper_fdtable_unlock(old_files) };

        // SAFETY: A previously allocated table is not published yet.
        if !unsafe { bindings::rust_helper_fdtable_is_embedded(new_files, new_fdt) } {
            // SAFETY: The new files structure has no other owner.
            unsafe { bindings::rust_helper_fdtable_free(new_fdt) };
        }

        // SAFETY: The private destination table is being replaced.
        let allocation = unsafe { alloc_fdtable(open_files) };
        new_fdt = match allocation {
            Ok(fdt) => fdt,
            Err(error) => {
                // SAFETY: The new files structure has no other owner.
                unsafe { bindings::rust_helper_fdtable_free_files(new_files) };
                // SAFETY: The helper creates the original error pointer.
                return unsafe { error_ptr(error) };
            }
        };

        // SAFETY: Reload the old table after another task may have resized it.
        unsafe { bindings::rust_helper_fdtable_lock(old_files) };
        // SAFETY: Reacquisition restores the caller's lock contract.
        old_fdt = unsafe { files_fdt(old_files) };
        // SAFETY: The latest old table is stable under the lock.
        open_files = unsafe { sane_fdtable_size(old_fdt, punch_hole) };
    }

    // SAFETY: Both tables are live and the destination is large enough.
    unsafe {
        bindings::rust_helper_fdtable_copy_bitmaps(new_fdt, old_fdt, open_files / BITS_PER_LONG)
    };

    let mut index = open_files;
    while index != 0 {
        let fd = open_files - index;
        // SAFETY: `fd` is inside the copied descriptor range.
        let file = unsafe { bindings::rust_helper_fdtable_get_fd_raw(old_fdt, fd) };
        if file.is_null() {
            // SAFETY: The new table mirrors the source bitmap.
            unsafe { clear_open_fd(fd, new_fdt) };
        } else {
            // SAFETY: The clone owns the additional file reference.
            unsafe { bindings::rust_helper_fdtable_get_file(file) };
        }
        // SAFETY: The destination contains the same descriptor range.
        unsafe { bindings::rust_helper_fdtable_set_fd(new_fdt, fd, file) };
        index -= 1;
    }

    // SAFETY: All source state has been copied.
    unsafe { bindings::rust_helper_fdtable_unlock(old_files) };
    // SAFETY: Clear unused descriptor entries in the destination.
    unsafe {
        bindings::rust_helper_fdtable_clear_entries(
            new_fdt,
            open_files,
            bindings::rust_helper_fdtable_max_fds(new_fdt),
        )
    };
    // SAFETY: Publish the completed private table into the new files structure.
    unsafe { bindings::rust_helper_fdtable_publish(new_files, new_fdt) };
    new_files
}

const EAGAIN: c_int = -(bindings::EAGAIN as c_int);

/// Acquires and validates one RCU-protected file reference.
///
/// # Safety
///
/// `entry` must remain readable under RCU until this function returns.
unsafe fn get_file_rcu_internal(entry: *mut *mut File) -> Result<*mut File, c_int> {
    // SAFETY: The caller supplies an RCU-protected file pointer.
    let file = unsafe { bindings::rust_helper_fdtable_entry_raw(entry) };
    if file.is_null() {
        return Ok(ptr::null_mut());
    }

    // SAFETY: The file object is protected by SLAB_TYPESAFE_BY_RCU.
    if !unsafe { bindings::rust_helper_fdtable_file_ref_get(file) } {
        return Err(EAGAIN);
    }

    // SAFETY: The caller keeps the pointer entry live through this read.
    let reloaded = black_box(unsafe { bindings::rust_helper_fdtable_entry_raw(entry) });
    if file == reloaded {
        Ok(reloaded)
    } else {
        // SAFETY: The acquired reference belongs to this retry path.
        unsafe { bindings::fput(file) };
        Err(EAGAIN)
    }
}

/// Acquires a file reference through a stable RCU pointer.
///
/// # Safety
///
/// `entry` must remain in an RCU-readable structure for the duration of the
/// call.
#[export]
unsafe extern "C" fn get_file_rcu(entry: *mut *mut File) -> *mut File {
    loop {
        // SAFETY: The caller supplies the original RCU pointer contract.
        match unsafe { get_file_rcu_internal(entry) } {
            Ok(file) => return file,
            Err(_) => continue,
        }
    }
}

/// Acquires a file reference while the caller holds a live object.
///
/// # Safety
///
/// The object containing `entry` must stay live through the call.
#[export]
unsafe extern "C" fn get_file_active(entry: *mut *mut File) -> *mut File {
    // SAFETY: The caller guarantees the containing object is live.
    unsafe { bindings::rust_helper_fdtable_read_lock() };
    // SAFETY: The caller supplies the original RCU pointer contract.
    let file = unsafe { get_file_rcu_internal(entry) };
    // SAFETY: End the RCU read-side section.
    unsafe { bindings::rust_helper_fdtable_read_unlock() };
    match file {
        Ok(file) => file,
        Err(_) => ptr::null_mut(),
    }
}

/// Looks up and validates a descriptor while an RCU section is active.
///
/// # Safety
///
/// `files` must remain live and the caller must hold an RCU read-side section.
unsafe fn fget_files_rcu(files: *mut Files, fd: c_uint, mask: u32) -> *mut File {
    loop {
        // SAFETY: The caller holds an RCU read-side section.
        let fdt = unsafe { raw_files_fdt(files) };
        // SAFETY: The fdt pointer is stable for this iteration.
        let entry = unsafe { bindings::rust_helper_fdtable_fdentry(fdt, fd) };
        // SAFETY: Invalid descriptors are masked to a null result.
        let file = unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) };
        if file.is_null() {
            return ptr::null_mut();
        }

        // SAFETY: The file object is protected by SLAB_TYPESAFE_BY_RCU.
        if !unsafe { bindings::rust_helper_fdtable_file_ref_get(file) } {
            continue;
        }

        // SAFETY: The same RCU section keeps the entry live.
        let reloaded = black_box(unsafe { bindings::rust_helper_fdtable_entry_raw(entry) });
        // SAFETY: The same RCU section keeps the files table pointer live.
        let current_fdt = unsafe { raw_files_fdt(files) };
        if file != reloaded || fdt != current_fdt {
            // SAFETY: The acquired reference belongs to this retry path.
            unsafe { bindings::fput(file) };
            continue;
        }

        // SAFETY: The acquired reference keeps the file live.
        if unsafe { bindings::rust_helper_fdtable_file_mode(file) } & mask != 0 {
            // SAFETY: The acquired reference belongs to this rejection path.
            unsafe { bindings::fput(file) };
            return ptr::null_mut();
        }
        return file;
    }
}

/// Looks up a descriptor and acquires a file reference.
///
/// # Safety
///
/// `files` must remain live for the duration of the call.
unsafe fn fget_files(files: *mut Files, fd: c_uint, mask: u32) -> *mut File {
    // SAFETY: Begin the RCU section that protects the descriptor table.
    unsafe { bindings::rust_helper_fdtable_read_lock() };
    // SAFETY: The RCU section remains active for the lookup.
    let file = unsafe { fget_files_rcu(files, fd, mask) };
    // SAFETY: End the RCU read-side section.
    unsafe { bindings::rust_helper_fdtable_read_unlock() };
    file
}

/// Acquires a reference to a non-path descriptor in the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn fget(fd: c_uint) -> *mut File {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The path mask is the original `fget()` policy.
    unsafe { fget_files(files, fd, bindings::rust_helper_fdtable_fmode_path()) }
}

/// Acquires a reference to any live descriptor in the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn fget_raw(fd: c_uint) -> *mut File {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: No mode mask is applied by the raw lookup.
    unsafe { fget_files(files, fd, 0) }
}

/// Acquires a descriptor reference from another live task.
///
/// # Safety
///
/// `task` must remain live for the call.
#[export]
unsafe extern "C" fn fget_task(task: *mut Task, fd: c_uint) -> *mut File {
    // SAFETY: The caller supplies a live task.
    unsafe { bindings::rust_helper_fdtable_task_lock(task) };
    // SAFETY: The task lock stabilizes the files-structure pointer.
    let files = unsafe { bindings::rust_helper_fdtable_task_files(task) };
    let file = if files.is_null() {
        ptr::null_mut()
    } else {
        // SAFETY: The files structure remains live under the task lock.
        unsafe { fget_files(files, fd, 0) }
    };
    // SAFETY: Release the task lock acquired above.
    unsafe { bindings::rust_helper_fdtable_task_unlock(task) };
    file
}

/// Acquires the next live descriptor from another task.
///
/// # Safety
///
/// `task` and `ret_fd` must remain live for the call.
#[export]
unsafe extern "C" fn fget_task_next(task: *mut Task, ret_fd: *mut c_uint) -> *mut File {
    // SAFETY: The caller supplies a live output pointer.
    let mut fd = unsafe { *ret_fd };
    // SAFETY: The caller supplies a live task.
    unsafe { bindings::rust_helper_fdtable_task_lock(task) };
    // SAFETY: The task lock stabilizes the files-structure pointer.
    let files = unsafe { bindings::rust_helper_fdtable_task_files(task) };
    let mut file = ptr::null_mut();

    if !files.is_null() {
        // SAFETY: The task lock keeps the files structure live.
        unsafe { bindings::rust_helper_fdtable_read_lock() };
        loop {
            // SAFETY: The RCU section protects each current table pointer.
            let fdt = unsafe { raw_files_fdt(files) };
            // SAFETY: The table pointer is valid for this iteration.
            if fd >= unsafe { fdt_max(fdt) } {
                break;
            }
            // SAFETY: The RCU section remains active during lookup.
            file = unsafe { fget_files_rcu(files, fd, 0) };
            if !file.is_null() {
                break;
            }
            fd += 1;
        }
        // SAFETY: End the RCU read-side section.
        unsafe { bindings::rust_helper_fdtable_read_unlock() };
    }
    // SAFETY: Release the task lock acquired above.
    unsafe { bindings::rust_helper_fdtable_task_unlock(task) };
    // SAFETY: The caller supplied a live output pointer.
    unsafe { *ret_fd = fd };
    file
}

/// Performs a lightweight lookup against an explicit files structure.
///
/// # Safety
///
/// `files` must remain live for the duration of the call.
unsafe fn fdget_light_files(files: *mut Files, fd: c_uint, mask: u32) -> Fd {
    // SAFETY: The acquire read pairs with the final files reference drop.
    if unsafe { bindings::rust_helper_fdtable_count_acquire(files) } == 1 {
        // SAFETY: An unshared files structure is stable in this task.
        let fdt = unsafe { raw_files_fdt(files) };
        // SAFETY: Invalid descriptors are masked to a null result.
        let file = unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) };
        // SAFETY: The unshared files structure keeps the file entry live.
        if file.is_null() || unsafe { bindings::rust_helper_fdtable_file_mode(file) } & mask != 0 {
            return Fd { word: 0 };
        }
        return Fd {
            word: file as usize,
        };
    }

    // SAFETY: Shared files structures require a real file reference.
    let file = unsafe { fget_files(files, fd, mask) };
    if file.is_null() {
        Fd { word: 0 }
    } else {
        Fd {
            word: file as usize | FDPUT_FPUT,
        }
    }
}

/// Performs the path-filtered lightweight descriptor lookup.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn fdget(fd: c_uint) -> Fd {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: The helper supplies the original path mode mask.
    unsafe { fdget_light_files(files, fd, bindings::rust_helper_fdtable_fmode_path()) }
}

/// Performs the raw lightweight descriptor lookup.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn fdget_raw(fd: c_uint) -> Fd {
    // SAFETY: The current task has a live files structure.
    let files = unsafe { current_files() };
    // SAFETY: No mode mask is applied by the raw lookup.
    unsafe { fdget_light_files(files, fd, 0) }
}

/// Releases a reserved descriptor while the caller holds the file lock.
///
/// # Safety
///
/// `files` must be live and its `file_lock` must be held.
#[export]
unsafe extern "C" fn rust_fdtable_put_unused_locked(files: *mut Files, fd: c_uint) {
    // SAFETY: The caller supplies the locking contract.
    unsafe { put_unused_fd_for(files, fd) };
}

/// Expands a descriptor table while preserving the caller's lock state.
///
/// # Safety
///
/// `files` must be live and its `file_lock` must be held.
#[export]
unsafe extern "C" fn rust_fdtable_expand_files(files: *mut Files, nr: c_uint) -> c_int {
    // SAFETY: The caller supplies the locking contract.
    unsafe { expand_files(files, nr) }
}

/// Allocates a descriptor from an explicit range for the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn rust_fdtable_alloc(start: c_uint, end: c_uint, flags: c_uint) -> c_int {
    // SAFETY: The current task owns the target descriptor table.
    unsafe { alloc_fd(start, end, flags) }
}

/// Looks up a descriptor while the caller holds an RCU read-side section.
///
/// # Safety
///
/// `files` must remain live and the caller must hold an RCU read-side section.
#[export]
unsafe extern "C" fn rust_fdtable_fget_rcu(files: *mut Files, fd: c_uint, mask: u32) -> *mut File {
    // SAFETY: The caller supplies the RCU protection.
    unsafe { fget_files_rcu(files, fd, mask) }
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_fdtable_test_alloc_count() -> u32 {
    ALLOC_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_fdtable_test_copy_count() -> u32 {
    COPY_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_fdtable_test_close_count() -> u32 {
    CLOSE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_fdtable_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_fdtable_test_fail_next(stage: u32) {
    NEXT_FAILURE.store(stage, Ordering::Release);
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
/// Looks up a descriptor in an explicitly supplied files structure.
///
/// # Safety
///
/// `files` must remain live through the call.
#[export]
unsafe extern "C" fn rust_fdtable_test_fget(files: *mut Files, fd: c_uint, mask: u32) -> *mut File {
    // SAFETY: The test supplies the live files structure.
    unsafe { fget_files(files, fd, mask) }
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
/// Allocates a descriptor from an explicit range in the current task.
///
/// # Safety
///
/// The current task must own a live files structure.
#[export]
unsafe extern "C" fn rust_fdtable_test_alloc_range(
    start: c_uint,
    end: c_uint,
    flags: c_uint,
) -> c_int {
    // SAFETY: The current task owns the target descriptor table.
    unsafe { alloc_fd(start, end, flags) }
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
/// Installs a file into a descriptor of an explicitly supplied files structure.
///
/// # Safety
///
/// `files` must remain live and `file` must own a reference transferred to the
/// descriptor table.
#[export]
unsafe extern "C" fn rust_fdtable_test_install(files: *mut Files, fd: c_uint, file: *mut File) {
    // SAFETY: The test supplies a live files structure.
    unsafe { bindings::rust_helper_fdtable_lock(files) };
    // SAFETY: The file lock is held.
    let fdt = unsafe { files_fdt(files) };
    // SAFETY: The caller selected a reserved descriptor.
    // SAFETY: The file lock keeps the selected descriptor table live.
    let invalid_fd = fd >= unsafe { fdt_max(fdt) };
    // SAFETY: The file lock keeps the selected descriptor table live.
    let installed = !unsafe { bindings::rust_helper_fdtable_get_fd_raw(fdt, fd) }.is_null();
    if invalid_fd || installed {
        // SAFETY: This preserves the original invariant failure.
        unsafe { fdtable_vfs_bug() };
    }
    // SAFETY: The test reserved this descriptor before the call.
    unsafe { set_open_fd(fd, fdt, false) };
    // SAFETY: The lock publishes the installed file reference.
    unsafe { bindings::rust_helper_fdtable_set_fd(fdt, fd, file) };
    // SAFETY: Release the lock acquired above.
    unsafe { bindings::rust_helper_fdtable_unlock(files) };
}

#[cfg(CONFIG_FDTABLE_LIFECYCLE_KUNIT_TEST)]
/// Performs a lightweight lookup against an explicitly supplied files
/// structure.
///
/// # Safety
///
/// `files` must remain live through the call.
#[export]
unsafe extern "C" fn rust_fdtable_test_fdget(files: *mut Files, fd: c_uint, mask: u32) -> Fd {
    // SAFETY: The test supplies the live files structure.
    unsafe { fdget_light_files(files, fd, mask) }
}
