// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree write dispatcher and in-place store strategies.
//!
//! Rust owns store-type dispatch and the exact-fit, append, and slot-store
//! mutations. C retains node layout, RCU publication, protected slot access,
//! metadata layout, gap propagation, tracepoint delivery, and the complex
//! strategy implementations.

use super::maple_tree_core::{MaState, MaWrState, pivot_count};
use crate::bindings;
use core::ffi::c_void;
use macros::export;

/// Publishes an entry into one Maple Tree slot.
///
/// # Safety
///
/// `slot` must be valid and the caller must hold the tree write lock or an
/// equivalent lock accepted by the Maple Tree RCU annotations.
#[inline(always)]
unsafe fn store_slot(slot: *mut *mut c_void, entry: *mut c_void) {
    // SAFETY: The caller guarantees a valid protected slot.
    unsafe { bindings::rust_maple_tree_store_slot(slot, entry) };
}

/// Reads one slot while holding the Maple Tree write lock.
///
/// # Safety
///
/// `mas` must identify a valid tree and `slots` must belong to its locked
/// write state. `offset` must identify a valid slot.
#[inline(always)]
unsafe fn slot_locked(mas: *mut MaState, slots: *mut *mut c_void, offset: u8) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_slot_locked(mas, slots, offset) }
}

/// Updates the end and largest-gap metadata of a Maple node.
///
/// # Safety
///
/// `node` and `node_type` must identify the same live locked node.
#[inline(always)]
unsafe fn set_meta(node: *mut bindings::maple_node, node_type: bindings::maple_type, end: u8) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_set_meta(node, node_type, 0, end) };
}

/// Propagates a changed gap toward the root.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple write state.
#[inline(always)]
unsafe fn update_gap(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_update_gap(mas) };
}

/// Emits the Maple Tree write tracepoint.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple write state.
#[inline(always)]
unsafe fn trace_write(mas: *mut MaState, offset: u8, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_trace(mas, offset, entry) };
}

/// Executes the complex strategy selected by the Rust dispatcher.
///
/// # Safety
///
/// `wr_mas` must be a fully initialized write state protected by the tree
/// write lock.
#[inline(always)]
unsafe fn store_complex(wr_mas: *mut MaWrState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_complex(wr_mas) };
}

/// Replaces an exact-fit slot and updates the gap only on a NULL transition.
///
/// # Safety
///
/// `wr_mas` must describe a selected exact-fit write under the tree lock.
unsafe fn exact_fit(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The write traversal selected a valid slot.
    let slot = unsafe { (*wr_mas).slots.add(usize::from((*mas).offset)) };
    // SAFETY: The exact-fit strategy publishes into the selected slot.
    unsafe { store_slot(slot, (*wr_mas).entry) };

    // SAFETY: The caller supplies valid entry and content pointers.
    if unsafe { (*wr_mas).entry.is_null() != (*wr_mas).content.is_null() } {
        // SAFETY: The NULL status of the selected range changed.
        unsafe { update_gap(mas) };
    }
}

/// Appends a range to the end of a node without replacing the node.
///
/// # Safety
///
/// `wr_mas` must describe a selected append write under the tree lock and the
/// tree must not be in RCU mode.
unsafe fn append(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid write state.
    let node_type = unsafe { (*wr_mas).type_ };
    // SAFETY: Append requires a valid initialized write state.
    let new_end = unsafe { bindings::rust_maple_tree_wr_new_end(wr_mas) };
    // SAFETY: The caller supplies a valid write state.
    let end = unsafe { (*mas).end };

    // Reserve the metadata pivot before changing the data end.
    if new_end < pivot_count(node_type) {
        // SAFETY: The caller supplies a valid locked write state.
        let pivots = unsafe { (*wr_mas).pivots };
        // SAFETY: Both offsets are within the node pivot array.
        unsafe { *pivots.add(usize::from(new_end)) = *pivots.add(usize::from(end)) };
        // SAFETY: The caller supplies a valid node and node type.
        unsafe { set_meta((*wr_mas).node, node_type, new_end) };
    }

    // SAFETY: The caller supplies a valid locked write state.
    let slots = unsafe { (*wr_mas).slots };
    // SAFETY: The caller supplies a valid locked write state.
    let pivots = unsafe { (*wr_mas).pivots };

    if new_end == end.wrapping_add(1) {
        // SAFETY: The caller supplies valid locked write state fields.
        if unsafe { (*mas).last == (*wr_mas).r_max } {
            // Append to the end of the current range.
            // SAFETY: The new end identifies a writable slot.
            unsafe { store_slot(slots.add(usize::from(new_end)), (*wr_mas).entry) };
            // SAFETY: The old end identifies a writable pivot.
            unsafe { *pivots.add(usize::from(end)) = (*mas).index.wrapping_sub(1) };
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).offset = new_end };
        } else {
            // Append to the start of the current range.
            // SAFETY: The new end identifies a writable slot.
            unsafe { store_slot(slots.add(usize::from(new_end)), (*wr_mas).content) };
            // SAFETY: The old end identifies a writable pivot.
            unsafe { *pivots.add(usize::from(end)) = (*mas).last };
            // SAFETY: The old end identifies a writable slot.
            unsafe { store_slot(slots.add(usize::from(end)), (*wr_mas).entry) };
        }
    } else {
        // Append inside the current range without replacing its boundaries.
        let middle = usize::from(end) + 1;
        // SAFETY: The new end identifies a writable slot.
        unsafe { store_slot(slots.add(usize::from(new_end)), (*wr_mas).content) };
        // SAFETY: The middle offset identifies a writable pivot.
        unsafe { *pivots.add(middle) = (*mas).last };
        // SAFETY: The middle offset identifies a writable slot.
        unsafe { store_slot(slots.add(middle), (*wr_mas).entry) };
        // SAFETY: The old end identifies a writable pivot.
        unsafe { *pivots.add(usize::from(end)) = (*mas).index.wrapping_sub(1) };
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).offset = end.wrapping_add(1) };
    }

    // A NULL entry changes the gap even when both slots are non-NULL.
    // SAFETY: The caller supplies valid entry and content pointers.
    if unsafe { (*wr_mas).content.is_null() || (*wr_mas).entry.is_null() } {
        // SAFETY: A range changed between NULL and non-NULL state.
        unsafe { update_gap(mas) };
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).end = new_end };
    // SAFETY: The tracepoint uses the completed append state.
    unsafe { trace_write(mas, new_end, (*wr_mas).entry) };
}

/// Replaces or expands adjacent ranges while retaining the node size.
///
/// # Safety
///
/// `wr_mas` must describe a selected slot-store write under the tree lock.
unsafe fn slot_store(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid write state.
    let mut offset = unsafe { (*mas).offset };
    // SAFETY: The caller supplies a valid locked write state.
    let slots = unsafe { (*wr_mas).slots };
    // SAFETY: The caller supplies a valid locked write state.
    let pivots = unsafe { (*wr_mas).pivots };
    // SAFETY: The caller supplies a valid content pointer.
    let mut gap = unsafe { (*wr_mas).content.is_null() };

    // The second slot is inspected on every slot-store path.
    // SAFETY: Slot-store selection guarantees this slot exists.
    gap |= unsafe { slot_locked(mas, slots, offset.wrapping_add(1)) }.is_null();

    // SAFETY: The caller supplies valid locked write state fields.
    let slot_delta = unsafe { (*wr_mas).offset_end.wrapping_sub(offset) };
    if slot_delta == 1 {
        // SAFETY: The caller supplies valid locked write state fields.
        if unsafe { (*mas).index == (*wr_mas).r_min } {
            // Replace the current range and part of the next range.
            // SAFETY: The current offset identifies a writable slot.
            unsafe { store_slot(slots.add(usize::from(offset)), (*wr_mas).entry) };
            // SAFETY: The current offset identifies a writable pivot.
            unsafe { *pivots.add(usize::from(offset)) = (*mas).last };
        } else {
            // Replace part of the current range and the next range.
            // SAFETY: The next offset identifies a writable slot.
            unsafe {
                store_slot(
                    slots.add(usize::from(offset.wrapping_add(1))),
                    (*wr_mas).entry,
                )
            };
            // SAFETY: The current offset identifies a writable pivot.
            unsafe { *pivots.add(usize::from(offset)) = (*mas).index.wrapping_sub(1) };
            offset = offset.wrapping_add(1);
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).offset = offset };
        }
    } else {
        // SAFETY: RCU node replacement is required for this three-slot path.
        unsafe {
            bindings::rust_maple_tree_warn_on_once(bindings::rust_maple_tree_in_rcu((*mas).tree))
        };

        // Expand the range only partially over the previous and next ranges.
        // SAFETY: The three-slot path guarantees this slot exists.
        gap |= unsafe { slot_locked(mas, slots, offset.wrapping_add(2)) }.is_null();
        // SAFETY: The middle offset identifies a writable slot.
        unsafe {
            store_slot(
                slots.add(usize::from(offset.wrapping_add(1))),
                (*wr_mas).entry,
            )
        };
        // SAFETY: The current offset identifies a writable pivot.
        unsafe { *pivots.add(usize::from(offset)) = (*mas).index.wrapping_sub(1) };
        // SAFETY: The next offset identifies a writable pivot.
        unsafe { *pivots.add(usize::from(offset.wrapping_add(1))) = (*mas).last };
        offset = offset.wrapping_add(1);
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).offset = offset };
    }

    // SAFETY: The tracepoint observes the completed slot mutations.
    unsafe { trace_write(mas, 0, (*wr_mas).entry) };

    // Update the gap when the new entry is NULL or a replaced slot was NULL.
    // SAFETY: The caller supplies a valid entry pointer.
    if unsafe { (*wr_mas).entry.is_null() } || gap {
        // SAFETY: A range changed between NULL and non-NULL state.
        unsafe { update_gap(mas) };
    }
}

/// Dispatches a prepared Maple Tree write to its production strategy.
///
/// # Safety
///
/// `wr_mas` and `wr_mas.mas` must be valid and protected by the tree write
/// lock.
unsafe fn store_entry(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid selected store type.
    match unsafe { (*mas).store_type } {
        bindings::store_type_wr_exact_fit => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { exact_fit(wr_mas) };
        }
        bindings::store_type_wr_append => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { append(wr_mas) };
        }
        bindings::store_type_wr_slot_store => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { slot_store(wr_mas) };
        }
        bindings::store_type_wr_node_store
        | bindings::store_type_wr_spanning_store
        | bindings::store_type_wr_split_store
        | bindings::store_type_wr_rebalance
        | bindings::store_type_wr_new_root
        | bindings::store_type_wr_store_root
        | bindings::store_type_wr_invalid => {
            // SAFETY: The selected complex strategy remains a retained boundary.
            unsafe { store_complex(wr_mas) };
        }
        _ => {
            // SAFETY: Unknown values are treated as an impossible strategy.
            unsafe { store_complex(wr_mas) };
        }
    }
}

/// Executes the production Maple Tree write dispatcher.
///
/// # Safety
///
/// The C caller must provide a fully initialized write state and hold the
/// Maple Tree write lock.
#[export]
unsafe extern "C" fn rust_mas_wr_store_entry(wr_mas: *mut MaWrState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { store_entry(wr_mas) };
}
