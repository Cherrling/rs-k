// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for creating one VMA region.
//!
//! Rust owns the preparation, error selection, writable-file cleanup, core
//! operation dispatch, and final validation order. C retains the existing
//! region implementation and low-level predicates and address-space
//! operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_region_state;

fn error_value(error: c_int) -> usize {
    error as usize
}

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Creates one mapping region.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state, file, VMA iterator,
/// userfaultfd list, and address space must remain valid until this function
/// returns.
unsafe fn do_mmap_region(state: *mut State) -> usize {
    // SAFETY: The caller supplies a live region state. Each helper performs one
    // narrow projection, predicate, accounting transition, or existing
    // operation.
    unsafe {
        bindings::rust_helper_mmap_region_assert_write_locked(state);

        if bindings::rust_helper_mmap_region_deny_write_exec(state) {
            return error_value(-errno(bindings::EACCES));
        }

        if !bindings::rust_helper_mmap_region_arch_valid(state) {
            return error_value(-errno(bindings::EINVAL));
        }

        let writable_file_mapping = bindings::rust_helper_mmap_region_shared_maywrite(state);
        if writable_file_mapping {
            let error = bindings::rust_helper_mmap_region_map_writable(state);
            if error != 0 {
                return error as usize;
            }
        }

        bindings::rust_helper_mmap_region_create(state);
        if writable_file_mapping {
            bindings::rust_helper_mmap_region_unmap_writable(state);
        }
        bindings::rust_helper_mmap_region_validate_mm(state);

        bindings::rust_helper_mmap_region_ret(state)
    }
}

/// Creates one mapping region.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mmap_region`].
#[export]
unsafe extern "C" fn rust_mmap_region(state: *mut State) -> usize {
    // SAFETY: The caller upholds the region requirements.
    unsafe { do_mmap_region(state) }
}
