// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for merging after an existing VMA change.
//!
//! Rust owns merge scenario selection, removal fallback, range and target
//! adjustment, anonymous-VMA duplication sequencing, and state transitions.
//! C retains iterator primitives, field writes, locks, anonymous-VMA helpers,
//! structural merge commitment, and debug invariants.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_RUST_VMA_MERGE_COMMIT)]
use crate::vma_merge_commit;
#[cfg(CONFIG_RUST_VMA_MERGE_COMPAT)]
use crate::vma_merge_compat;

type MergeState = bindings::rust_vme_state;
type MergeParams = bindings::vma_merge_struct;
type Vma = bindings::vm_area_struct;

/// Tests left-neighbor compatibility with the native Rust policy.
///
/// # Safety
///
/// `state` must describe a live existing-range merge under the mmap write lock.
#[cfg(CONFIG_RUST_VMA_MERGE_COMPAT)]
unsafe fn can_merge_left(_state: *mut MergeState, vmg: *mut MergeParams) -> bool {
    // SAFETY: The caller holds the mmap write lock and supplied live merge
    // state.
    unsafe { vma_merge_compat::can_merge_left(vmg) }
}

/// Tests left-neighbor compatibility with the C fallback policy.
///
/// # Safety
///
/// `state` must describe a live existing-range merge under the mmap write lock.
#[cfg(not(CONFIG_RUST_VMA_MERGE_COMPAT))]
unsafe fn can_merge_left(state: *mut MergeState, _vmg: *mut MergeParams) -> bool {
    // SAFETY: The caller holds the mmap write lock and supplied live merge
    // state.
    unsafe { bindings::rust_helper_vma_merge_existing_can_merge_left(state) }
}

/// Tests right-neighbor compatibility with the native Rust policy.
///
/// # Safety
///
/// `state` must describe a live existing-range merge under the mmap write lock.
#[cfg(CONFIG_RUST_VMA_MERGE_COMPAT)]
unsafe fn can_merge_right(
    _state: *mut MergeState,
    vmg: *mut MergeParams,
    can_merge_left: bool,
) -> bool {
    // SAFETY: The caller holds the mmap write lock and supplied live merge
    // state.
    unsafe { vma_merge_compat::can_merge_right(vmg, can_merge_left) }
}

/// Tests right-neighbor compatibility with the C fallback policy.
///
/// # Safety
///
/// `state` must describe a live existing-range merge under the mmap write lock.
#[cfg(not(CONFIG_RUST_VMA_MERGE_COMPAT))]
unsafe fn can_merge_right(
    state: *mut MergeState,
    _vmg: *mut MergeParams,
    can_merge_left: bool,
) -> bool {
    // SAFETY: The caller holds the mmap write lock and supplied live merge
    // state.
    unsafe { bindings::rust_helper_vma_merge_existing_can_merge_right(state, can_merge_left) }
}

/// Restores iterator state after a failed merge attempt.
///
/// # Safety
///
/// `state` must describe a live merge operation.
unsafe fn abort(state: *mut MergeState, start: usize) {
    // SAFETY: The caller supplies a live merge operation and the original
    // start address.
    unsafe {
        bindings::rust_helper_vma_merge_existing_iter_set(state, start);
        bindings::rust_helper_vma_merge_existing_iter_load(state);
        bindings::rust_helper_vma_merge_existing_unlink_anon(state);

        if !bindings::rust_helper_vma_merge_existing_give_up_on_oom(state) {
            bindings::rust_helper_vma_merge_existing_set_nomem(state);
        }
    }
}

/// Commits the structural merge through the native Rust implementation.
///
/// # Safety
///
/// `state` must describe a live merge operation under the mmap write lock.
#[cfg(CONFIG_RUST_VMA_MERGE_COMMIT)]
unsafe fn commit(state: *mut MergeState) -> i32 {
    // SAFETY: The caller supplies a live merge operation.
    unsafe { vma_merge_commit::commit((*state).vmg) }
}

/// Commits the structural merge through the C fallback implementation.
///
/// # Safety
///
/// `state` must describe a live merge operation under the mmap write lock.
#[cfg(not(CONFIG_RUST_VMA_MERGE_COMMIT))]
unsafe fn commit(state: *mut MergeState) -> i32 {
    // SAFETY: The caller supplies a live merge operation.
    unsafe { bindings::rust_helper_vma_merge_existing_commit(state) }
}

/// Attempts to merge adjacent VMAs after an existing VMA range changed.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a live
/// existing-range merge operation.
unsafe fn merge_existing(state: *mut MergeState) -> *mut Vma {
    // SAFETY: The caller upholds the merge-state requirements. Every helper
    // operates on the supplied merge state or one of its live VMAs.
    unsafe {
        let vmg = (*state).vmg;
        let middle = bindings::rust_helper_vma_merge_existing_middle(state);
        let prev = bindings::rust_helper_vma_merge_existing_prev(state);
        let start = bindings::rust_helper_vma_merge_existing_start(state);
        let end = bindings::rust_helper_vma_merge_existing_end(state);

        bindings::rust_helper_vma_merge_existing_validate(state);
        bindings::rust_helper_vma_merge_existing_set_nomerge(state);

        let left_side = !middle.is_null()
            && start == bindings::rust_helper_vma_merge_existing_vma_start(middle);
        let right_side =
            !middle.is_null() && end == bindings::rust_helper_vma_merge_existing_vma_end(middle);

        if bindings::rust_helper_vma_merge_existing_is_special(state) || (!left_side && !right_side)
        {
            return core::ptr::null_mut();
        }
        if middle.is_null() {
            return core::ptr::null_mut();
        }

        let merge_left = left_side && can_merge_left(state, vmg);
        let mut next = core::ptr::null_mut();
        let mut merge_right = false;

        if right_side {
            next = bindings::rust_helper_vma_merge_existing_iter_next_range(state);
            bindings::rust_helper_vma_merge_existing_iter_prev_range(state);
            bindings::rust_helper_vma_merge_existing_set_next(state, next);
            merge_right = can_merge_right(state, vmg, merge_left);
        }

        if merge_left {
            bindings::rust_helper_vma_merge_existing_prev_range(state);
        } else if !merge_right {
            return core::ptr::null_mut();
        }

        let mut merge_both = merge_left && merge_right;
        bindings::rust_helper_vma_merge_existing_set_remove_middle(state, left_side && right_side);

        if left_side && right_side && !bindings::rust_helper_vma_merge_existing_can_remove(middle) {
            return core::ptr::null_mut();
        }

        bindings::rust_helper_vma_merge_existing_set_remove_next(state, merge_both);

        if merge_both && !bindings::rust_helper_vma_merge_existing_can_remove(next) {
            bindings::rust_helper_vma_merge_existing_set_remove_next(state, false);
            merge_right = false;
            merge_both = false;
        }

        bindings::rust_helper_vma_merge_existing_start_write(middle);

        let target;
        if merge_right {
            bindings::rust_helper_vma_merge_existing_start_write(next);
            bindings::rust_helper_vma_merge_existing_set_target(state, next);
            bindings::rust_helper_vma_merge_existing_add_target_sticky(state, next);
        }

        if merge_left {
            bindings::rust_helper_vma_merge_existing_start_write(prev);
            bindings::rust_helper_vma_merge_existing_set_target(state, prev);
            bindings::rust_helper_vma_merge_existing_add_target_sticky(state, prev);
        }

        let err;
        if merge_both {
            bindings::rust_helper_vma_merge_existing_set_start(
                state,
                bindings::rust_helper_vma_merge_existing_vma_start(prev),
            );
            bindings::rust_helper_vma_merge_existing_set_end(
                state,
                bindings::rust_helper_vma_merge_existing_vma_end(next),
            );
            bindings::rust_helper_vma_merge_existing_set_start_pgoffs(state, prev);

            let source = if bindings::rust_helper_vma_merge_existing_vma_has_anon_vma(next) {
                next
            } else {
                middle
            };
            err = bindings::rust_helper_vma_merge_existing_dup_anon(state, prev, source);
            target = prev;
        } else if merge_left {
            bindings::rust_helper_vma_merge_existing_set_start(
                state,
                bindings::rust_helper_vma_merge_existing_vma_start(prev),
            );
            bindings::rust_helper_vma_merge_existing_set_start_pgoffs(state, prev);

            if !(left_side && right_side) {
                bindings::rust_helper_vma_merge_existing_set_adjust_middle_start(state, true);
            }

            err = bindings::rust_helper_vma_merge_existing_dup_anon(state, prev, middle);
            target = prev;
        } else {
            let pglen = bindings::rust_helper_vma_merge_existing_pages(state);

            bindings::rust_helper_vma_merge_existing_debug_right(state, merge_right);

            if left_side && right_side {
                let next_pgoff = bindings::rust_helper_vma_merge_existing_vma_start_pgoff(next);
                let next_anon_pgoff =
                    bindings::rust_helper_vma_merge_existing_vma_start_anon_pgoff(next);

                bindings::rust_helper_vma_merge_existing_set_end(
                    state,
                    bindings::rust_helper_vma_merge_existing_vma_end(next),
                );
                bindings::rust_helper_vma_merge_existing_set_pgoffs(
                    state,
                    next_pgoff.wrapping_sub(pglen),
                    next_anon_pgoff.wrapping_sub(pglen),
                );
            } else {
                bindings::rust_helper_vma_merge_existing_set_adjust_next_start(state, true);
                bindings::rust_helper_vma_merge_existing_set_start(
                    state,
                    bindings::rust_helper_vma_merge_existing_vma_start(middle),
                );
                bindings::rust_helper_vma_merge_existing_set_end(state, start);
                bindings::rust_helper_vma_merge_existing_set_start_pgoffs(state, middle);
            }

            err = bindings::rust_helper_vma_merge_existing_dup_anon(state, next, middle);
            target = next;
        }

        if err != 0 || commit(state) != 0 {
            abort(state, start);
            return core::ptr::null_mut();
        }

        bindings::rust_helper_vma_merge_existing_apply_sticky(state, target);
        bindings::rust_helper_vma_merge_existing_khugepaged_enter(state, target);
        bindings::rust_helper_vma_merge_existing_set_success(state);
        target
    }
}

/// Attempts to merge adjacent VMAs after an existing VMA range changed.
///
/// # Safety
///
/// The caller must hold the mmap write lock and initialize `state` with a live
/// existing-range merge operation.
#[export]
unsafe extern "C" fn rust_vma_merge_existing(state: *mut MergeState) -> *mut Vma {
    // SAFETY: The caller upholds the merge-state requirements.
    unsafe { merge_existing(state) }
}
