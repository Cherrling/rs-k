// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for creating a mapping after setup.
//!
//! Rust owns setup dispatch, preparation callbacks, adjacent merging, new-VMA
//! allocation, completion, deferred actions, and failure cleanup ordering. C
//! retains the existing VMA, maple-tree, accounting, and callback operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_create_state;

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Aborts a region operation that did not complete.
///
/// # Safety
///
/// The state must contain the initialization performed by the C adapter. The
/// caller must hold the mmap write lock and keep the state live.
unsafe fn abort(state: *mut State) {
    // SAFETY: The caller supplies a live, initialized region state.
    unsafe {
        if bindings::rust_helper_mmap_create_file_changed(state) {
            bindings::rust_helper_mmap_create_put_changed_file(state);
        }
        bindings::rust_helper_mmap_create_abort_munmap(state);
    }
}

/// Aborts a region operation after setup succeeded.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`abort`], and setup must have
/// returned success.
unsafe fn abort_after_setup(state: *mut State) {
    // SAFETY: The C helpers expose only the fixed accounting rollback.
    unsafe {
        if bindings::rust_helper_mmap_create_charged(state) {
            bindings::rust_helper_mmap_create_unaccount(state);
        }
        abort(state);
    }
}

/// Creates a mapping after request validation.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state and all referenced
/// address-space, file, VMA iterator, and userfaultfd objects must remain valid
/// until this function returns.
unsafe fn do_mmap_region_create(state: *mut State) -> usize {
    // SAFETY: The C adapter provides a live state for the duration of this call.
    unsafe {
        let have_mmap_prepare = bindings::rust_helper_mmap_create_have_prepare(state);
        let check_ksm_early = bindings::rust_helper_mmap_create_check_ksm_early(state);

        let error = bindings::rust_helper_mmap_create_setup(state);
        if error != 0 {
            abort(state);
            return error_value(error);
        }

        if have_mmap_prepare {
            let error = bindings::rust_helper_mmap_create_prepare(state);
            if error != 0 {
                abort_after_setup(state);
                return error_value(error);
            }
        }

        if check_ksm_early {
            bindings::rust_helper_mmap_create_update_ksm_flags(state);
        }

        if bindings::rust_helper_mmap_create_has_adjacent(state) {
            bindings::rust_helper_mmap_create_try_merge(state);
        }

        let mut allocated_new = false;
        if !bindings::rust_helper_mmap_create_has_vma(state) {
            let error = bindings::rust_helper_mmap_create_new_vma(state);
            if error != 0 {
                abort_after_setup(state);
                return error_value(error);
            }
            allocated_new = true;
        }

        if have_mmap_prepare && allocated_new {
            bindings::rust_helper_mmap_create_set_user_fields(state);
        }

        bindings::rust_helper_mmap_create_complete(state);

        if have_mmap_prepare && allocated_new {
            let error = bindings::rust_helper_mmap_create_action_complete(state);
            if error != 0 {
                return error_value(error);
            }
        }

        bindings::rust_helper_mmap_create_addr(state)
    }
}

/// Creates a mapping after request validation.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mmap_region_create`].
#[export]
unsafe extern "C" fn rust_mmap_region_create(state: *mut State) -> usize {
    // SAFETY: The C adapter upholds the region-state requirements.
    unsafe { do_mmap_region_create(state) }
}
