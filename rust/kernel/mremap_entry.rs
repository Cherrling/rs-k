// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for the `mremap` syscall entry.
//!
//! Rust owns parameter validation, mmap locking, early rejection, operation
//! dispatch, unlock, populated expansion, and userfaultfd notification. C
//! retains the existing field projections, predicates, and operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mremap_entry_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Validates one aligned remap request.
///
/// # Safety
///
/// The caller supplies a live remap state. Each helper performs one narrow
/// access or one predicate.
unsafe fn validate(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state.
    unsafe {
        if bindings::rust_helper_mremap_entry_unsupported_flags(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if !bindings::rust_helper_mremap_entry_addr_aligned(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if bindings::rust_helper_mremap_entry_new_len_invalid(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if !bindings::rust_helper_mremap_entry_implies_new_addr(state) {
            return 0;
        }

        if bindings::rust_helper_mremap_entry_new_addr_invalid(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if !bindings::rust_helper_mremap_entry_has_maymove(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if bindings::rust_helper_mremap_entry_dontunmap_resize_invalid(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        if bindings::rust_helper_mremap_entry_overlaps(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        0
    }
}

/// Validates and executes one remap request.
///
/// # Safety
///
/// The caller supplies a live remap state and current address space.
unsafe fn do_mremap(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state. Each helper performs one
    // narrow access, predicate, or existing operation.
    unsafe {
        bindings::rust_helper_mremap_entry_align_lengths(state);

        let err = validate(state);
        if err != 0 {
            return err;
        }

        let err = bindings::rust_helper_mremap_entry_acquire_mmap_write_lock(state);
        if err != 0 {
            return error_value(err);
        }

        if !bindings::rust_helper_mremap_entry_map_count_allows_split(state) {
            bindings::rust_helper_mremap_entry_unlock(state);
            return error_value(-errno(bindings::ENOMEM));
        }

        let res = if bindings::rust_helper_mremap_entry_move_only(state) {
            bindings::rust_helper_mremap_entry_remap_move(state)
        } else {
            bindings::rust_helper_mremap_entry_lookup_source(state);
            let err = bindings::rust_helper_mremap_entry_check_prepare(state);
            if err != 0 {
                err
            } else if bindings::rust_helper_mremap_entry_implies_new_addr(state) {
                bindings::rust_helper_mremap_entry_mremap_to(state)
            } else {
                bindings::rust_helper_mremap_entry_mremap_at(state)
            }
        };

        let failed = bindings::rust_helper_mremap_entry_is_error_value(res);

        if bindings::rust_helper_mremap_entry_mmap_locked(state) {
            bindings::rust_helper_mremap_entry_unlock(state);
        }

        if !failed && bindings::rust_helper_mremap_entry_needs_populate(state) {
            bindings::rust_helper_mremap_entry_populate(state);
        }

        bindings::rust_helper_mremap_entry_notify_uffd(state, failed);
        res
    }
}

/// Validates and executes one remap request.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mremap`].
#[export]
unsafe extern "C" fn rust_mremap_entry(state: *mut State) -> usize {
    // SAFETY: The caller upholds the mremap entry requirements.
    unsafe { do_mremap(state) }
}
