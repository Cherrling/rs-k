// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for merging a proposed VMA range.
//!
//! Rust owns merge scenario selection, range and target adjustment, iterator
//! repositioning, and the success state transition. C retains VMA
//! compatibility predicates, structural expansion, reverse-map updates, and
//! khugepaged integration.

use crate::bindings;
use macros::export;

type MergeState = bindings::rust_vma_merge_new_range_state;
type Vma = bindings::vm_area_struct;

/// Attempts to merge a proposed new VMA range into an adjacent VMA.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a
/// live merge state for an empty proposed range.
unsafe fn merge_new_range(state: *mut MergeState) -> *mut Vma {
    // SAFETY: The caller upholds the merge-state requirements. Every helper
    // below operates on the supplied merge state or one of its VMAs.
    unsafe {
        let state = &mut *state;
        let vmg = state.vmg;
        let prev = state.prev;
        let next = state.next;
        let original_end = state.end;

        bindings::rust_helper_vma_merge_new_range_validate(vmg, original_end);
        bindings::rust_helper_vma_merge_new_range_set_nomerge(vmg);

        if bindings::rust_helper_vma_merge_new_range_is_special(vmg)
            || (prev.is_null() && next.is_null())
        {
            return core::ptr::null_mut();
        }

        let can_merge_left = bindings::rust_helper_vma_merge_new_range_can_merge_left(vmg);
        let can_merge_right = !state.just_expand
            && bindings::rust_helper_vma_merge_new_range_can_merge_right(vmg, can_merge_left);

        if can_merge_right {
            bindings::rust_helper_vma_merge_new_range_set_end(
                vmg,
                bindings::rust_helper_vma_merge_new_range_next_end(next),
            );
            bindings::rust_helper_vma_merge_new_range_set_target(vmg, next);
        }

        if can_merge_left {
            bindings::rust_helper_vma_merge_new_range_set_start(
                vmg,
                bindings::rust_helper_vma_merge_new_range_prev_start(prev),
            );
            bindings::rust_helper_vma_merge_new_range_set_target(vmg, prev);
            bindings::rust_helper_vma_merge_new_range_set_start_pgoffs(vmg, prev);

            if can_merge_right && !bindings::rust_helper_vma_merge_new_range_can_remove_next(next) {
                bindings::rust_helper_vma_merge_new_range_set_end(vmg, original_end);
            }

            if !state.just_expand {
                bindings::rust_helper_vma_merge_new_range_prev(vmg);
            }
        }

        let target = bindings::rust_helper_vma_merge_new_range_target(vmg);
        if !target.is_null() && bindings::rust_helper_vma_merge_new_range_expand(vmg) == 0 {
            bindings::rust_helper_vma_merge_new_range_khugepaged_enter(vmg, target);
            bindings::rust_helper_vma_merge_new_range_set_success(vmg);
            return target;
        }

        core::ptr::null_mut()
    }
}

/// Attempts to merge one proposed VMA range into the address space.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a
/// live merge state for an empty proposed range.
#[export]
unsafe extern "C" fn rust_vma_merge_new_range(state: *mut MergeState) -> *mut Vma {
    // SAFETY: The caller upholds the merge-state requirements.
    unsafe { merge_new_range(state) }
}
