// SPDX-License-Identifier: GPL-2.0

//! Native VMA copy and page-table move orchestration for `mremap`.
//!
//! Rust owns the VMA-copy result handling, page-table move result, mremap
//! callback decision, rollback order, accounting correction, userfaultfd
//! preparation, and remap-state transitions. C retains page-table movement,
//! callback invocation, field access, accounting, userfaultfd, and hugetlb
//! repair primitives.

use crate::bindings;
use core::ffi::c_int;
use core::ptr;
use macros::export;

type State = bindings::rust_mremap_copy_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Copies one VMA and moves its page tables to the destination range.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The remap state, source VMA,
/// destination range, and output pointer in `state` must remain live until
/// this function returns.
unsafe fn copy_vma_and_data(state: *mut State) -> c_int {
    // SAFETY: The caller supplies the live remap state and output pointer.
    // Each helper performs one narrow access or one existing operation.
    unsafe {
        let new = bindings::rust_helper_mremap_copy_vma(state);
        if new.is_null() {
            bindings::rust_helper_mremap_copy_uncharge(state);
            bindings::rust_helper_mremap_copy_set_output(state, ptr::null_mut());
            return -errno(bindings::ENOMEM);
        }

        // A copied-range self-merge can replace the source VMA and invalidate
        // any iterator held by the caller.
        if bindings::rust_helper_mremap_copy_source_changed(state) {
            bindings::rust_helper_mremap_copy_mark_vmi_invalid(state);
        }

        bindings::rust_helper_mremap_copy_commit_source(state);
        bindings::rust_helper_mremap_copy_set_new_vma(state, new);

        let moved = bindings::rust_helper_mremap_copy_page_tables(state);
        let mut err = 0;

        if moved < bindings::rust_helper_mremap_copy_old_len(state) {
            err = -errno(bindings::ENOMEM);
        } else if bindings::rust_helper_mremap_copy_has_mremap(state) {
            err = bindings::rust_helper_mremap_copy_invoke_mremap(state);
        }

        if err != 0 {
            // Move every page-table entry that reached the destination back
            // before the caller unmaps the destination VMA.
            bindings::rust_helper_mremap_copy_prepare_revert(state, moved);
            bindings::rust_helper_mremap_copy_page_tables_back(state);

            if !bindings::rust_helper_mremap_copy_keeps_source(state) {
                bindings::rust_helper_mremap_copy_uncharge(state);
            }

            bindings::rust_helper_mremap_copy_set_error_destination(state);
        } else {
            bindings::rust_helper_mremap_copy_userfaultfd_prepare(state);
        }

        bindings::rust_helper_mremap_copy_fixup_hugetlb(state);
        bindings::rust_helper_mremap_copy_set_output(state, new);
        err
    }
}

/// Copies one VMA and moves its page tables to the destination range.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`copy_vma_and_data`].
#[export]
unsafe extern "C" fn rust_mremap_copy(state: *mut State) -> c_int {
    // SAFETY: The caller upholds the mremap copy requirements.
    unsafe { copy_vma_and_data(state) }
}
