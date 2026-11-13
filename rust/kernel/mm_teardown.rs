// SPDX-License-Identifier: GPL-2.0

//! Native final address-space teardown.
//!
//! Rust owns the ordering of subsystem teardown and the conditional `mmlist`
//! and binary-format cleanup decisions. C retains subsystem entry points,
//! diagnostics, locking primitives, and the final `mmdrop()` operation.

use crate::bindings;
use macros::export;

type Mm = bindings::mm_struct;

const FINAL_ACTION_DELETE_MMLIST: u32 = bindings::RUST_MM_FINAL_ACTION_DELETE_MMLIST;
const FINAL_ACTION_PUT_BINFMT: u32 = bindings::RUST_MM_FINAL_ACTION_PUT_BINFMT;

/// Selects conditional cleanup actions for final address-space teardown.
fn final_plan(mmlist_empty: bool, has_binfmt: bool) -> u32 {
    let mut plan = 0;

    if !mmlist_empty {
        plan |= FINAL_ACTION_DELETE_MMLIST;
    }
    if has_binfmt {
        plan |= FINAL_ACTION_PUT_BINFMT;
    }

    plan
}

/// Returns the conditional cleanup action mask for final teardown.
#[cfg(CONFIG_KUNIT)]
#[export]
extern "C" fn rust_mm_final_plan_select(mmlist_empty: bool, has_binfmt: bool) -> u32 {
    final_plan(mmlist_empty, has_binfmt)
}

/// Performs final address-space teardown.
///
/// # Safety
///
/// `mm` must have reached zero `mm_users`. The call must occur in sleepable
/// context and the caller must own the final user reference.
pub(crate) unsafe fn run(mm: *mut Mm) {
    // SAFETY: The caller guarantees that `mm` has no users.
    unsafe { bindings::rust_helper_mm_final_check_users(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_uprobe_clear_state(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_exit_aio(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_ksm_exit(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_khugepaged_exit(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_exit_mmap(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_put_huge_zero_folio(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_set_exe_file_null(mm) };

    // SAFETY: The final user reference is no longer in use.
    let mmlist_empty = unsafe { bindings::rust_helper_mm_final_mmlist_is_empty(mm) };
    // SAFETY: The final user reference is no longer in use.
    let has_binfmt = unsafe { bindings::rust_helper_mm_final_has_binfmt(mm) };
    let plan = final_plan(mmlist_empty, has_binfmt);

    if plan & FINAL_ACTION_DELETE_MMLIST != 0 {
        // SAFETY: The Rust decision selected deletion from the global mmlist.
        unsafe { bindings::rust_helper_mm_final_mmlist_delete(mm) };
    }
    if plan & FINAL_ACTION_PUT_BINFMT != 0 {
        // SAFETY: The Rust decision selected release of the binary-format module.
        unsafe { bindings::rust_helper_mm_final_put_binfmt(mm) };
    }

    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_lru_gen_del_mm(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_futex_hash_free(mm) };
    // SAFETY: The final user reference is no longer in use.
    unsafe { bindings::rust_helper_mm_final_drop(mm) };
}

/// Performs final address-space teardown through the stable C ABI.
///
/// # Safety
///
/// The same requirements as [`run`] apply.
#[export]
unsafe extern "C" fn rust_mm_final(mm: *mut Mm) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { run(mm) };
}
