// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for completing a mapping region.
//!
//! Rust owns completion ordering, locked-VMA handling, file callback
//! selection, soft-dirty selection, and page-protection selection. C retains
//! the existing VMA, accounting, unmap, and callback operations.

use crate::bindings;
use macros::export;

type State = bindings::rust_mmap_create_state;

/// Completes installation of a merged or newly allocated mapping VMA.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state and VMA must remain
/// valid until this function returns.
unsafe fn do_complete(state: *mut State) {
    // SAFETY: The C adapter provides a live state and installed VMA.
    unsafe {
        bindings::rust_helper_mmap_complete_perf_event(state);
        bindings::rust_helper_mmap_complete_unmap_overlap(state);
        bindings::rust_helper_mmap_complete_account_stats(state);

        if bindings::rust_helper_mmap_complete_is_locked(state) {
            if bindings::rust_helper_mmap_complete_supports_mlock(state) {
                bindings::rust_helper_mmap_complete_account_locked(state);
            } else {
                bindings::rust_helper_mmap_complete_clear_locked(state);
            }
        }

        if bindings::rust_helper_mmap_complete_has_file(state) {
            bindings::rust_helper_mmap_complete_uprobe(state);
        }

        if bindings::rust_helper_mmap_complete_supports_soft_dirty() {
            bindings::rust_helper_mmap_complete_set_soft_dirty(state);
        }

        bindings::rust_helper_mmap_complete_set_page_prot(state);
    }
}

/// Completes installation of a merged or newly allocated mapping VMA.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_complete`].
#[export]
unsafe extern "C" fn rust_mmap_region_complete(state: *mut State) {
    // SAFETY: The C adapter upholds the mapping-state requirements.
    unsafe { do_complete(state) }
}
