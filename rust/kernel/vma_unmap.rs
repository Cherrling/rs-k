// SPDX-License-Identifier: GPL-2.0

//! Native address-space range unmapping.
//!
//! Rust owns range validation, page alignment, the first-overlap lookup,
//! empty-range unlock decisions, mmap write-lock handling, and the userfaultfd
//! completion order. C retains the detached-tree gather, partial split,
//! rollback, and completion primitives.

use crate::bindings;
use crate::page::PAGE_SIZE;
use core::ffi::c_int;
use core::mem::MaybeUninit;
use macros::export;

type Mm = bindings::mm_struct;
type State = bindings::rust_vma_unmap_state;
type VmaIterator = bindings::vma_iterator;
type ListHead = bindings::list_head;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn unmap_end(start: usize, len: usize, task_size: usize) -> Result<usize, c_int> {
    let page_mask = PAGE_SIZE - 1;

    if start & page_mask != 0 || start > task_size || len > task_size - start {
        return Err(-errno(bindings::EINVAL));
    }

    let len = len.wrapping_add(page_mask) & !page_mask;
    let end = start.wrapping_add(len);
    if end == start {
        return Err(-errno(bindings::EINVAL));
    }

    Ok(end)
}

/// Validates and aligns one unmap range for KUnit.
///
/// # Safety
///
/// `end` must point to writable storage for one `usize`.
#[cfg(CONFIG_VMA_UNMAP_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_vma_unmap_test_range(
    start: usize,
    len: usize,
    task_size: usize,
    end: *mut usize,
) -> c_int {
    match unmap_end(start, len, task_size) {
        Ok(value) => {
            // SAFETY: The caller provides a writable output pointer.
            unsafe { end.write(value) };
            0
        }
        Err(error) => error,
    }
}

/// Unmaps one aligned range using an existing iterator.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vmi` and `uf` must remain valid.
/// `unlock` must select the caller's lock-release contract.
unsafe fn do_vmi_munmap_range(
    vmi: *mut VmaIterator,
    mm: *mut Mm,
    start: usize,
    len: usize,
    uf: *mut ListHead,
    unlock: bool,
) -> c_int {
    // SAFETY: The caller provides the active address-space configuration.
    let task_size = unsafe { bindings::rust_helper_vma_unmap_task_size() };
    let end = match unmap_end(start, len, task_size) {
        Ok(end) => end,
        Err(error) => return error,
    };

    // SAFETY: The iterator and mmap write lock are active.
    let vma = unsafe { bindings::rust_helper_vma_unmap_find(vmi, end) };
    if vma.is_null() {
        if unlock {
            // SAFETY: The caller selected the successful empty-range contract.
            unsafe { bindings::rust_helper_vma_unmap_write_unlock(mm) };
        }
        return 0;
    }

    // SAFETY: The VMA overlaps the validated range and the mmap write lock is
    // held.
    unsafe { bindings::rust_helper_vma_unmap_align(vmi, vma, mm, start, end, uf, unlock) }
}

/// Unmaps one aligned range using an existing iterator.
///
/// # Safety
///
/// The caller must hold the mmap write lock. `vmi` and `uf` must remain valid.
/// `unlock` must select the caller's lock-release contract.
#[export]
unsafe extern "C" fn do_vmi_munmap(
    vmi: *mut VmaIterator,
    mm: *mut Mm,
    start: usize,
    len: usize,
    uf: *mut ListHead,
    unlock: bool,
) -> c_int {
    // SAFETY: The caller upholds the function requirements.
    unsafe { do_vmi_munmap_range(vmi, mm, start, len, uf, unlock) }
}

/// Unmaps a user range from the current address space.
///
/// # Safety
///
/// The current task must have a live address space. The call must occur in
/// task context and allow this function to acquire and release the mmap write
/// lock.
unsafe fn unmap_range(mm: *mut Mm, start: usize, len: usize, unlock: bool) -> c_int {
    let mut storage = MaybeUninit::<State>::uninit();
    let state = storage.as_mut_ptr();
    // SAFETY: The private state is initialized before use.
    unsafe { bindings::rust_helper_vma_unmap_state_init(state, mm, start) };

    // SAFETY: The address space is live and may be write-locked.
    if unsafe { bindings::rust_helper_vma_unmap_write_lock_killable(mm) } != 0 {
        return -errno(bindings::EINTR);
    }

    // SAFETY: The write lock is held and the private state is initialized.
    let ret = unsafe {
        do_vmi_munmap_range(
            &mut (*state).vmi,
            mm,
            start,
            len,
            &mut (*state).userfaultfd,
            unlock,
        )
    };

    if ret != 0 || !unlock {
        // SAFETY: The range operation left the write lock held.
        unsafe { bindings::rust_helper_vma_unmap_write_unlock(mm) };
    }

    // SAFETY: The userfaultfd list was initialized and the lock contract has
    // been completed.
    unsafe { bindings::rust_helper_vma_unmap_userfault_complete(mm, &mut (*state).userfaultfd) };
    ret
}

/// Unmaps a user range from the current address space.
///
/// # Safety
///
/// The current task must have a live address space. The call must occur in
/// task context and allow this function to acquire and release the mmap write
/// lock.
#[export]
unsafe extern "C" fn __vm_munmap(start: usize, len: usize, unlock: bool) -> c_int {
    // SAFETY: The current task provides the live address space.
    let mm = unsafe { bindings::rust_helper_vma_unmap_current_mm() };
    // SAFETY: The caller upholds the function requirements.
    unsafe { unmap_range(mm, start, len, unlock) }
}

/// Unmaps a range from an address space that is already write locked.
///
/// # Safety
///
/// The caller must hold the mmap write lock for `mm`. `mm` and `uf` must remain
/// valid for the entire call. `uf` may be null when the caller does not collect
/// userfaultfd events.
#[export]
unsafe extern "C" fn do_munmap(mm: *mut Mm, start: usize, len: usize, uf: *mut ListHead) -> c_int {
    let mut storage = MaybeUninit::<VmaIterator>::uninit();
    let vmi = storage.as_mut_ptr();
    // SAFETY: The private iterator storage is initialized before use.
    unsafe { bindings::rust_helper_vma_unmap_iterator_init(vmi, mm, start) };
    // SAFETY: The caller holds the write lock and upholds the range contract.
    unsafe { do_vmi_munmap_range(vmi, mm, start, len, uf, false) }
}

/// Unmaps a range from the current address space without releasing the lock
/// after a successful call.
///
/// # Safety
///
/// The current task must have a live address space. The call must occur in
/// task context and allow this function to acquire and release the mmap write
/// lock.
#[export]
unsafe extern "C" fn vm_munmap(start: usize, len: usize) -> c_int {
    // SAFETY: The caller upholds the function requirements.
    unsafe { __vm_munmap(start, len, false) }
}
