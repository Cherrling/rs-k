// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for expanding one VMA.
//!
//! Rust owns the write-lock ordering, target and next-VMA selection, removal
//! decision, sticky-flag propagation, anonymous-VMA duplication order,
//! structural commit, and failure rollback. C retains individual flag
//! operations, anonymous-VMA helpers, locking, debug checks, and the merge
//! commit primitive.

use crate::bindings;
use core::ffi::c_int;
use core::mem::MaybeUninit;
use core::ptr::null_mut;
use macros::export;

type MergeParams = bindings::vma_merge_struct;
type VmaFlags = bindings::vma_flags_t;

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Expands a target VMA to the requested merge range.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation.
/// The operation must have a target VMA and a valid next-VMA relationship.
unsafe fn expand(vmg: *mut MergeParams) -> c_int {
    // SAFETY: The caller supplies a live merge operation. Each helper performs
    // one narrow access or one existing primitive operation on it.
    unsafe {
        let target = bindings::rust_helper_vma_expand_target(vmg);
        let next = bindings::rust_helper_vma_expand_next(vmg);

        bindings::rust_helper_vma_expand_assert_write_locked(vmg);
        bindings::rust_helper_vma_expand_start_write(target);

        let mut sticky = MaybeUninit::<VmaFlags>::uninit();
        bindings::rust_helper_vma_expand_init_sticky(sticky.as_mut_ptr(), vmg);
        bindings::rust_helper_vma_expand_add_sticky(sticky.as_mut_ptr(), target);

        let remove_next = !next.is_null()
            && target != next
            && bindings::rust_helper_vma_expand_end(vmg)
                == bindings::rust_helper_vma_expand_vma_end(next);

        bindings::rust_helper_vma_expand_validate(vmg, target, next, remove_next);

        let mut anon_dup = null_mut();
        if remove_next {
            let err = bindings::rust_helper_vma_expand_dup_anon(target, next, &mut anon_dup);
            if err != 0 {
                return err;
            }
        }

        let copied_from = bindings::rust_helper_vma_expand_copied_from(vmg);
        if !copied_from.is_null() {
            let err = bindings::rust_helper_vma_expand_dup_anon(target, copied_from, &mut anon_dup);
            if err != 0 {
                return err;
            }
        }

        if remove_next {
            bindings::rust_helper_vma_expand_start_write(next);
            bindings::rust_helper_vma_expand_set_remove_next(vmg, true);
            bindings::rust_helper_vma_expand_add_sticky(sticky.as_mut_ptr(), next);
        }

        if bindings::rust_helper_vma_expand_commit(vmg) != 0 {
            if !anon_dup.is_null() {
                bindings::rust_helper_vma_expand_unlink_anon(anon_dup);
            }
            if !bindings::rust_helper_vma_expand_give_up_on_oom(vmg) {
                bindings::rust_helper_vma_expand_set_nomem(vmg);
            }
            return -errno(bindings::ENOMEM);
        }

        bindings::rust_helper_vma_expand_apply_sticky(target, sticky.as_ptr());

        0
    }
}

/// Expands a target VMA to the requested merge range.
///
/// # Safety
///
/// The caller must hold the mmap write lock and supply a live merge operation.
#[export]
unsafe extern "C" fn rust_vma_expand(vmg: *mut MergeParams) -> c_int {
    // SAFETY: The caller upholds the VMA expansion requirements.
    unsafe { expand(vmg) }
}
