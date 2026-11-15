// SPDX-License-Identifier: GPL-2.0

//! Native orchestration for moving one page-table entry.
//!
//! Rust owns the rmap-lock decision, entry dispatch, lock ordering, and result
//! propagation. C retains the architecture-specific page-table updates and the
//! existing normal and huge-page move operations.

use crate::bindings;
use core::ffi::c_void;
use macros::export;

type State = bindings::rust_move_pgt_entry_state;

const NORMAL_PMD: u32 = bindings::pgt_entry_NORMAL_PMD;
const HPAGE_PMD: u32 = bindings::pgt_entry_HPAGE_PMD;
const NORMAL_PUD: u32 = bindings::pgt_entry_NORMAL_PUD;
const HPAGE_PUD: u32 = bindings::pgt_entry_HPAGE_PUD;

fn needs_rmap_locks(state: *mut State, entry: u32) -> bool {
    match entry {
        NORMAL_PMD | NORMAL_PUD => true,
        _ => {
            // SAFETY: The caller supplies a live page-table move state.
            unsafe { bindings::rust_helper_move_pgt_entry_need_rmap_locks(state) }
        }
    }
}

/// Moves one page-table entry and applies the required rmap locking.
///
/// # Safety
///
/// The caller must hold the mmap write lock. The move state and both page-table
/// entries must remain valid until this function returns.
unsafe fn move_pgt_entry(
    state: *mut State,
    entry: u32,
    old_entry: *mut c_void,
    new_entry: *mut c_void,
) -> bool {
    // SAFETY: The caller satisfies the page-table move requirements.
    unsafe {
        let rmap_locks = needs_rmap_locks(state, entry);
        if rmap_locks {
            bindings::rust_helper_move_pgt_entry_take_rmap_locks(state);
        }

        let moved = match entry {
            NORMAL_PMD => {
                bindings::rust_helper_move_pgt_entry_normal_pmd(state, old_entry, new_entry)
            }
            NORMAL_PUD => {
                bindings::rust_helper_move_pgt_entry_normal_pud(state, old_entry, new_entry)
            }
            HPAGE_PMD => bindings::rust_helper_move_pgt_entry_huge_pmd(state, old_entry, new_entry),
            HPAGE_PUD => bindings::rust_helper_move_pgt_entry_huge_pud(state, old_entry, new_entry),
            _ => {
                bindings::rust_helper_move_pgt_entry_warn_invalid();
                false
            }
        };

        if rmap_locks {
            bindings::rust_helper_move_pgt_entry_drop_rmap_locks(state);
        }

        moved
    }
}

/// Moves one page-table entry and applies the required rmap locking.
///
/// # Safety
///
/// The caller must satisfy the requirements of [`move_pgt_entry`].
#[export]
unsafe extern "C" fn rust_move_pgt_entry(
    state: *mut State,
    entry: u32,
    old_entry: *mut c_void,
    new_entry: *mut c_void,
) -> bool {
    // SAFETY: The caller upholds the page-table move requirements.
    unsafe { move_pgt_entry(state, entry, old_entry, new_entry) }
}
