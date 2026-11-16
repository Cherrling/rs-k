// SPDX-License-Identifier: GPL-2.0

//! Native forward Maple Tree finds.
//!
//! Rust owns forward find state normalization, first-entry inclusion, and
//! subsequent-entry exclusion. C retains encoded parent metadata, node
//! layout, protected slot access, and lockdep checks.

use super::maple_tree_core::{
    is_none, is_ptr, is_start, may_activate, may_init_lock_check, MaState,
};
use super::maple_tree_next::next_slot;
use super::maple_tree_walk::walk;
use crate::bindings;
use core::{ffi::c_void, ptr};
use macros::export;

enum SetupResult {
    Ready,
    Done(*mut c_void),
}

/// Prepares a forward find and returns an early result when possible.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn find_setup(mas: *mut MaState, max: usize) -> SetupResult {
    // SAFETY: The caller supplies a valid state.
    match unsafe { (*mas).status } {
        bindings::maple_status_ma_active => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).last } < max {
                return SetupResult::Ready;
            }
            return SetupResult::Done(ptr::null_mut());
        }
        bindings::maple_status_ma_start => {}
        bindings::maple_status_ma_pause => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).last } >= max {
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).index = (*mas).last + 1;
                (*mas).status = bindings::maple_status_ma_start;
            }
        }
        bindings::maple_status_ma_none => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).last } >= max {
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).index = (*mas).last;
                (*mas).status = bindings::maple_status_ma_start;
            }
        }
        bindings::maple_status_ma_underflow => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } >= max {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_overflow };
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The state is protected by the caller.
            unsafe { may_activate(mas) };
            // SAFETY: The state is protected by the caller.
            let entry = unsafe { walk(mas) };
            if !entry.is_null() {
                return SetupResult::Done(entry);
            }
        }
        bindings::maple_status_ma_overflow => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).last } >= max {
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The state is protected by the caller.
            unsafe { may_activate(mas) };
            // SAFETY: The state is protected by the caller.
            let entry = unsafe { walk(mas) };
            if !entry.is_null() {
                return SetupResult::Done(entry);
            }
        }
        bindings::maple_status_ma_root => {}
        bindings::maple_status_ma_error => return SetupResult::Done(ptr::null_mut()),
        _ => {}
    }

    if is_start(mas) {
        // SAFETY: The caller supplies a valid state.
        if unsafe { (*mas).index } > max {
            return SetupResult::Done(ptr::null_mut());
        }
        // SAFETY: The state is protected by the caller.
        let entry = unsafe { walk(mas) };
        if !entry.is_null() {
            return SetupResult::Done(entry);
        }
    }

    if is_ptr(mas) {
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).status = bindings::maple_status_ma_none;
            (*mas).index = 1;
            (*mas).last = usize::MAX;
        }
        return SetupResult::Done(ptr::null_mut());
    }

    if is_none(mas) {
        return SetupResult::Done(ptr::null_mut());
    }

    // SAFETY: The caller supplies a valid active state.
    if unsafe { (*mas).index } == max {
        return SetupResult::Done(ptr::null_mut());
    }

    SetupResult::Ready
}

/// Implements a forward Maple Tree find.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn find(mas: *mut MaState, max: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid initialized state.
    unsafe { may_init_lock_check(mas) };

    // SAFETY: The caller supplies a valid protected state.
    match unsafe { find_setup(mas, max) } {
        SetupResult::Done(entry) => entry,
        SetupResult::Ready => {
            // SAFETY: The setup produced an active protected state.
            let entry = unsafe { next_slot(mas, max, empty) };
            if !empty {
                // SAFETY: A forward find ignores the iterator overflow state.
                unsafe { (*mas).status = bindings::maple_status_ma_active };
            }
            entry
        }
    }
}

/// Finds the next Maple Tree entry.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_find(mas: *mut MaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find(mas, max, false) }
}

/// Finds the next Maple Tree range.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_find_range(mas: *mut MaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find(mas, max, true) }
}
