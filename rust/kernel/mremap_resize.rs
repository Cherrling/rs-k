// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for resizing a mapping during `mremap`.
//!
//! Rust owns the no-resize, shrink, in-place expansion, expansion rejection,
//! destination selection, and expansion move decisions. C retains the existing
//! operations that these decisions invoke.

use crate::bindings;
use core::ffi::{c_int, c_uint};
use macros::export;

type State = bindings::rust_mremap_resize_state;

const TYPE_INVALID: c_uint = bindings::RUST_MREMAP_RESIZE_INVALID;
const TYPE_NO_RESIZE: c_uint = bindings::RUST_MREMAP_RESIZE_NO_RESIZE;
const TYPE_SHRINK: c_uint = bindings::RUST_MREMAP_RESIZE_SHRINK;
const TYPE_EXPAND: c_uint = bindings::RUST_MREMAP_RESIZE_EXPAND;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn error_value(error: c_int) -> usize {
    error as usize
}

/// Expands one prepared mapping, in place when possible.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The remap state and source VMA
/// must remain live until this function returns.
unsafe fn expand(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state. Each helper performs one
    // narrow access or one existing operation.
    unsafe {
        if bindings::rust_helper_mremap_resize_ends_at_vma_end(state)
            && bindings::rust_helper_mremap_resize_is_expandable(state)
        {
            if !bindings::rust_helper_mremap_resize_charge(state) {
                return error_value(-errno(bindings::ENOMEM));
            }

            if !bindings::rust_helper_mremap_resize_merge(state) {
                bindings::rust_helper_mremap_resize_uncharge(state);
                return error_value(-errno(bindings::ENOMEM));
            }

            bindings::rust_helper_mremap_resize_account(state);
            return bindings::rust_helper_mremap_resize_addr(state);
        }

        if !bindings::rust_helper_mremap_resize_is_movable(state) {
            return error_value(-errno(bindings::ENOMEM));
        }

        let err = bindings::rust_helper_mremap_resize_set_new_addr(state);
        if err != 0 {
            return err;
        }

        bindings::rust_helper_mremap_resize_move(state)
    }
}

/// Resizes one prepared mapping and returns its address or an error.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The remap state and source VMA
/// must remain live until this function returns.
unsafe fn mremap_at(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live remap state. Each helper performs one
    // narrow access or one existing operation.
    unsafe {
        match bindings::rust_helper_mremap_resize_type(state) {
            TYPE_NO_RESIZE => bindings::rust_helper_mremap_resize_addr(state),
            TYPE_SHRINK => {
                let err = bindings::rust_helper_mremap_resize_shrink(state);
                if err == 0 {
                    bindings::rust_helper_mremap_resize_addr(state)
                } else {
                    err
                }
            }
            TYPE_EXPAND => expand(state),
            TYPE_INVALID => {
                bindings::rust_helper_mremap_resize_warn_invalid();
                error_value(-errno(bindings::EINVAL))
            }
            _ => {
                bindings::rust_helper_mremap_resize_warn_invalid();
                error_value(-errno(bindings::EINVAL))
            }
        }
    }
}

/// Resizes one prepared mapping and returns its address or an error.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`mremap_at`].
#[export]
unsafe extern "C" fn rust_mremap_resize(state: *mut State) -> usize {
    // SAFETY: The caller upholds the mremap resize requirements.
    unsafe { mremap_at(state) }
}
