// SPDX-License-Identifier: GPL-2.0

//! Native reverse Maple Tree iteration.
//!
//! Rust owns reverse state normalization, parent ascent, node transition,
//! slot selection, dead-node restart, and underflow policy. C retains encoded
//! parent metadata, node layout, protected slot access, and lockdep checks.

use super::maple_tree_core::{
    ascend, data_end, enode_is_dead, is_none, is_ptr, is_start, may_activate, may_init_lock_check,
    node_is_dead, node_is_root, node_type, pivots, rewalk, rewalk_if_dead, root, safe_min, slot,
    slots, to_node, warn_on_once, MaState, MapleEnode,
};
use super::maple_tree_walk::walk;
use crate::bindings;
use core::{ffi::c_void, ptr};
use macros::export;

enum SetupResult {
    Ready,
    Done(*mut c_void),
}

/// Marks a Maple state as underflowed and returns the public sentinel.
///
/// # Safety
///
/// `mas` must contain a valid writable state.
unsafe fn mark_underflow(mas: *mut MaState) -> *mut c_void {
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).status = bindings::maple_status_ma_underflow };
    ptr::null_mut()
}

/// Prepares a reverse walk and returns an early result when possible.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn prev_setup(mas: *mut MaState, min: usize) -> SetupResult {
    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).index } <= min {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).status = bindings::maple_status_ma_underflow };
        return SetupResult::Done(ptr::null_mut());
    }

    // SAFETY: The caller supplies a valid state.
    match unsafe { (*mas).status } {
        bindings::maple_status_ma_active => return SetupResult::Ready,
        bindings::maple_status_ma_start => {}
        bindings::maple_status_ma_none | bindings::maple_status_ma_pause => {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_start };
        }
        bindings::maple_status_ma_underflow => {
            // SAFETY: The state is protected by the caller.
            unsafe { may_activate(mas) };
        }
        bindings::maple_status_ma_overflow => {
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
        // SAFETY: The state is protected by the caller.
        unsafe { walk(mas) };
    }

    if is_ptr(mas) {
        // SAFETY: The caller supplies a valid state.
        if unsafe { (*mas).index } == 0 {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_none };
            return SetupResult::Done(ptr::null_mut());
        }

        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).index = 0;
            (*mas).last = 0;
        }
        // SAFETY: The single-entry root is protected by the caller.
        return SetupResult::Done(unsafe { root(mas) });
    }

    if is_none(mas) {
        // SAFETY: The caller supplies a valid state.
        if unsafe { (*mas).index } != 0 {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).index = 0;
                (*mas).last = 0;
                (*mas).status = bindings::maple_status_ma_root;
            }
            // SAFETY: The tree root is protected by the caller.
            return SetupResult::Done(unsafe { root(mas) });
        }
        return SetupResult::Done(ptr::null_mut());
    }

    SetupResult::Ready
}

/// Moves to the previous peer node and updates the covered range.
///
/// # Safety
///
/// `mas` must contain a valid active state protected by RCU or the tree lock.
unsafe fn prev_node(mas: *mut MaState, min: usize) -> bool {
    // SAFETY: The caller supplies a valid active state.
    let mut current = unsafe { to_node((*mas).node) };

    // SAFETY: The caller supplies a valid active state.
    if unsafe { (*mas).min } == 0 {
        // SAFETY: The current node was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).status = bindings::maple_status_ma_underflow };
        return false;
    }

    // SAFETY: The caller supplies a valid active state.
    let max = unsafe { (*mas).min } - 1;
    if max < min {
        // SAFETY: The current node was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).status = bindings::maple_status_ma_underflow };
        return false;
    }

    let mut level = 0usize;
    let mut current_type;
    loop {
        // SAFETY: The current node was reached under the caller's protection.
        if unsafe { node_is_root(current) } {
            // SAFETY: The current node was reached under the caller's protection.
            if unsafe { node_is_dead(current) } {
                return true;
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_underflow };
            return false;
        }

        // SAFETY: The state is protected by the caller.
        if unsafe { ascend(mas) } {
            return true;
        }
        level += 1;
        // SAFETY: Ascend installed a valid active encoded node.
        current = unsafe { to_node((*mas).node) };
        // SAFETY: The caller supplies a valid active state.
        if unsafe { (*mas).offset } != 0 {
            break;
        }
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).offset -= 1 };
    // SAFETY: The caller supplies a valid active state.
    let mut offset = unsafe { (*mas).offset };
    // SAFETY: The active encoded node is live.
    current_type = unsafe { node_type((*mas).node) };

    while level > 1 {
        level -= 1;
        // SAFETY: The node and type identify the same live node.
        let current_slots = unsafe { slots(current, current_type) };
        // SAFETY: The selected offset is within the parent slot array.
        unsafe { (*mas).node = slot(mas, current_slots, offset).cast::<MapleEnode>() };
        // SAFETY: The parent was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }

        // SAFETY: The selected encoded node is live.
        current_type = unsafe { node_type((*mas).node) };
        // SAFETY: The selected encoded node is live.
        current = unsafe { to_node((*mas).node) };
        // SAFETY: The child and type identify the same live node.
        let current_pivots = unsafe { pivots(current, current_type) };
        // SAFETY: The child and protected range describe a live traversal.
        offset = unsafe { data_end(current, current_type, current_pivots, max) };
        // SAFETY: The child was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }
    }

    // SAFETY: The node and type identify the same live node.
    let current_slots = unsafe { slots(current, current_type) };
    // SAFETY: The selected offset is within the parent slot array.
    unsafe { (*mas).node = slot(mas, current_slots, offset).cast::<MapleEnode>() };
    // SAFETY: The node and type identify the same live node.
    let current_pivots = unsafe { pivots(current, current_type) };
    // SAFETY: The parent was reached under the caller's protection.
    if unsafe { node_is_dead(current) } {
        return true;
    }

    if offset != 0 {
        // SAFETY: A nonzero offset has a preceding pivot.
        let new_min = unsafe { *current_pivots.add(usize::from(offset - 1)) } + 1;
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).min = new_min };
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).max = max };
    // SAFETY: The selected encoded node is live.
    let selected = unsafe { to_node((*mas).node) };
    // SAFETY: The selected encoded node is live.
    let selected_type = unsafe { node_type((*mas).node) };
    // SAFETY: The selected node and type identify the same live node.
    let selected_pivots = unsafe { pivots(selected, selected_type) };
    // SAFETY: The selected node and protected range describe a live traversal.
    let end = unsafe { data_end(selected, selected_type, selected_pivots, (*mas).max) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe {
        (*mas).offset = end;
        (*mas).end = end;
    }
    // SAFETY: The selected encoded node was acquired under the caller's protection.
    unsafe { enode_is_dead((*mas).node) }
}

/// Selects the previous reverse entry or range.
///
/// # Safety
///
/// `mas` must contain a valid active state protected by RCU or the tree lock.
unsafe fn prev_slot(mas: *mut MaState, min: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid active state.
    let save_point = unsafe { (*mas).index };

    'retry: loop {
        // SAFETY: The caller supplies a valid active state.
        let mut node = unsafe { to_node((*mas).node) };
        // SAFETY: The caller supplies a valid active encoded node.
        let mut current_type = unsafe { node_type((*mas).node) };
        // SAFETY: The node and type identify the same live node.
        let mut node_pivots = unsafe { pivots(node, current_type) };

        // SAFETY: The state and node are protected by the caller.
        if unsafe { rewalk_if_dead(mas, node, save_point) } {
            continue 'retry;
        }

        // SAFETY: The caller supplies a valid active state.
        if unsafe { (*mas).min } <= min {
            // SAFETY: The caller supplies a valid state.
            let offset = unsafe { (*mas).offset };
            // SAFETY: The current node and offset identify a valid range.
            let pivot = unsafe { safe_min(mas, node_pivots, offset) };

            // SAFETY: The state and node are protected by the caller.
            if unsafe { rewalk_if_dead(mas, node, save_point) } {
                continue 'retry;
            }
            if pivot <= min {
                // SAFETY: The caller supplies a valid writable state.
                return unsafe { mark_underflow(mas) };
            }
        }

        loop {
            // SAFETY: The caller supplies a valid active state.
            if unsafe { (*mas).offset } != 0 {
                // SAFETY: The caller supplies a valid writable state.
                unsafe {
                    (*mas).offset -= 1;
                    (*mas).last = (*mas).index - 1;
                    (*mas).index = safe_min(mas, node_pivots, (*mas).offset);
                }
            } else {
                // SAFETY: The caller supplies a valid state.
                if unsafe { (*mas).index } <= min {
                    // SAFETY: The caller supplies a valid writable state.
                    return unsafe { mark_underflow(mas) };
                }

                // SAFETY: The state and current node are protected by the caller.
                if unsafe { prev_node(mas, min) } {
                    // SAFETY: The protected state can restart at the saved index.
                    unsafe { rewalk(mas, save_point) };
                    continue 'retry;
                }
                // SAFETY: The caller supplies a valid active state.
                if warn_on_once(unsafe { (*mas).status } == bindings::maple_status_ma_underflow) {
                    return ptr::null_mut();
                }

                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).last = (*mas).max };
                // SAFETY: Previous-node selected a valid active state.
                node = unsafe { to_node((*mas).node) };
                // SAFETY: Previous-node installed a valid encoded node.
                current_type = unsafe { node_type((*mas).node) };
                // SAFETY: The node and type identify the same live node.
                node_pivots = unsafe { pivots(node, current_type) };
                // SAFETY: Previous-node installs a nonzero slot offset.
                unsafe {
                    (*mas).index = *node_pivots.add(usize::from((*mas).offset - 1)) + 1;
                }
            }

            // SAFETY: The node and type identify the same live node.
            let node_slots = unsafe { slots(node, current_type) };
            // SAFETY: The selected offset is within the live slot array.
            let entry = unsafe { slot(mas, node_slots, (*mas).offset) };
            // SAFETY: The state and node are protected by the caller.
            if unsafe { rewalk_if_dead(mas, node, save_point) } {
                continue 'retry;
            }
            if !entry.is_null() {
                return entry;
            }
            if empty {
                return ptr::null_mut();
            }
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } <= min {
                // SAFETY: The caller supplies a valid writable state.
                return unsafe { mark_underflow(mas) };
            }
        }
    }
}

/// Implements a reverse Maple Tree iteration.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn prev(mas: *mut MaState, min: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid initialized state.
    unsafe { may_init_lock_check(mas) };

    // SAFETY: The caller supplies a valid protected state.
    match unsafe { prev_setup(mas, min) } {
        SetupResult::Done(entry) => entry,
        SetupResult::Ready => {
            // SAFETY: The setup produced an active protected state.
            unsafe { prev_slot(mas, min, empty) }
        }
    }
}

/// Prepares a reverse find and returns an early result when possible.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn find_rev_setup(mas: *mut MaState, min: usize) -> SetupResult {
    // SAFETY: The caller supplies a valid state.
    match unsafe { (*mas).status } {
        bindings::maple_status_ma_active => {}
        bindings::maple_status_ma_start => {}
        bindings::maple_status_ma_pause => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } <= min {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_underflow };
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).index -= 1;
                (*mas).last = (*mas).index;
                (*mas).status = bindings::maple_status_ma_start;
            }
        }
        bindings::maple_status_ma_none => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } <= min {
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).last = (*mas).index;
                (*mas).status = bindings::maple_status_ma_start;
            }
        }
        bindings::maple_status_ma_overflow => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } <= min {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_underflow };
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_active };
        }
        bindings::maple_status_ma_underflow => {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } <= min {
                return SetupResult::Done(ptr::null_mut());
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_active };
        }
        bindings::maple_status_ma_root => {}
        bindings::maple_status_ma_error => return SetupResult::Done(ptr::null_mut()),
        _ => {}
    }

    if is_start(mas) {
        // SAFETY: The caller supplies a valid state.
        if unsafe { (*mas).index } < min {
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
        unsafe { (*mas).status = bindings::maple_status_ma_none };
        return SetupResult::Done(ptr::null_mut());
    }

    if is_none(mas) {
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).index = 0;
            (*mas).last = 0;
            (*mas).status = bindings::maple_status_ma_root;
        }
        // SAFETY: The tree root is protected by the caller.
        return SetupResult::Done(unsafe { root(mas) });
    }

    // SAFETY: The caller supplies a valid active state.
    if unsafe { (*mas).index } < min {
        return SetupResult::Done(ptr::null_mut());
    }
    SetupResult::Ready
}

/// Implements a reverse Maple Tree find.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn find_rev(mas: *mut MaState, min: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid initialized state.
    unsafe { may_init_lock_check(mas) };

    // SAFETY: The caller supplies a valid protected state.
    match unsafe { find_rev_setup(mas, min) } {
        SetupResult::Done(entry) => entry,
        SetupResult::Ready => {
            // SAFETY: The setup produced an active protected state.
            unsafe { prev_slot(mas, min, empty) }
        }
    }
}

/// Advances to the previous Maple Tree entry.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_prev(mas: *mut MaState, min: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prev(mas, min, false) }
}

/// Advances to the previous Maple Tree range.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_prev_range(mas: *mut MaState, min: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prev(mas, min, true) }
}

/// Finds the previous Maple Tree entry.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_find_rev(mas: *mut MaState, min: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_rev(mas, min, false) }
}

/// Finds the previous Maple Tree range.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_find_range_rev(mas: *mut MaState, min: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_rev(mas, min, true) }
}

/// Moves a C-visible Maple state to its previous peer node.
///
/// # Safety
///
/// The C caller must provide a valid active state and protection.
#[export]
unsafe extern "C" fn rust_mas_prev_node(mas: *mut MaState, min: usize) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { i32::from(prev_node(mas, min)) }
}

/// Selects the previous slot for a C-visible Maple state.
///
/// # Safety
///
/// The C caller must provide a valid active state and protection.
#[export]
unsafe extern "C" fn rust_mas_prev_slot(mas: *mut MaState, min: usize, empty: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { prev_slot(mas, min, empty) }
}
