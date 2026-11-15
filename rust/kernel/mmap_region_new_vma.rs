// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for allocating and installing a new mapping VMA.
//!
//! Rust owns allocation failure handling, file or shmem callback selection,
//! KSM update selection, installation ordering, and cleanup selection. C
//! retains the existing VMA, iterator, file, and mapping operations.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type State = bindings::rust_mmap_create_state;

fn nomem() -> c_int {
    -(bindings::ENOMEM as c_int)
}

/// Allocates and installs a new VMA for a mapping region.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The state and all referenced
/// address-space, file, VMA iterator, and mapping objects must remain valid
/// until this function returns.
unsafe fn do_new_vma(state: *mut State) -> c_int {
    // SAFETY: The C adapter provides a live state for this call.
    unsafe {
        let is_anon = bindings::rust_helper_mmap_new_vma_is_anon(state);

        if bindings::rust_helper_mmap_new_vma_alloc(state).is_null() {
            return nomem();
        }

        bindings::rust_helper_mmap_new_vma_init(state, is_anon);
        if bindings::rust_helper_mmap_new_vma_prealloc(state) != 0 {
            bindings::rust_helper_mmap_new_vma_free(state);
            return nomem();
        }

        let error = if bindings::rust_helper_mmap_new_vma_has_file(state) {
            bindings::rust_helper_mmap_new_vma_map_file(state)
        } else if !is_anon {
            bindings::rust_helper_mmap_new_vma_setup_shmem(state)
        } else {
            0
        };
        if error != 0 {
            bindings::rust_helper_mmap_new_vma_free_iter(state);
            bindings::rust_helper_mmap_new_vma_free(state);
            return error;
        }

        if !bindings::rust_helper_mmap_create_check_ksm_early(state) {
            bindings::rust_helper_mmap_new_vma_update_ksm(state);
        }

        bindings::rust_helper_mmap_new_vma_arch_validate(state);
        bindings::rust_helper_mmap_new_vma_install(state);

        if !bindings::rust_helper_mmap_new_vma_is_anonymous(state) {
            bindings::rust_helper_mmap_new_vma_enter_khugepaged(state);
        }

        0
    }
}

/// Allocates and installs a new VMA for a mapping region.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`do_new_vma`].
#[export]
unsafe extern "C" fn rust_mmap_region_new_vma(state: *mut State) -> c_int {
    // SAFETY: The C adapter upholds the mapping-state requirements.
    unsafe { do_new_vma(state) }
}
