// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for attaching a file to a new mapping VMA.
//!
//! Rust owns file-reference selection, mapping callback selection, failure
//! cleanup ordering, warning selection, and mapping-state synchronization.
//! C retains the existing file, callback, reverse-mapping, warning, and
//! state-write operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_create_state;

/// Attaches a file and invokes its mapping callback for a new VMA.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state, mapping file, VMA,
/// iterator, and gathered unmap state must remain valid until this function
/// returns.
unsafe fn do_file(state: *mut State) -> c_int {
    // SAFETY: The C adapter provides a live state for this call.
    unsafe {
        bindings::rust_helper_mmap_file_set(state);
        if bindings::rust_helper_mmap_file_needs_get(state) {
            bindings::rust_helper_mmap_file_get(state);
        }

        if !bindings::rust_helper_mmap_file_has_callback(state) {
            return 0;
        }

        let error = bindings::rust_helper_mmap_file_map(state);
        if error != 0 {
            bindings::rust_helper_mmap_file_prepare_unmap(state);
            bindings::rust_helper_mmap_file_put(state);
            bindings::rust_helper_mmap_file_unmap_partial(state);
            return error;
        }

        if bindings::rust_helper_mmap_file_address_changed(state) {
            bindings::rust_helper_mmap_file_warn_addr(state);
        }
        if bindings::rust_helper_mmap_file_adds_write(state) {
            bindings::rust_helper_mmap_file_warn_write(state);
        }

        bindings::rust_helper_mmap_file_sync_state(state);
        0
    }
}

/// Attaches a file and invokes its mapping callback for a new VMA.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_file`].
#[export]
unsafe extern "C" fn rust_mmap_region_file(state: *mut State) -> c_int {
    // SAFETY: The C adapter upholds the mapping-state requirements.
    unsafe { do_file(state) }
}
