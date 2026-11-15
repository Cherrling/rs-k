// SPDX-License-Identifier: GPL-2.0

//! Native VMA copying for `mremap`.
//!
//! Rust owns the merge-versus-duplicate decision, the `MREMAP_DONTUNMAP`
//! self-merge restriction, copied range setup, file and anonymous-state
//! ordering, insertion, and reverse-order failure cleanup. C retains single
//! field accesses and existing merge, allocation, policy, callback, locking,
//! iterator, and release primitives.

use crate::bindings;
use macros::export;

type State = bindings::rust_vma_copy_state;
type Vma = bindings::vm_area_struct;

/// Copies one VMA range to a new location in the same address space.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The source VMA and output
/// pointers in `state` must remain live until this function returns.
unsafe fn copy(state: *mut State) -> *mut Vma {
    // SAFETY: The caller supplies a live VMA and merge state. Each helper
    // performs one narrow access or one existing operation.
    unsafe {
        let mut can_self_merge = false;

        if !bindings::rust_helper_vma_copy_has_anon_vma(state) {
            let anon_pgoff = bindings::rust_helper_vma_copy_page_index(state);
            bindings::rust_helper_vma_copy_set_anon_pgoff(state, anon_pgoff);

            if bindings::rust_helper_vma_copy_is_anonymous(state) {
                bindings::rust_helper_vma_copy_set_pgoff(state, anon_pgoff);
                can_self_merge = true;
                bindings::rust_helper_vma_copy_set_can_self_merge(state, true);
            }
        }

        if bindings::rust_helper_vma_copy_source_has_file(state) {
            bindings::rust_helper_vma_copy_skip_uprobe(state);
        }

        let found = bindings::rust_helper_vma_copy_find_prev(state);
        if !found.is_null()
            && bindings::rust_helper_vma_copy_vma_start(found)
                < bindings::rust_helper_vma_copy_range_end(state)
        {
            return core::ptr::null_mut();
        }

        bindings::rust_helper_vma_copy_prepare_merge(state);

        // Keeping the source mapping makes it distinct from the destination.
        // A self-merge would combine them, so remove the source from both
        // adjacent positions and force the duplicate path.
        if can_self_merge && bindings::rust_helper_vma_copy_keep_source(state) {
            let source = bindings::rust_helper_vma_copy_source(state);
            if bindings::rust_helper_vma_copy_prev(state) == source {
                bindings::rust_helper_vma_copy_clear_prev(state);
            }
            if bindings::rust_helper_vma_copy_next(state) == source {
                bindings::rust_helper_vma_copy_clear_next(state);
            }
            bindings::rust_helper_vma_copy_clear_self_merge(state);
        }

        let merged = bindings::rust_helper_vma_copy_merge(state);
        if !merged.is_null() {
            let old_start = bindings::rust_helper_vma_copy_old_start(state);
            if bindings::rust_helper_vma_copy_vma_start(merged) < old_start
                && bindings::rust_helper_vma_copy_vma_end(merged) > old_start
            {
                bindings::rust_helper_vma_copy_warn_replaced_self(state, merged);
                bindings::rust_helper_vma_copy_replace_source(state, merged);
            }

            let source = bindings::rust_helper_vma_copy_source(state);
            bindings::rust_helper_vma_copy_set_need_rmap_locks(
                state,
                bindings::rust_helper_vma_copy_start_pgoff(merged)
                    <= bindings::rust_helper_vma_copy_start_pgoff(source),
            );
            return merged;
        }

        let new = bindings::rust_helper_vma_copy_dup(state);
        if new.is_null() {
            return core::ptr::null_mut();
        }

        bindings::rust_helper_vma_copy_set_range(state, new);

        if bindings::rust_helper_vma_copy_dup_policy(state, new) != 0 {
            bindings::rust_helper_vma_copy_free(new);
            return core::ptr::null_mut();
        }

        if bindings::rust_helper_vma_copy_anon_clone(state, new) != 0 {
            bindings::rust_helper_vma_copy_policy_put(new);
            bindings::rust_helper_vma_copy_free(new);
            return core::ptr::null_mut();
        }

        if bindings::rust_helper_vma_copy_has_file(new) {
            bindings::rust_helper_vma_copy_get_file(new);
        }
        if bindings::rust_helper_vma_copy_has_open(new) {
            bindings::rust_helper_vma_copy_open(new);
        }

        if bindings::rust_helper_vma_copy_link(state, new) != 0 {
            bindings::rust_helper_vma_copy_fixup_hugetlb(new);
            bindings::rust_helper_vma_copy_close(new);
            if bindings::rust_helper_vma_copy_has_file(new) {
                bindings::rust_helper_vma_copy_fput(new);
            }
            bindings::rust_helper_vma_copy_unlink_anon(new);
            bindings::rust_helper_vma_copy_policy_put(new);
            bindings::rust_helper_vma_copy_free(new);
            return core::ptr::null_mut();
        }

        bindings::rust_helper_vma_copy_set_need_rmap_locks(state, false);
        new
    }
}

/// Copies one VMA range to a new location in the same address space.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`copy`].
#[export]
unsafe extern "C" fn rust_vma_copy(state: *mut State) -> *mut Vma {
    // SAFETY: The caller upholds the VMA copy requirements.
    unsafe { copy(state) }
}
