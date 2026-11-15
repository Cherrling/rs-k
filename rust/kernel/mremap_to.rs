// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for moving a VMA to a selected destination.
//!
//! Rust owns fixed-destination release and source reload, shrink completion,
//! `MREMAP_DONTUNMAP` expansion checks, destination selection, and the final
//! move. C retains the operations that these decisions invoke.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mremap_to_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Moves one prepared VMA to its destination.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The remap state and source VMA
/// must remain live until this function returns.
unsafe fn mremap_to(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state. Each helper performs one
    // narrow access or one existing operation.
    unsafe {
        if bindings::rust_helper_mremap_to_is_fixed(state) {
            let err = bindings::rust_helper_mremap_to_unmap_destination(state);

            bindings::rust_helper_mremap_to_invalidate_source(state);
            if err != 0 {
                return err;
            }

            // Releasing the destination can split or replace the source VMA
            // when both ranges belong to the same original mapping.
            if !bindings::rust_helper_mremap_to_reload_source(state) {
                return error_value(-errno(bindings::EFAULT));
            }
        }

        if bindings::rust_helper_mremap_to_is_shrink(state) {
            let err = bindings::rust_helper_mremap_to_shrink_source(state);
            if err != 0 {
                return err;
            }

            bindings::rust_helper_mremap_to_shrink_complete(state);
        }

        if bindings::rust_helper_mremap_to_keeps_source(state)
            && !bindings::rust_helper_mremap_to_can_expand_for_dontunmap(state)
        {
            return error_value(-errno(bindings::ENOMEM));
        }

        let err = bindings::rust_helper_mremap_to_set_new_addr(state);
        if err != 0 {
            return err;
        }

        bindings::rust_helper_mremap_to_move(state)
    }
}

/// Moves one prepared VMA to its destination.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`mremap_to`].
#[export]
unsafe extern "C" fn rust_mremap_to(state: *mut State) -> usize {
    // SAFETY: The caller upholds the mremap destination requirements.
    unsafe { mremap_to(state) }
}
