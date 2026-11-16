// SPDX-License-Identifier: GPL-2.0

//! Native forward Maple Tree iteration.
//!
//! Rust owns the forward state normalization, parent ascent, node transition,
//! slot selection, dead-node restart, and overflow policy. C retains encoded
//! parent metadata, node layout, protected slot access, and lockdep checks.

use super::maple_tree_core::{
    ascend, data_end, is_active, is_none, is_overflow, is_ptr, may_activate, may_init_lock_check,
    node_is_dead, node_is_root, node_type, pivots, rewalk, rewalk_if_dead, safe_pivot, slot, slots,
    to_node, warn_on_once, MaState, MapleEnode, MapleNode,
};
use super::maple_tree_walk::walk;
use crate::bindings;
use core::{ffi::c_void, ptr};
use macros::export;

enum SetupResult {
    Ready,
    Done(*mut c_void),
}

/// Prepares a forward walk and returns an early result when possible.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn next_setup(mas: *mut MaState, max: usize) -> SetupResult {
    let was_none = is_none(mas);
    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).last } >= max {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).status = bindings::maple_status_ma_overflow };
        return SetupResult::Done(ptr::null_mut());
    }

    // SAFETY: The caller supplies a valid state.
    match unsafe { (*mas).status } {
        bindings::maple_status_ma_active => return SetupResult::Ready,
        bindings::maple_status_ma_none | bindings::maple_status_ma_pause => {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_start };
            // SAFETY: The state is protected by the caller.
            unsafe { walk(mas) };
        }
        bindings::maple_status_ma_start => {
            // SAFETY: The state is protected by the caller.
            unsafe { walk(mas) };
        }
        bindings::maple_status_ma_overflow => {
            // SAFETY: The state is protected by the caller.
            unsafe { may_activate(mas) };
        }
        bindings::maple_status_ma_underflow => {
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

    if is_active(mas) {
        return SetupResult::Ready;
    }

    if is_ptr(mas) {
        if was_none {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).index } == 0 {
                // SAFETY: A single root entry covers index zero.
                unsafe {
                    (*mas).index = 0;
                    (*mas).last = 0;
                }
                return SetupResult::Done(ptr::null_mut());
            }
        }

        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).index = 1;
            (*mas).last = usize::MAX;
            (*mas).status = bindings::maple_status_ma_none;
        }
        return SetupResult::Done(ptr::null_mut());
    }

    if is_none(mas) {
        return SetupResult::Done(ptr::null_mut());
    }

    SetupResult::Ready
}

/// Moves to the next peer node and updates the covered range.
///
/// # Safety
///
/// `mas` must contain a valid active state protected by RCU or the tree lock.
/// `node` must be the raw node of `mas`.
unsafe fn next_node(mas: *mut MaState, node: *mut MapleNode, max: usize) -> bool {
    // SAFETY: The caller supplies a valid active state.
    if unsafe { (*mas).max } >= max {
        // SAFETY: The current node was reached under the caller's protection.
        if unsafe { node_is_dead(node) } {
            return true;
        }
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).status = bindings::maple_status_ma_overflow };
        return false;
    }

    // SAFETY: The caller supplies a valid active state.
    let min = unsafe { (*mas).max } + 1;
    let mut level = 0usize;
    let mut current = node;
    let mut current_type;
    let mut current_pivots;

    loop {
        // SAFETY: The current node was reached under the caller's protection.
        if unsafe { node_is_root(current) } {
            // SAFETY: The current node was reached under the caller's protection.
            if unsafe { node_is_dead(current) } {
                return true;
            }
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).status = bindings::maple_status_ma_overflow };
            return false;
        }

        // SAFETY: The state is protected by the caller.
        if unsafe { ascend(mas) } {
            return true;
        }
        level += 1;
        // SAFETY: Ascend installed a valid active encoded node.
        current = unsafe { to_node((*mas).node) };
        // SAFETY: The encoded node is live.
        current_type = unsafe { node_type((*mas).node) };
        // SAFETY: The node and type identify the same live node.
        current_pivots = unsafe { pivots(current, current_type) };
        // SAFETY: The caller supplies a valid active state.
        let node_end = unsafe { data_end(current, current_type, current_pivots, (*mas).max) };

        // SAFETY: The node was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }
        // SAFETY: The caller supplies a valid state.
        if unsafe { (*mas).offset } != node_end {
            break;
        }
    }

    // SAFETY: The node and type identify the same live node.
    let mut current_slots = unsafe { slots(current, current_type) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).offset += 1 };
    // SAFETY: The incremented offset is a valid peer slot.
    let mut next = unsafe { slot(mas, current_slots, (*mas).offset) }.cast::<MapleEnode>();
    // SAFETY: The node was reached under the caller's protection.
    if unsafe { node_is_dead(current) } {
        return true;
    }

    if level > 1 {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).offset = 0 };
    }

    while level > 1 {
        level -= 1;
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).node = next };
        // SAFETY: The selected child is a valid encoded node.
        current = unsafe { to_node(next) };
        // SAFETY: The selected encoded node is live.
        current_type = unsafe { node_type(next) };
        // SAFETY: The node and type identify the same live node.
        current_slots = unsafe { slots(current, current_type) };
        // SAFETY: The first slot is valid in a traversed child.
        next = unsafe { slot(mas, current_slots, 0) }.cast::<MapleEnode>();
        // SAFETY: The node was reached under the caller's protection.
        if unsafe { node_is_dead(current) } {
            return true;
        }
    }

    // SAFETY: The caller supplies a valid writable state.
    if unsafe { (*mas).offset } == 0 {
        // SAFETY: The node and type identify the same live node.
        current_pivots = unsafe { pivots(current, current_type) };
    }

    // SAFETY: The current state and node are valid.
    let new_max = unsafe { safe_pivot(mas, current_pivots, (*mas).offset, current_type) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).max = new_max };
    // SAFETY: The next encoded node is live.
    let next_node = unsafe { to_node(next) };
    // SAFETY: The next encoded node is live.
    let next_type = unsafe { node_type(next) };
    // SAFETY: The next node and type identify the same live node.
    let next_pivots = unsafe { pivots(next_node, next_type) };
    // SAFETY: The next node and protected range are valid.
    let next_end = unsafe { data_end(next_node, next_type, next_pivots, new_max) };
    // SAFETY: The parent node was reached under the caller's protection.
    if unsafe { node_is_dead(current) } {
        return true;
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe {
        (*mas).end = next_end;
        (*mas).node = next;
        (*mas).min = min;
    }
    false
}

/// Selects the next forward entry or range.
///
/// # Safety
///
/// `mas` must contain a valid active state protected by RCU or the tree lock.
pub(super) unsafe fn next_slot(mas: *mut MaState, max: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid active state.
    let save_point = unsafe { (*mas).last };

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
        if unsafe { (*mas).max } >= max {
            // SAFETY: The caller supplies a valid state.
            let offset = unsafe { (*mas).offset };
            // SAFETY: The caller supplies a valid state.
            let end = unsafe { (*mas).end };
            let pivot = if offset < end {
                // SAFETY: The offset is within the pivot array.
                unsafe { *node_pivots.add(usize::from(offset)) }
            } else {
                // SAFETY: The caller supplies a valid active state.
                unsafe { (*mas).max }
            };

            // SAFETY: The state and node are protected by the caller.
            if unsafe { rewalk_if_dead(mas, node, save_point) } {
                continue 'retry;
            }
            if pivot >= max {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_overflow };
                return ptr::null_mut();
            }
        }

        // SAFETY: The caller supplies a valid state.
        let mut offset = unsafe { (*mas).offset };
        // SAFETY: The caller supplies a valid state.
        let mut end = unsafe { (*mas).end };
        let mut last;
        if offset < end {
            // SAFETY: The offset is within the pivot array.
            let index = unsafe { *node_pivots.add(usize::from(offset)) } + 1;
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).index = index };
            offset += 1;
            last = if offset < end {
                // SAFETY: The incremented offset is within the pivot array.
                unsafe { *node_pivots.add(usize::from(offset)) }
            } else {
                // SAFETY: The caller supplies a valid active state.
                unsafe { (*mas).max }
            };
        } else {
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).last } >= max {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_overflow };
                return ptr::null_mut();
            }

            // SAFETY: The state and current node are protected by the caller.
            if unsafe { next_node(mas, node, max) } {
                // SAFETY: The protected state can restart at the saved index.
                unsafe { rewalk(mas, save_point) };
                continue 'retry;
            }
            if warn_on_once(is_overflow(mas)) {
                return ptr::null_mut();
            }

            // SAFETY: Next-node selected a valid active state.
            node = unsafe { to_node((*mas).node) };
            // SAFETY: Next-node installed a valid encoded node.
            current_type = unsafe { node_type((*mas).node) };
            // SAFETY: The node and type identify the same live node.
            node_pivots = unsafe { pivots(node, current_type) };
            // SAFETY: Next-node installed the new node end.
            end = unsafe { (*mas).end };
            offset = 0;
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).index = (*mas).min };
            // SAFETY: The first pivot is valid in the selected node.
            last = unsafe { *node_pivots };
        }

        loop {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).offset = offset;
                (*mas).last = last;
            }

            // SAFETY: The node and type identify the same live node.
            let node_slots = unsafe { slots(node, current_type) };
            // SAFETY: The selected offset is within the live slot array.
            let entry = unsafe { slot(mas, node_slots, offset) };
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
            if last >= max {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { (*mas).status = bindings::maple_status_ma_overflow };
                return ptr::null_mut();
            }

            // SAFETY: The next iteration starts after this range.
            unsafe { (*mas).index = last + 1 };
            offset += 1;
            last = if offset < end {
                // SAFETY: The incremented offset is within the pivot array.
                unsafe { *node_pivots.add(usize::from(offset)) }
            } else {
                // SAFETY: The caller supplies a valid active state.
                unsafe { (*mas).max }
            };
        }
    }
}

/// Implements a forward Maple Tree iteration.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
unsafe fn next(mas: *mut MaState, max: usize, empty: bool) -> *mut c_void {
    // SAFETY: The caller supplies a valid initialized state.
    unsafe { may_init_lock_check(mas) };

    // SAFETY: The caller supplies a valid protected state.
    match unsafe { next_setup(mas, max) } {
        SetupResult::Done(entry) => entry,
        SetupResult::Ready => {
            // SAFETY: The setup produced an active protected state.
            unsafe { next_slot(mas, max, empty) }
        }
    }
}

/// Advances to the next Maple Tree entry.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_next(mas: *mut MaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { next(mas, max, false) }
}

/// Advances to the next Maple Tree range.
///
/// # Safety
///
/// The C caller must provide a valid state and protection.
#[export]
unsafe extern "C" fn rust_mas_next_range(mas: *mut MaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { next(mas, max, true) }
}

/// Moves a C-visible Maple state to its parent.
///
/// # Safety
///
/// The C caller must provide a valid active state and protection.
#[export]
unsafe extern "C" fn rust_mas_ascend(mas: *mut MaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { i32::from(ascend(mas)) }
}

/// Moves a C-visible Maple state to the next peer node.
///
/// # Safety
///
/// The C caller must provide a valid active state and protection.
#[export]
unsafe extern "C" fn rust_mas_next_node(
    mas: *mut MaState,
    node: *mut MapleNode,
    max: usize,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { i32::from(next_node(mas, node, max)) }
}

/// Selects the next slot for a C-visible Maple state.
///
/// # Safety
///
/// The C caller must provide a valid active state and protection.
#[export]
unsafe extern "C" fn rust_mas_next_slot(mas: *mut MaState, max: usize, empty: bool) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { next_slot(mas, max, empty) }
}
