// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for VMA region setup.
//!
//! Rust owns overlapping-VMA collection, address-space limit selection,
//! memory accounting, cleanup selection, and descriptor synchronization. C
//! retains the existing VMA, maple-tree, accounting, cleanup, and descriptor
//! operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_create_state;

fn errno(error: u32) -> c_int {
    error as c_int
}

/// Prepares one mapping region and records the state needed for completion.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state and all referenced
/// address-space, VMA iterator, and userfaultfd objects must remain valid until
/// this function returns.
unsafe fn do_mmap_setup(state: *mut State) -> c_int {
    // SAFETY: The C adapter provides a live state for the duration of this call.
    unsafe {
        let overlapping = bindings::rust_helper_mmap_setup_find_overlap(state);
        bindings::rust_helper_mmap_setup_init_unmap(state);

        if overlapping {
            bindings::rust_helper_mmap_setup_init_detach(state);
            let error = bindings::rust_helper_mmap_setup_gather(state);
            if error != 0 {
                bindings::rust_helper_mmap_setup_clear_unmap_pages(state);
                return error;
            }
            bindings::rust_helper_mmap_setup_adopt_unmap_neighbors(state);
        } else {
            bindings::rust_helper_mmap_setup_select_adjacent(state);
        }

        if !bindings::rust_helper_mmap_setup_may_expand(state) {
            return -errno(bindings::ENOMEM);
        }

        if bindings::rust_helper_mmap_setup_accountable(state) {
            let uncharged = bindings::rust_helper_mmap_setup_uncharged(state);
            if uncharged != 0 {
                let error = bindings::rust_helper_mmap_setup_charge(state, uncharged);
                if error != 0 {
                    return error;
                }
            }
            bindings::rust_helper_mmap_setup_commit_accounting(state);
        }

        bindings::rust_helper_mmap_setup_clean_area(state);
        bindings::rust_helper_mmap_setup_sync_desc(state);
        0
    }
}

/// Prepares one mapping region.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_mmap_setup`].
#[export]
unsafe extern "C" fn rust_mmap_region_setup(state: *mut State) -> c_int {
    // SAFETY: The C adapter upholds the region-state requirements.
    unsafe { do_mmap_setup(state) }
}
