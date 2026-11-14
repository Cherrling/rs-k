// SPDX-License-Identifier: GPL-2.0

//! Native VMA merge compatibility policy.
//!
//! Rust owns the complete compatibility predicates and left, right, and
//! two-sided merge decisions. C retains single-field access, VMA flag bitmap
//! comparison, memory-policy equality, userfaultfd context comparison,
//! anonymous-VMA name comparison, and debug invariant checks.

use crate::bindings;
use macros::export;

type MergeState = bindings::vma_merge_struct;
type Vma = bindings::vm_area_struct;

/// Tests whether a VMA may have inherited an anonymous mapping from a parent.
///
/// # Safety
///
/// `vma` must be a live VMA. A non-null VMA must have an initialized
/// `anon_vma_chain` list.
unsafe fn vma_is_fork_child(vma: *mut Vma) -> bool {
    // SAFETY: The caller supplies a live VMA. The anonymous VMA test protects
    // the chain list access.
    unsafe {
        !vma.is_null()
            && bindings::rust_helper_vma_merge_compat_vma_has_anon_vma(vma)
            && !bindings::rust_helper_vma_merge_compat_anon_vma_chain_is_singular(vma)
    }
}

/// Tests whether a proposed merge state is compatible with one adjacent VMA.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock. The VMA
/// selected by `merge_next` must be live.
unsafe fn is_mergeable_vma(vmg: *mut MergeState, merge_next: bool) -> bool {
    // SAFETY: The caller supplies live merge state and adjacent VMAs. Each
    // helper reads one field or calls one existing side-effect-free predicate.
    unsafe {
        let vma = if merge_next {
            bindings::rust_helper_vma_merge_compat_vmg_next(vmg)
        } else {
            bindings::rust_helper_vma_merge_compat_vmg_prev(vmg)
        };

        !vma.is_null()
            && bindings::rust_helper_vma_merge_compat_mempolicy_equal(vmg, vma)
            && bindings::rust_helper_vma_merge_compat_flags_compatible(vmg, vma)
            && bindings::rust_helper_vma_merge_compat_vmg_file(vmg)
                == bindings::rust_helper_vma_merge_compat_vma_file(vma)
            && bindings::rust_helper_vma_merge_compat_userfaultfd_compatible(vmg, vma)
            && bindings::rust_helper_vma_merge_compat_anon_name_equal(vmg, vma)
    }
}

/// Tests whether the proposed and adjacent anonymous VMAs are compatible.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock. The VMA
/// selected by `merge_next` must be live.
unsafe fn is_mergeable_anon_vma(vmg: *mut MergeState, merge_next: bool) -> bool {
    // SAFETY: The caller supplies a live merge state and adjacent VMA. The
    // helper retains the existing debug warning for an inconsistent state.
    unsafe {
        bindings::rust_helper_vma_merge_compat_validate_anon_vma(vmg);

        let target = if merge_next {
            bindings::rust_helper_vma_merge_compat_vmg_next(vmg)
        } else {
            bindings::rust_helper_vma_merge_compat_vmg_prev(vmg)
        };
        let source = bindings::rust_helper_vma_merge_compat_vmg_middle(vmg);
        let target_anon = bindings::rust_helper_vma_merge_compat_vma_anon_vma(target);
        let source_anon = bindings::rust_helper_vma_merge_compat_vmg_anon_vma(vmg);

        if target_anon.is_null() && !source_anon.is_null() {
            let copied_from = bindings::rust_helper_vma_merge_compat_vmg_copied_from(vmg);

            return !vma_is_fork_child(source) && !vma_is_fork_child(copied_from);
        }

        if !target_anon.is_null() && source_anon.is_null() {
            return !vma_is_fork_child(target);
        }

        source_anon == target_anon
    }
}

/// Tests whether adjacent anonymous page offsets must also be continuous.
///
/// # Safety
///
/// `vmg` must describe a live merge operation.
unsafe fn needs_adjacent_anon_pgoff(vmg: *mut MergeState) -> bool {
    // SAFETY: The helpers read the live merge state and its flags.
    unsafe {
        !bindings::rust_helper_vma_merge_compat_vmg_file(vmg).is_null()
            && bindings::rust_helper_vma_merge_compat_vmg_is_cow_mapping(vmg)
    }
}

/// Tests whether the proposed range can merge in front of its next VMA.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock with a live
/// next VMA.
unsafe fn can_merge_before(vmg: *mut MergeState) -> bool {
    // SAFETY: The caller supplies a live next VMA. The helpers read merge state,
    // VMA identity, and page offsets without side effects.
    unsafe {
        let next = bindings::rust_helper_vma_merge_compat_vmg_next(vmg);

        if !is_mergeable_vma(vmg, true) || !is_mergeable_anon_vma(vmg, true) {
            return false;
        }
        if bindings::rust_helper_vma_merge_compat_vmg_end_pgoff(vmg)
            != bindings::rust_helper_vma_merge_compat_vma_start_pgoff(next)
        {
            return false;
        }
        if needs_adjacent_anon_pgoff(vmg)
            && bindings::rust_helper_vma_merge_compat_vmg_end_anon_pgoff(vmg)
                != bindings::rust_helper_vma_merge_compat_vma_start_anon_pgoff(next)
        {
            return false;
        }

        true
    }
}

/// Tests whether the proposed range can merge after its previous VMA.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock with a live
/// previous VMA.
unsafe fn can_merge_after(vmg: *mut MergeState) -> bool {
    // SAFETY: The caller supplies a live previous VMA. The helpers read merge
    // state, VMA identity, and page offsets without side effects.
    unsafe {
        let prev = bindings::rust_helper_vma_merge_compat_vmg_prev(vmg);

        if !is_mergeable_vma(vmg, false) || !is_mergeable_anon_vma(vmg, false) {
            return false;
        }
        if bindings::rust_helper_vma_merge_compat_vma_end_pgoff(prev)
            != bindings::rust_helper_vma_merge_compat_vmg_start_pgoff(vmg)
        {
            return false;
        }
        if needs_adjacent_anon_pgoff(vmg)
            && bindings::rust_helper_vma_merge_compat_vma_end_anon_pgoff(prev)
                != bindings::rust_helper_vma_merge_compat_vmg_start_anon_pgoff(vmg)
        {
            return false;
        }

        true
    }
}

/// Tests whether the proposed range can merge with its previous VMA.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock.
pub(crate) unsafe fn can_merge_left(vmg: *mut MergeState) -> bool {
    // SAFETY: The helpers read the live merge state and its previous VMA.
    unsafe {
        let prev = bindings::rust_helper_vma_merge_compat_vmg_prev(vmg);

        !prev.is_null()
            && bindings::rust_helper_vma_merge_compat_vma_end(prev)
                == bindings::rust_helper_vma_merge_compat_vmg_start(vmg)
            && can_merge_after(vmg)
    }
}

/// Tests whether the proposed range can merge with its next VMA.
///
/// # Safety
///
/// `vmg` must describe a merge operation under the mmap write lock.
pub(crate) unsafe fn can_merge_right(vmg: *mut MergeState, can_merge_left: bool) -> bool {
    // SAFETY: The helpers read the live merge state and its adjacent VMAs.
    unsafe {
        let next = bindings::rust_helper_vma_merge_compat_vmg_next(vmg);

        if next.is_null()
            || bindings::rust_helper_vma_merge_compat_vmg_end(vmg)
                != bindings::rust_helper_vma_merge_compat_vma_start(next)
            || !can_merge_before(vmg)
        {
            return false;
        }
        if !can_merge_left {
            return true;
        }

        let prev = bindings::rust_helper_vma_merge_compat_vmg_prev(vmg);
        let prev_anon = bindings::rust_helper_vma_merge_compat_vma_anon_vma(prev);
        let next_anon = bindings::rust_helper_vma_merge_compat_vma_anon_vma(next);

        prev_anon.is_null() || next_anon.is_null() || prev_anon == next_anon
    }
}

/// Tests whether an existing range can merge with its previous VMA.
///
/// # Safety
///
/// `vmg` must describe a live merge operation under the mmap write lock.
#[export]
unsafe extern "C" fn rust_vma_merge_compat_can_merge_left(vmg: *mut MergeState) -> bool {
    // SAFETY: The caller upholds the merge-state requirements.
    unsafe { can_merge_left(vmg) }
}

/// Tests whether an existing range can merge with its next VMA.
///
/// # Safety
///
/// `vmg` must describe a live merge operation under the mmap write lock.
#[export]
unsafe extern "C" fn rust_vma_merge_compat_can_merge_right(
    vmg: *mut MergeState,
    can_merge_left: bool,
) -> bool {
    // SAFETY: The caller upholds the merge-state requirements.
    unsafe { can_merge_right(vmg, can_merge_left) }
}
