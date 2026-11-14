// SPDX-License-Identifier: GPL-2.0

//! Native completion and rollback for aligned VMA unmapping.
//!
//! Rust owns the lock-downgrade decision, address-space accounting order,
//! detached-VMA removal loop, resource release, and rollback loop. C retains
//! VMA splitting, detached-tree insertion, page-table and reverse-map removal,
//! maple-tree primitives, and lock primitives.

use crate::bindings;
use core::ffi::c_int;
use core::mem::MaybeUninit;
use macros::export;

type AlignState = bindings::rust_vma_munmap_align_state;
type CompleteState = bindings::rust_vma_munmap_complete_state;
type MapleState = bindings::ma_state;

/// Completes one aligned unmap from an initialized snapshot.
///
/// # Safety
///
/// `state` must describe the active mmap lock, address space, accounting
/// values, and populated detached tree. This function consumes the tree.
unsafe fn complete(state: *mut CompleteState) {
    // SAFETY: The caller provides a complete snapshot with an active mmap lock
    // and a populated detached tree.
    let state = unsafe { &*state };
    let mm = state.mm;

    // SAFETY: The mmap lock protects both counters.
    unsafe {
        bindings::rust_helper_vma_munmap_adjust_counts(mm, state.vma_count, state.locked_vm);
    }

    if state.unlock {
        // SAFETY: The caller permits the documented lock downgrade.
        unsafe { bindings::rust_helper_vma_munmap_downgrade(mm) };
    }

    if state.nr_pages == 0 {
        return;
    }

    if state.clear_ptes {
        // SAFETY: The snapshot identifies the populated detached tree and the
        // original unmap range.
        unsafe { bindings::rust_helper_vma_munmap_clear_ptes(state) };
    }

    // SAFETY: The address space is live and the lock contract is active.
    unsafe {
        bindings::rust_helper_vma_munmap_update_hiwater(mm);
        bindings::rust_helper_vma_munmap_sub_total_vm(mm, state.nr_pages);
        bindings::rust_helper_vma_munmap_adjust_stats(
            mm,
            state.exec_vm,
            state.stack_vm,
            state.data_vm,
        );

        bindings::rust_helper_vma_munmap_mas_reset(state.mas_detach);
        loop {
            let vma = bindings::rust_helper_vma_munmap_mas_find(state.mas_detach);
            if vma.is_null() {
                break;
            }
            bindings::rust_helper_vma_munmap_remove_vma(vma);
        }

        bindings::rust_helper_vma_munmap_unaccount(state.nr_accounted);
        bindings::rust_helper_vma_munmap_validate(mm);
        if state.unlock {
            bindings::rust_helper_vma_munmap_read_unlock(mm);
        }
        bindings::rust_helper_vma_munmap_destroy_tree(state.mas_detach);
    }
}

/// Reattaches every VMA in a detached tree and destroys the tree.
///
/// # Safety
///
/// The caller must own `mas_detach` and all VMA references in its tree.
unsafe fn reattach(mas_detach: *mut MapleState) {
    // SAFETY: The caller owns the detached tree and its VMA references.
    unsafe {
        bindings::rust_helper_vma_munmap_mas_reset(mas_detach);
        loop {
            let vma = bindings::rust_helper_vma_munmap_mas_find(mas_detach);
            if vma.is_null() {
                break;
            }
            bindings::rust_helper_vma_munmap_mark_attached(vma);
        }
        bindings::rust_helper_vma_munmap_destroy_tree(mas_detach);
    }
}

/// Completes one aligned VMA unmap after the main tree update.
///
/// # Safety
///
/// `state` must describe the active mmap lock, address space, accounting
/// values, and populated detached tree. The caller transfers ownership of the
/// detached tree to this function.
#[export]
unsafe extern "C" fn rust_vma_munmap_complete(state: *mut CompleteState) {
    // SAFETY: The caller upholds the completion-state requirements.
    unsafe { complete(state) }
}

/// Reattaches every VMA in a detached tree and destroys the tree.
///
/// # Safety
///
/// The caller must own `mas_detach` and all VMA references in its tree.
#[export]
unsafe extern "C" fn rust_vma_munmap_reattach(mas_detach: *mut MapleState) {
    // SAFETY: The caller upholds the rollback requirements.
    unsafe { reattach(mas_detach) }
}

/// Runs the aligned unmap sequence for one address range.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize the opaque state
/// with a live address space and detached tree.
#[export]
unsafe extern "C" fn rust_do_vmi_align_munmap(state: *mut AlignState) -> c_int {
    // SAFETY: The caller initializes the opaque state for this operation.
    let error = unsafe { bindings::rust_helper_vma_munmap_align_gather(state) };
    if error != 0 {
        // SAFETY: The gather path restored the original VMA tree.
        unsafe { bindings::rust_helper_vma_munmap_align_validate(state) };
        return error;
    }

    let mut storage = MaybeUninit::<CompleteState>::uninit();
    let snapshot = storage.as_mut_ptr();
    // SAFETY: Gather populated every accounting field in the opaque state.
    unsafe { bindings::rust_helper_vma_munmap_align_snapshot(state, snapshot) };

    // SAFETY: The gather completed and the state describes the active range.
    let error = unsafe { bindings::rust_helper_vma_munmap_align_clear_tree(state) };
    if error != 0 {
        // SAFETY: The snapshot owns the still-populated detached tree.
        unsafe { reattach((*snapshot).mas_detach) };
        // SAFETY: Rollback restored the original VMA tree.
        unsafe { bindings::rust_helper_vma_munmap_align_validate(state) };
        return error;
    }

    // SAFETY: The main tree update succeeded and the snapshot owns the
    // detached tree.
    unsafe { complete(snapshot) };
    0
}
