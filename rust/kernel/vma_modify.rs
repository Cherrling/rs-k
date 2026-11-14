// SPDX-License-Identifier: GPL-2.0

//! Native merge-or-split orchestration for VMA modifications.
//!
//! Rust owns the decision to attempt an existing-range merge, propagate a
//! backup failure, enforce the give-up-on-OOM split invariant, split leading
//! and trailing ranges, and return the modified VMA. C retains field access,
//! error-pointer construction, warnings, the existing merge entry, and the
//! individual split operation.

use crate::bindings;
use macros::export;

type MergeParams = bindings::vma_merge_struct;
type Vma = bindings::vm_area_struct;

/// Modifies one VMA by merging it or splitting its leading and trailing ranges.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation
/// whose middle VMA overlaps the requested range.
unsafe fn modify(vmg: *mut MergeParams) -> *mut Vma {
    // SAFETY: The caller supplies a live merge operation. Each helper performs
    // one access or one split using that operation or its live VMA.
    unsafe {
        let vma = bindings::rust_helper_vma_modify_middle(vmg);
        let start = bindings::rust_helper_vma_modify_start(vmg);
        let end = bindings::rust_helper_vma_modify_end(vmg);

        let merged = bindings::rust_helper_vma_modify_merge(vmg);
        if !merged.is_null() {
            return merged;
        }

        let backup_error = bindings::rust_helper_vma_modify_backup_error(vmg);
        if backup_error != 0 {
            return bindings::rust_helper_vma_modify_error(backup_error);
        }

        bindings::rust_helper_vma_modify_warn_if_give_up_on_oom(vmg);

        if bindings::rust_helper_vma_modify_vma_start(vma) < start {
            let err = bindings::rust_helper_vma_modify_split(vmg, vma, start, 1);
            if err != 0 {
                return bindings::rust_helper_vma_modify_error(err);
            }
        }

        if bindings::rust_helper_vma_modify_vma_end(vma) > end {
            let err = bindings::rust_helper_vma_modify_split(vmg, vma, end, 0);
            if err != 0 {
                return bindings::rust_helper_vma_modify_error(err);
            }
        }

        vma
    }
}

/// Modifies one VMA by merging it or splitting its leading and trailing ranges.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation
/// whose middle VMA overlaps the requested range.
#[export]
unsafe extern "C" fn rust_vma_modify(vmg: *mut MergeParams) -> *mut Vma {
    // SAFETY: The caller upholds the merge-operation requirements.
    unsafe { modify(vmg) }
}
