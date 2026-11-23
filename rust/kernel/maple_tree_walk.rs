// SPDX-License-Identifier: GPL-2.0

//! Native stateful Maple Tree walk.
//!
//! Rust owns state normalization, start-state traversal, range selection,
//! dead-node restart, and the `mas_walk()` result transitions. C retains the
//! encoded-node primitives, node layout, protected slot access, traversal
//! metadata, and lockdep checks.

use super::maple_tree_core::{
    is_none, is_ptr, is_start, make_walkable, may_init_lock_check, state_walk, MaState,
};
use core::{ffi::c_void, ptr};
use macros::export;

/// Implements the complete public stateful walk operation.
///
/// # Safety
///
/// `mas` must point to a valid initialized Maple state. The caller must hold
/// the RCU read lock or the Maple tree lock.
pub(crate) unsafe fn walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { may_init_lock_check(mas) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe { make_walkable(mas) };

    loop {
        // SAFETY: The state is valid and protected by the caller.
        let entry = unsafe { state_walk(mas) };

        if is_start(mas) {
            continue;
        }

        if is_none(mas) {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).index = 0;
                (*mas).last = usize::MAX;
            }
        } else if is_ptr(mas) {
            // SAFETY: The caller supplies a valid writable state.
            if unsafe { (*mas).index } == 0 {
                // SAFETY: A single-entry root covers index zero.
                unsafe { (*mas).last = 0 };
                return entry;
            }

            // SAFETY: The out-of-range state starts after the root entry.
            unsafe {
                (*mas).index = 1;
                (*mas).last = usize::MAX;
                (*mas).status = crate::bindings::maple_status_ma_none;
            }
            return ptr::null_mut();
        }

        return entry;
    }
}

/// Walks a Maple Tree state through the production ABI.
///
/// # Safety
///
/// The caller must provide a valid Maple state and the required RCU or tree
/// lock protection.
#[export]
unsafe extern "C" fn mas_walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { walk(mas) }
}
