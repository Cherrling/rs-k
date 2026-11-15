// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for moving a VMA during `mremap`.
//!
//! Rust owns the move preparation, charge, copy, failure recovery, `DONTUNMAP`
//! completion, accounting, source release, high-watermark restoration, and
//! return-value decisions. C retains the existing operations that these
//! decisions invoke.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mremap_move_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Moves one prepared VMA and releases or preserves its source as required.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The remap state and source VMA
/// must remain live until this function returns.
unsafe fn move_vma(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state. Each helper performs one
    // narrow access or one existing operation.
    unsafe {
        let err = bindings::rust_helper_mremap_move_prepare(state);
        if err != 0 {
            return err;
        }

        if !bindings::rust_helper_mremap_move_charge(state) {
            return error_value(-errno(bindings::ENOMEM));
        }

        bindings::rust_helper_mremap_move_start_write(state);

        let err = bindings::rust_helper_mremap_move_copy(state);
        if err != 0 && !bindings::rust_helper_mremap_move_has_new_vma(state) {
            return error_value(err);
        }

        let hiwater_vm = bindings::rust_helper_mremap_move_get_hiwater();
        let is_dontunmap = bindings::rust_helper_mremap_move_is_dontunmap(state);

        if is_dontunmap && err == 0 {
            bindings::rust_helper_mremap_move_dontunmap_complete(state);
        }

        bindings::rust_helper_mremap_move_account(state);
        if !is_dontunmap || err != 0 {
            bindings::rust_helper_mremap_move_unmap_source(state);
        }

        bindings::rust_helper_mremap_move_set_hiwater(hiwater_vm);

        if err != 0 {
            error_value(err)
        } else {
            bindings::rust_helper_mremap_move_new_addr(state)
        }
    }
}

/// Moves one prepared VMA and releases or preserves its source as required.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`move_vma`].
#[export]
unsafe extern "C" fn rust_mremap_move(state: *mut State) -> usize {
    // SAFETY: The caller upholds the mremap move requirements.
    unsafe { move_vma(state) }
}
