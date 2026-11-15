// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for mmap preparation callbacks.
//!
//! Rust owns callback success selection, deferred-action dispatch,
//! replacement-file selection, and descriptor synchronization. C retains the
//! existing callback and state-write operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_create_state;

/// Invokes a mapping's preparation callbacks and synchronizes their result.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state, mapping file,
/// descriptor, and all referenced address-space objects must remain valid
/// until this function returns.
unsafe fn do_prepare(state: *mut State) -> c_int {
    // SAFETY: The C adapter provides a live state for this call.
    unsafe {
        let error = bindings::rust_helper_mmap_prepare_call_file(state);
        if error != 0 {
            return error;
        }

        let error = bindings::rust_helper_mmap_prepare_call_action(state);
        if error != 0 {
            return error;
        }

        if bindings::rust_helper_mmap_prepare_file_changed(state) {
            bindings::rust_helper_mmap_prepare_adopt_file(state);
        }

        bindings::rust_helper_mmap_prepare_sync_fields(state);
        0
    }
}

/// Invokes a mapping's preparation callbacks and synchronizes their result.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_prepare`].
#[export]
unsafe extern "C" fn rust_mmap_region_prepare(state: *mut State) -> c_int {
    // SAFETY: The C adapter upholds the region-state requirements.
    unsafe { do_prepare(state) }
}
