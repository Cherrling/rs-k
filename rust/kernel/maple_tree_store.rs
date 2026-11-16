// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree write dispatcher and in-place store strategies.
//!
//! Rust owns store-type dispatch and the exact-fit, append, and slot-store
//! mutations. C retains node layout, RCU publication, protected slot access,
//! metadata layout, gap propagation, tracepoint delivery, and the complex
//! strategy implementations.

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
use super::maple_tree_core::MapleNode;
use super::maple_tree_core::{pivot_count, MaState, MaWrState};
use crate::bindings;
use core::ffi::c_void;
#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
use core::{mem::MaybeUninit, ptr};
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

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
/// Emits the node-store operation tracepoint.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple write state.
unsafe fn trace_op(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_trace_op(mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
/// Obtains one preallocated replacement node.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple state with a replacement node.
unsafe fn pop_node(mas: *mut MaState) -> *mut MapleNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_pop_node(mas) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
/// Copies the parent of one Maple node into another.
///
/// # Safety
///
/// `dst` and `src` must identify valid Maple nodes.
unsafe fn set_parent(dst: *mut MapleNode, src: *const MapleNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_set_parent(dst, src) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
/// Copies one complete Maple node.
///
/// # Safety
///
/// `dst` must be writable and `src` must identify an initialized Maple node.
unsafe fn copy_node(dst: *mut MapleNode, src: *const MapleNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_copy_node(dst, src) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
/// Publishes a complete RCU replacement node.
///
/// # Safety
///
/// `mas` must identify a valid locked Maple state. `node` must be initialized
/// with `node_type`, and `height` must match the current tree height.
unsafe fn replace_node(
    mas: *mut MaState,
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    height: u32,
) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_store_replace_node(mas, node, node_type, height) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
#[inline(always)]
fn slot_count(node_type: bindings::maple_type) -> u8 {
    // SAFETY: The layout type is valid.
    unsafe { bindings::rust_maple_tree_slot_count(node_type) }
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

/// Builds a replacement node for an insertion that changes the range layout.
///
/// # Safety
///
/// `wr_mas` must describe a selected node-store write under the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
unsafe fn node_store(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid locked write state.
    let node_type = unsafe { (*wr_mas).type_ };

    // SAFETY: The caller supplies a valid locked write state.
    unsafe { trace_op(mas) };
    // SAFETY: The caller supplies a valid initialized tree.
    let in_rcu = unsafe { bindings::rust_maple_tree_in_rcu((*mas).tree) };
    // SAFETY: The caller supplies a valid locked write state.
    let mut offset_end = unsafe { (*wr_mas).offset_end };
    let node_pivots = pivot_count(node_type);
    let node_slots = slot_count(node_type);
    // Assume that writing to the node end adds one entry.
    // SAFETY: The caller supplies valid locked write state fields.
    let mut new_end = unsafe {
        (*mas)
            .end
            .wrapping_add(1)
            .wrapping_sub(offset_end)
            .wrapping_add((*mas).offset)
    };

    // SAFETY: The caller supplies a valid locked write state.
    if unsafe { (*mas).last == (*wr_mas).end_piv } {
        offset_end = offset_end.wrapping_add(1);
        new_end = new_end.wrapping_sub(1);
    }

    let mut reuse = MaybeUninit::<MapleNode>::uninit();
    let new_node = if in_rcu {
        // SAFETY: A selected node store has preallocated a replacement node.
        unsafe { pop_node(mas) }
    } else {
        reuse.as_mut_ptr()
    };
    // SAFETY: The source and destination identify compatible Maple nodes.
    unsafe { set_parent(new_node, (*wr_mas).node) };
    // SAFETY: The replacement node has a valid concrete layout.
    let new_pivots = unsafe { super::maple_tree_core::pivots(new_node, node_type) };
    // SAFETY: The replacement node has a valid concrete layout.
    let new_slots = unsafe { super::maple_tree_core::slots(new_node, node_type) };
    // SAFETY: The caller supplies a valid locked write state.
    let mut offset = unsafe { (*mas).offset };

    if offset != 0 {
        // SAFETY: The ranges before the insertion point are initialized.
        unsafe {
            ptr::copy_nonoverlapping((*wr_mas).pivots, new_pivots, usize::from(offset));
            ptr::copy_nonoverlapping((*wr_mas).slots, new_slots, usize::from(offset));
        }
    }

    // Retain the beginning of a range that starts after the old range minimum.
    // SAFETY: The caller supplies valid locked write state fields.
    if unsafe { (*wr_mas).r_min < (*mas).index } {
        // SAFETY: The insertion offset is writable.
        unsafe { store_slot(new_slots.add(usize::from(offset)), (*wr_mas).content) };
        // SAFETY: The insertion offset is writable.
        unsafe { *new_pivots.add(usize::from(offset)) = (*mas).index.wrapping_sub(1) };
        offset = offset.wrapping_add(1);
        new_end = new_end.wrapping_add(1);
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).offset = offset };
    }

    if offset < node_pivots {
        // SAFETY: The insertion offset is below the pivot count.
        unsafe { *new_pivots.add(usize::from(offset)) = (*mas).last };
    }
    // SAFETY: The insertion offset identifies a writable slot.
    unsafe { store_slot(new_slots.add(usize::from(offset)), (*wr_mas).entry) };

    // Copy every range after the overwritten region.
    // SAFETY: The caller supplies valid locked write state fields.
    if offset_end <= unsafe { (*mas).end } {
        let dst_offset = offset.wrapping_add(1);
        // SAFETY: The traversal end is not beyond the node end.
        let copy_size = unsafe { (*mas).end }
            .wrapping_sub(offset_end)
            .wrapping_add(1);
        // SAFETY: The retained ranges are initialized.
        unsafe {
            ptr::copy_nonoverlapping(
                (*wr_mas).slots.add(usize::from(offset_end)),
                new_slots.add(usize::from(dst_offset)),
                usize::from(copy_size),
            );
            ptr::copy_nonoverlapping(
                (*wr_mas).pivots.add(usize::from(offset_end)),
                new_pivots.add(usize::from(dst_offset)),
                usize::from(copy_size.wrapping_sub(1)),
            );
        }
        if new_end < node_pivots {
            // SAFETY: The new end is below the pivot count.
            unsafe { *new_pivots.add(usize::from(new_end)) = (*mas).max };
        }
    }

    // A locked reuse node must not retain stale slots or pivots.
    if !in_rcu && new_end.wrapping_add(2) < node_slots {
        let clear_from = new_end.wrapping_add(1);
        // SAFETY: The unused tail is within the node arrays.
        unsafe {
            ptr::write_bytes(
                new_slots.add(usize::from(clear_from)),
                0,
                usize::from(node_slots.wrapping_sub(clear_from)),
            );
        }
        if clear_from < node_pivots {
            // SAFETY: The unused pivot tail is within the node array.
            unsafe {
                ptr::write_bytes(
                    new_pivots.add(usize::from(clear_from)),
                    0,
                    usize::from(node_pivots.wrapping_sub(clear_from)),
                );
            }
        }
    }

    if new_end < node_slots.wrapping_sub(1) {
        // SAFETY: A leaf node can store its metadata at the new end.
        unsafe { set_meta(new_node, node_type, new_end) };
    }

    if in_rcu {
        // SAFETY: Node stores allocate one replacement in RCU mode.
        let height = unsafe { bindings::rust_maple_tree_height(mas) };
        // SAFETY: The replacement node is fully initialized.
        unsafe { replace_node(mas, new_node, node_type, height) };
    } else {
        // SAFETY: The reuse node is fully initialized for all live ranges.
        unsafe { copy_node((*wr_mas).node, new_node) };
    }

    // SAFETY: The tracepoint observes the completed node mutation.
    unsafe { trace_write(mas, 0, (*wr_mas).entry) };
    // SAFETY: The replacement changed one or more range entries.
    unsafe { update_gap(mas) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).end = new_end };
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
        #[cfg(CONFIG_RUST_MAPLE_TREE_NODE_STORE)]
        bindings::store_type_wr_node_store => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { node_store(wr_mas) };
        }
        #[cfg(not(CONFIG_RUST_MAPLE_TREE_NODE_STORE))]
        bindings::store_type_wr_node_store => {
            // SAFETY: The selected complex strategy remains a retained boundary.
            unsafe { store_complex(wr_mas) };
        }
        bindings::store_type_wr_spanning_store
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
