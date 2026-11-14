// SPDX-License-Identifier: GPL-2.0

//! Native structural commit for VMA merge operations.
//!
//! Rust owns the selection of the VMA and iterator range, the preparation
//! object, the preallocation ordering, structural range updates, maple-tree
//! storage, and completion. C retains the individual setter and locking
//! primitives, maple-tree operations, and VMA completion mechanics.

use crate::bindings;
use core::mem::MaybeUninit;
use macros::export;

type MergeParams = bindings::vma_merge_struct;
type Prepare = bindings::vma_prepare;

/// Commits a prepared VMA merge.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation
/// whose preallocation contract permits a structural commit.
pub(crate) unsafe fn commit(vmg: *mut MergeParams) -> i32 {
    // SAFETY: The caller supplies a live merge operation. Each helper performs
    // one operation on that operation or on the Rust-owned preparation object.
    unsafe {
        let adjust_next_start = bindings::rust_helper_vma_merge_commit_adjust_next_start(vmg);
        let vma = if adjust_next_start {
            bindings::rust_helper_vma_merge_commit_middle(vmg)
        } else {
            bindings::rust_helper_vma_merge_commit_target(vmg)
        };

        if adjust_next_start {
            bindings::rust_helper_vma_merge_commit_config_next(vmg);
        } else {
            bindings::rust_helper_vma_merge_commit_config_range(vmg);
        }

        let mut vp = MaybeUninit::<Prepare>::uninit();
        bindings::rust_helper_vma_merge_commit_init_prepare(vp.as_mut_ptr(), vma, vmg);

        let err = bindings::rust_helper_vma_merge_commit_prealloc(vmg, vma);
        if err != 0 {
            return err;
        }

        bindings::rust_helper_vma_merge_commit_prepare(vp.as_mut_ptr());
        bindings::rust_helper_vma_merge_commit_adjust_huge(vmg, vma);
        bindings::rust_helper_vma_merge_commit_set_range(vmg, vma);
        bindings::rust_helper_vma_merge_commit_adjust_set_range(vmg);
        bindings::rust_helper_vma_merge_commit_store(vmg);
        bindings::rust_helper_vma_merge_commit_complete(vp.as_mut_ptr(), vmg, vma);

        0
    }
}

/// Commits a prepared VMA merge.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation
/// whose preallocation contract permits a structural commit.
#[export]
unsafe extern "C" fn rust_vma_merge_commit(vmg: *mut MergeParams) -> i32 {
    // SAFETY: The caller upholds the merge-operation requirements.
    unsafe { commit(vmg) }
}
