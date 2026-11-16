// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree write dispatcher and store strategies.
//!
//! Rust owns store-type dispatch, the in-place store strategies, node
//! replacement, spanning stores, and root stores. C retains node layout, RCU
//! publication, protected slot access, metadata layout, gap propagation,
//! tracepoint delivery, and the split and rebalance strategies.

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

#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
/// Copies the tree parent into a replacement root node.
///
/// # Safety
///
/// `mas` must hold the tree lock and `node` must be the new root node.
unsafe fn set_root_parent(node: *mut MapleNode, mas: *mut MaState) {
    // SAFETY: The caller supplies a valid locked state and root node.
    unsafe { bindings::rust_maple_tree_store_root_parent(node, mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
/// Publishes the encoded `mas->node` as the tree root.
///
/// # Safety
///
/// `mas` must hold the tree lock and `mas->node` must encode a complete node.
unsafe fn publish_root_node(mas: *mut MaState) {
    // SAFETY: The caller supplies a complete encoded root node.
    unsafe { bindings::rust_maple_tree_store_root_node(mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
/// Publishes a direct root entry.
///
/// # Safety
///
/// `mas` must hold the tree lock and `entry` must be a valid root entry.
unsafe fn publish_root_entry(mas: *mut MaState, entry: *mut c_void) {
    // SAFETY: The caller supplies a valid direct root entry.
    unsafe { bindings::rust_maple_tree_store_root_entry(mas, entry) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
/// Destroys the old root subtree after root replacement.
///
/// # Safety
///
/// `mas` must hold the tree lock and `root` must be the previous root.
unsafe fn destroy_root(mas: *mut MaState, root: *mut c_void) {
    // SAFETY: The caller supplies the detached previous root.
    unsafe { bindings::rust_maple_tree_store_destroy_root(mas, root) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
/// Sets the height recorded by the Maple tree.
///
/// # Safety
///
/// `mas` must hold the tree lock and `height` must match the new root layout.
unsafe fn set_tree_height(mas: *mut MaState, height: u32) {
    // SAFETY: The caller supplies a valid locked tree state.
    unsafe { bindings::rust_maple_tree_store_set_height(mas, height) };
}

/// Expands a direct or empty root into a leaf node.
///
/// # Safety
///
/// `mas` must select a direct root store and hold the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
unsafe fn root_store_expand(mas: *mut MaState, entry: *mut c_void) {
    const NODE_TYPE: bindings::maple_type = bindings::maple_type_maple_leaf_64;

    // SAFETY: The caller holds the tree lock and selected a root store.
    let contents = unsafe { super::maple_tree_core::root(mas) };
    // SAFETY: A direct root expansion has one preallocated node.
    let node = unsafe { pop_node(mas) };
    // SAFETY: The replacement node has the selected leaf layout.
    let pivots = unsafe { super::maple_tree_core::pivots(node, NODE_TYPE) };
    // SAFETY: The replacement node has the selected leaf layout.
    let slots = unsafe { super::maple_tree_core::slots(node, NODE_TYPE) };
    // SAFETY: The new node is the next tree root.
    unsafe { set_root_parent(node, mas) };
    // SAFETY: The replacement node is complete enough to encode.
    let enode = unsafe { super::maple_tree_core::make_node(node, NODE_TYPE) };
    // SAFETY: The caller supplies a valid locked state.
    unsafe {
        (*mas).node = enode;
        (*mas).status = bindings::maple_status_ma_active;
    }

    let mut slot = 0u8;
    // SAFETY: The caller supplies a valid locked state.
    let index = unsafe { (*mas).index };
    if index != 0 {
        if !contents.is_null() {
            // SAFETY: This is the first writable slot.
            unsafe { store_slot(slots, contents) };
            if index > 1 {
                slot = slot.wrapping_add(1);
            }
        }
        // SAFETY: The current pivot is writable.
        unsafe { *pivots.add(usize::from(slot)) = index.wrapping_sub(1) };
        slot = slot.wrapping_add(1);
    }

    // SAFETY: The selected slot is writable.
    unsafe { store_slot(slots.add(usize::from(slot)), entry) };
    // SAFETY: The caller supplies a valid locked state.
    let last = unsafe { (*mas).last };
    // SAFETY: The new entry pivot is writable.
    unsafe { *pivots.add(usize::from(slot)) = last };
    // SAFETY: The caller supplies a valid locked state.
    unsafe { (*mas).offset = slot };
    if last != !0 {
        slot = slot.wrapping_add(1);
        // SAFETY: The full-range pivot is writable.
        unsafe { *pivots.add(usize::from(slot)) = !0 };
    }

    // SAFETY: The expanded tree has one leaf level.
    unsafe { set_tree_height(mas, 1) };
    // SAFETY: The completed leaf metadata records the final slot.
    unsafe { set_meta(node, NODE_TYPE, slot) };
    // SAFETY: The completed encoded node becomes the new root.
    unsafe { publish_root_node(mas) };
}

/// Stores into an empty root or replaces a direct root entry.
///
/// # Safety
///
/// `mas` must select a root store and hold the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
unsafe fn root_store_entry(mas: *mut MaState, entry: *mut c_void) {
    if entry.is_null() {
        // SAFETY: The caller supplies a valid locked state.
        if unsafe { (*mas).index } == 0 {
            // SAFETY: Clearing an empty or direct root publishes NULL.
            unsafe { publish_root_entry(mas, entry) };
        }
        return;
    }

    // SAFETY: The caller supplies a valid locked state.
    if unsafe { (*mas).last } != 0 || unsafe { (*mas).index } != 0 {
        // SAFETY: A multi-index root requires a leaf node.
        unsafe { root_store_expand(mas, entry) };
        return;
    }

    if entry as usize & 3 == 2 {
        // SAFETY: An encoded internal entry requires a leaf root.
        unsafe { root_store_expand(mas, entry) };
        return;
    }

    // SAFETY: A direct entry can occupy the root pointer.
    unsafe { publish_root_entry(mas, entry) };
    // SAFETY: A direct root entry starts a new walk.
    unsafe { (*mas).status = bindings::maple_status_ma_start };
}

/// Replaces the complete tree with a new leaf root.
///
/// # Safety
///
/// `mas` must select the complete range and hold the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
unsafe fn root_store_new_root(mas: *mut MaState, entry: *mut c_void) {
    const NODE_TYPE: bindings::maple_type = bindings::maple_type_maple_leaf_64;

    // SAFETY: The caller holds the tree lock.
    let old_root = unsafe { super::maple_tree_core::root(mas) };
    // SAFETY: The caller supplies a valid locked state.
    let valid_range = unsafe { (*mas).index == 0 && (*mas).last == !0 };
    super::maple_tree_core::warn_on_once(!valid_range);

    if entry.is_null() {
        // SAFETY: The empty tree has no node levels.
        unsafe { set_tree_height(mas, 0) };
        // SAFETY: Publish the empty root.
        unsafe { publish_root_entry(mas, entry) };
        // SAFETY: The empty tree starts a new walk.
        unsafe { (*mas).status = bindings::maple_status_ma_start };
    } else {
        // SAFETY: A new non-NULL root has one preallocated node.
        let node = unsafe { pop_node(mas) };
        // SAFETY: The new root node has the selected leaf layout.
        let pivots = unsafe { super::maple_tree_core::pivots(node, NODE_TYPE) };
        // SAFETY: The new root node has the selected leaf layout.
        let slots = unsafe { super::maple_tree_core::slots(node, NODE_TYPE) };
        // SAFETY: The replacement node is the next tree root.
        unsafe { set_root_parent(node, mas) };
        // SAFETY: The replacement node is ready to encode.
        let enode = unsafe { super::maple_tree_core::make_node(node, NODE_TYPE) };
        // SAFETY: The caller supplies a valid locked state.
        unsafe {
            (*mas).node = enode;
            (*mas).status = bindings::maple_status_ma_active;
        }
        // SAFETY: The first slot is writable.
        unsafe { store_slot(slots, entry) };
        // SAFETY: The caller supplies a valid locked state.
        let last = unsafe { (*mas).last };
        // SAFETY: The full-range pivot is writable.
        unsafe { *pivots = last };
        // SAFETY: The new tree has one leaf level.
        unsafe { set_tree_height(mas, 1) };
        // SAFETY: Publish the completed encoded root.
        unsafe { publish_root_node(mas) };
    }

    // SAFETY: The previous root is now detached.
    unsafe { destroy_root(mas, old_root) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Walks a write state to the leaf containing its index.
///
/// # Safety
///
/// `wr_mas` must be a valid write state protected by the tree lock.
unsafe fn spanning_walk_index(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    unsafe { bindings::rust_maple_tree_spanning_walk_index(wr_mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Resets a write state to the complete Maple range.
///
/// # Safety
///
/// `mas` must be a valid state protected by the tree lock.
unsafe fn spanning_set_full_range(mas: *mut MaState) {
    // SAFETY: The caller supplies a valid locked write state.
    unsafe { bindings::rust_maple_tree_spanning_set_full_range(mas) };
}

#[cfg(all(
    CONFIG_RUST_MAPLE_TREE_SPANNING_STORE,
    CONFIG_RUST_MAPLE_TREE_ROOT_STORE
))]
/// Replaces the complete tree with the native root-store implementation.
///
/// # Safety
///
/// `mas` must select the complete range and hold the tree lock.
unsafe fn spanning_new_root(mas: *mut MaState, entry: *mut c_void) {
    // SAFETY: The root-store unit owns complete-range replacement.
    unsafe { root_store_new_root(mas, entry) };
}

#[cfg(all(
    CONFIG_RUST_MAPLE_TREE_SPANNING_STORE,
    not(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)
))]
/// Replaces the complete tree with one root entry.
///
/// # Safety
///
/// `mas` must select the complete range and hold the tree lock.
unsafe fn spanning_new_root(mas: *mut MaState, entry: *mut c_void) {
    // SAFETY: The caller supplies a valid locked write state.
    unsafe { bindings::rust_maple_tree_spanning_new_root(mas, entry) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Finds an adjacent node that can contribute data to the copy.
///
/// # Safety
///
/// Both write states must describe one locked spanning write.
unsafe fn spanning_sibling(left: *mut MaWrState, right: *mut MaWrState, sibling: *mut MaState) {
    // SAFETY: The caller supplies two sides of a valid spanning write.
    unsafe { bindings::rust_maple_tree_spanning_sibling(left, right, sibling) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[derive(Clone, Copy)]
struct NodeCopySource {
    node: *mut MapleNode,
    start: u8,
    size: u8,
    max: usize,
    node_type: bindings::maple_type,
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[derive(Clone, Copy)]
struct NodeCopyDestination {
    node: *mut MapleNode,
    start: u8,
    node_type: bindings::maple_type,
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Copies a bounded source range into a destination node.
///
/// # Safety
///
/// Both node descriptors must identify compatible live Maple nodes and the
/// requested offsets must be in bounds.
unsafe fn spanning_node_copy(
    mas: *mut MaState,
    src: NodeCopySource,
    dst: NodeCopyDestination,
) -> usize {
    // SAFETY: The source and destination are valid for the requested copy.
    unsafe {
        bindings::rust_maple_tree_spanning_node_copy(
            mas,
            src.node,
            src.start,
            src.size,
            src.max,
            src.node_type,
            dst.node,
            dst.start,
            dst.node_type,
        )
    }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Finalizes a completed destination node.
///
/// # Safety
///
/// `node` must be a complete destination node of `node_type`.
unsafe fn spanning_node_finalise(node: *mut MapleNode, node_type: bindings::maple_type, end: u8) {
    // SAFETY: The caller supplies a complete destination node.
    unsafe { bindings::rust_maple_tree_spanning_node_finalise(node, node_type, end) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Encodes a completed node into a private copy slot.
///
/// # Safety
///
/// `slot` must be private to this writer and `node` must have the requested
/// concrete layout.
unsafe fn spanning_init_slot(
    slot: *mut *mut c_void,
    node: *mut MapleNode,
    node_type: bindings::maple_type,
) {
    // SAFETY: The slot and node have the requested compatible layout.
    unsafe { bindings::rust_maple_tree_spanning_init_slot(slot, node, node_type) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Returns whether a Maple tree tracks allocated ranges.
///
/// # Safety
///
/// `tree` must be a valid initialized Maple tree.
unsafe fn spanning_is_alloc(tree: *mut bindings::maple_tree) -> bool {
    // SAFETY: The caller supplies a valid Maple tree.
    unsafe { bindings::rust_maple_tree_spanning_is_alloc(tree) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Computes the largest gap in a completed leaf.
///
/// # Safety
///
/// The node and its layout arrays must be valid for the complete range.
unsafe fn spanning_leaf_gap(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    min: usize,
    max: usize,
    pivots: *mut usize,
    slots: *mut *mut c_void,
) -> usize {
    // SAFETY: The node layout and protected range are valid.
    unsafe { bindings::rust_maple_tree_spanning_leaf_gap(node, node_type, min, max, pivots, slots) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Returns the gap array of an allocation-range node.
///
/// # Safety
///
/// `node` must be a valid initialized allocation-range node.
unsafe fn spanning_gaps(node: *mut MapleNode, node_type: bindings::maple_type) -> *mut usize {
    // SAFETY: The caller supplies a valid allocation-range node.
    unsafe { bindings::rust_maple_tree_spanning_gaps(node, node_type) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Returns the metadata gap offset of an allocation-range node.
///
/// # Safety
///
/// `node` must be a valid initialized allocation-range node.
unsafe fn spanning_meta_gap(node: *mut MapleNode) -> u8 {
    // SAFETY: The caller supplies a valid allocation-range node.
    unsafe { bindings::rust_maple_tree_spanning_meta_gap(node) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Compares a private root slot with the copy destination.
///
/// # Safety
///
/// `mas` must hold the tree lock and `copy` must contain a complete root slot.
unsafe fn spanning_root_matches(mas: *mut MaState, copy: *mut bindings::maple_copy) -> bool {
    // SAFETY: The copy node and current root slot are valid.
    unsafe { bindings::rust_maple_tree_spanning_root_matches(mas, copy) }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Installs a complete node as the tree root.
///
/// # Safety
///
/// `mas` must hold the tree lock and `node` must be the complete root.
unsafe fn spanning_reset_root(mas: *mut MaState, node: *mut MapleNode) {
    // SAFETY: The caller supplies a complete new root node.
    unsafe { bindings::rust_maple_tree_spanning_reset_root(mas, node) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Copies the encoded parent from a Maple state to a replacement node.
///
/// # Safety
///
/// `mas` must select a valid active node and `node` must belong there.
unsafe fn spanning_set_parent(node: *mut MapleNode, mas: *mut MaState) {
    // SAFETY: The node belongs at the current state location.
    unsafe { bindings::rust_maple_tree_spanning_set_parent(node, mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Moves a write state to its parent node.
///
/// # Safety
///
/// `wr_mas` must describe a valid non-root write state under the tree lock.
unsafe fn spanning_wr_ascend(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state below the root.
    unsafe { bindings::rust_maple_tree_spanning_wr_ascend(wr_mas) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Publishes a completed replacement subtree.
///
/// # Safety
///
/// `mas` must hold the tree lock and every copy destination must be complete.
unsafe fn spanning_publish(mas: *mut MaState, copy: *mut bindings::maple_copy) {
    // SAFETY: Every destination in the copy node is complete.
    unsafe { bindings::rust_maple_tree_spanning_publish(mas, copy) };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[inline(always)]
/// Reads a private copy pivot.
///
/// # Safety
///
/// `copy` must be valid and `offset` must be below three.
unsafe fn copy_pivot(copy: *mut bindings::maple_copy, offset: usize) -> usize {
    // SAFETY: The offset is within the three-entry copy pivot array.
    unsafe { (*copy).__bindgen_anon_1.pivot[offset] }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[inline(always)]
/// Writes a private copy pivot.
///
/// # Safety
///
/// `copy` must be valid and `offset` must be below three.
unsafe fn copy_set_pivot(copy: *mut bindings::maple_copy, offset: usize, value: usize) {
    // SAFETY: The offset is within the three-entry copy pivot array.
    unsafe { (*copy).__bindgen_anon_1.pivot[offset] = value };
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[inline(always)]
/// Reads the private copy maximum.
///
/// # Safety
///
/// `copy` must be valid and its maximum union member must be active.
unsafe fn copy_max(copy: *mut bindings::maple_copy) -> usize {
    // SAFETY: The active union member stores the copy maximum.
    unsafe { (*copy).__bindgen_anon_1.__bindgen_anon_1.max }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
#[inline(always)]
/// Writes the private copy maximum.
///
/// # Safety
///
/// `copy` must be valid and its maximum union member must be writable.
unsafe fn copy_set_max(copy: *mut bindings::maple_copy, value: usize) {
    // SAFETY: The active union member stores the copy maximum.
    unsafe { (*copy).__bindgen_anon_1.__bindgen_anon_1.max = value };
}

/// Extends a NULL spanning write over adjacent NULL ranges.
///
/// # Safety
///
/// Both write states must identify the two sides of one locked spanning write.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn extend_spanning_null(left: *mut MaWrState, right: *mut MaWrState) {
    // SAFETY: The caller supplies valid write states.
    let left_state = unsafe { (*left).mas };
    // SAFETY: The caller supplies valid write states.
    let right_state = unsafe { (*right).mas };
    // SAFETY: The left state is valid.
    let left_slot = unsafe { (*left_state).offset };

    // SAFETY: The left write state is valid.
    if unsafe { (*left).content.is_null() } {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*left_state).index = (*left).r_min };
    }

    // SAFETY: The left write state is valid.
    if unsafe { (*left_state).index == (*left).r_min } && left_slot != 0 && {
        // SAFETY: The previous slot is within the locked node.
        unsafe { slot_locked(left_state, (*left).slots, left_slot.wrapping_sub(1)).is_null() }
    } {
        if left_slot > 1 {
            // SAFETY: The pivot two entries before the slot is valid.
            let previous = unsafe { *(*left).pivots.add(usize::from(left_slot - 2)) };
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*left_state).index = previous.wrapping_add(1) };
        } else {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*left_state).index = (*left_state).min };
        }
        // SAFETY: The adjacent NULL range begins at the previous slot.
        unsafe {
            (*left_state).offset = left_slot - 1;
            (*left).r_min = (*left_state).index;
        }
    }

    // SAFETY: The right write state is valid.
    if unsafe { (*right).content.is_null() } {
        // SAFETY: The right write state is valid.
        if unsafe { (*right_state).last < (*right).r_max } {
            // SAFETY: Include the complete final NULL range.
            unsafe { (*right_state).last = (*right).r_max };
        }
        // SAFETY: Advance past the final NULL slot.
        unsafe { (*right_state).offset = (*right_state).offset.wrapping_add(1) };
    }
}

/// Initializes the simulated leaf node at the bottom of a spanning write.
///
/// # Safety
///
/// Both write states must identify the two ends of one locked spanning write.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn copy_leaf_init(
    copy: *mut bindings::maple_copy,
    mas: *mut MaState,
    left: *mut MaWrState,
    right: *mut MaWrState,
) {
    // SAFETY: The copy node is private to this writer until publication.
    unsafe {
        (*copy).height = 1;
    }
    let mut end = 0usize;

    // SAFETY: The caller supplies a valid left write state.
    if unsafe { (*left).r_min < (*mas).index } {
        // SAFETY: The first private copy slot is available.
        unsafe {
            (*copy).slot[0] = (*left).content;
        }
        // SAFETY: The caller supplies a valid writable state.
        let pivot = unsafe { (*mas).index }.wrapping_sub(1);
        // SAFETY: The first private copy pivot is available.
        unsafe { copy_set_pivot(copy, 0, pivot) };
        end += 1;
    }

    // SAFETY: The current private copy slot and pivot are available.
    unsafe {
        (*copy).slot[end] = (*left).entry;
    }
    // SAFETY: The caller supplies a valid writable state.
    let pivot = unsafe { (*mas).last };
    // SAFETY: The current private copy pivot is available.
    unsafe { copy_set_pivot(copy, end, pivot) };

    // SAFETY: The right write state is valid.
    if unsafe { (*right).end_piv > (*mas).last } {
        end += 1;
        // SAFETY: The right state identifies the retained final slot.
        let slot = unsafe { *(*right).slots.add(usize::from((*right).offset_end)) };
        // SAFETY: The next private copy slot is available.
        unsafe { (*copy).slot[end] = slot };
        // SAFETY: The right write state is valid.
        let pivot = unsafe { (*right).end_piv };
        // SAFETY: The next private copy pivot is available.
        unsafe { copy_set_pivot(copy, end, pivot) };
    }

    // SAFETY: The private copy node is now initialized.
    unsafe {
        (*copy).min = (*left).r_min;
        copy_set_max(copy, copy_pivot(copy, end));
        (*copy).end = end as u8;
    }
}

/// Calculates the one-indexed copy data size.
///
/// # Safety
///
/// The copy node and both write states must belong to one spanning write.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn copy_data_calc(
    copy: *mut bindings::maple_copy,
    left: *mut MaWrState,
    right: *mut MaWrState,
) {
    // SAFETY: The caller supplies valid write states.
    let left_state = unsafe { (*left).mas };
    // SAFETY: The caller supplies valid write states.
    let right_state = unsafe { (*right).mas };
    // SAFETY: All fields hold valid sizes and offsets.
    let data = unsafe {
        (*left_state)
            .offset
            .wrapping_add((*copy).end)
            .wrapping_add(1)
            .wrapping_add((*right_state).end)
            .wrapping_sub((*right).offset_end)
    };
    // SAFETY: The caller supplies a valid writable copy node.
    unsafe { (*copy).data = data };
}

/// Computes spanning data and obtains an adjacent sibling when required.
///
/// # Safety
///
/// The copy node and write states must belong to one locked spanning write.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn spanning_data(
    copy: *mut bindings::maple_copy,
    left: *mut MaWrState,
    right: *mut MaWrState,
    sibling: *mut MaState,
) {
    // SAFETY: The caller supplies one spanning write.
    unsafe { copy_data_calc(copy, left, right) };
    // SAFETY: Both write states are valid.
    let left_state = unsafe { (*left).mas };
    // SAFETY: Both write states are valid.
    let right_state = unsafe { (*right).mas };
    // SAFETY: The left state is valid.
    let node_min = unsafe { (*left_state).min };
    // SAFETY: The right state is valid.
    let node_max = unsafe { (*right_state).max };
    // SAFETY: The left state is valid.
    let node_type = unsafe { (*left).type_ };
    // SAFETY: The copy data size is valid.
    let data = unsafe { (*copy).data };
    // SAFETY: The write state selected a valid node layout.
    let min_slots = unsafe { bindings::rust_maple_tree_min_slots(node_type) };

    if (node_min != 0 || node_max != !0) && data <= min_slots {
        // SAFETY: A non-global spanning write can use an adjacent sibling.
        unsafe { spanning_sibling(left, right, sibling) };
        // SAFETY: The sibling state is now complete.
        unsafe { (*copy).data = (*copy).data.wrapping_add((*sibling).end).wrapping_add(1) };
    } else {
        // SAFETY: No sibling participates in this copy.
        unsafe { (*sibling).end = 0 };
    }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Appends a source described by a Maple state.
///
/// # Safety
///
/// `copy` must have room for another source and `mas` must identify a valid
/// locked node.
unsafe fn append_mas_copy_source(
    copy: *mut bindings::maple_copy,
    mas: *mut MaState,
    start: u8,
    end: u8,
) {
    // SAFETY: The caller supplies a valid source state.
    let count = usize::from(unsafe { (*copy).s_count });
    // SAFETY: The source slot is available.
    let node = unsafe { super::maple_tree_core::to_node((*mas).node) };
    // SAFETY: The caller supplies a valid source state.
    let node_type = unsafe { super::maple_tree_core::node_type((*mas).node) };
    // SAFETY: The source state and pivot array are valid.
    let max = if unsafe { (*mas).end <= end } {
        // SAFETY: The caller supplies a valid source state.
        unsafe { (*mas).max }
    } else {
        // SAFETY: The end offset is within the source pivots.
        unsafe { *super::maple_tree_core::pivots(node, node_type).add(usize::from(end)) }
    };
    // SAFETY: The source slot is available.
    unsafe {
        (*copy).src[count].node = node;
        (*copy).src[count].max = max;
        (*copy).src[count].start = start;
        (*copy).src[count].end = end;
        (*copy).src[count].mt = node_type;
        (*copy).s_count = (*copy).s_count.wrapping_add(1);
    }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Appends a source described by a write state.
///
/// # Safety
///
/// `copy` must have room for another source and `wr_mas` must identify a valid
/// locked write state.
unsafe fn append_wr_copy_source(
    copy: *mut bindings::maple_copy,
    wr_mas: *mut MaWrState,
    start: u8,
    end: u8,
) {
    // SAFETY: The caller supplies a valid source state.
    let count = usize::from(unsafe { (*copy).s_count });
    // SAFETY: The caller supplies valid locked write state fields.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies valid locked write state fields.
    let max = if unsafe { (*mas).end <= end } {
        // SAFETY: The caller supplies a valid source state.
        unsafe { (*mas).max }
    } else {
        // SAFETY: The end offset is within the source pivots.
        unsafe { *(*wr_mas).pivots.add(usize::from(end)) }
    };
    // SAFETY: The source slot is available.
    unsafe {
        (*copy).src[count].node = (*wr_mas).node;
        (*copy).src[count].max = max;
        (*copy).src[count].start = start;
        (*copy).src[count].end = end;
        (*copy).src[count].mt = (*wr_mas).type_;
        (*copy).s_count = (*copy).s_count.wrapping_add(1);
    }
}

#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
/// Appends the private simulated leaf as a copy source.
///
/// # Safety
///
/// `copy` must have room for another source and a complete simulated leaf.
unsafe fn init_copy_source(copy: *mut bindings::maple_copy) {
    // SAFETY: The caller supplies a valid source slot.
    let count = usize::from(unsafe { (*copy).s_count });
    // SAFETY: The simulated copy node is a valid source.
    unsafe {
        (*copy).src[count].node = copy.cast::<MapleNode>();
        (*copy).src[count].mt = bindings::maple_type_maple_copy;
        (*copy).src[count].max = copy_max(copy);
        (*copy).src[count].start = 0;
        (*copy).src[count].end = (*copy).end;
        (*copy).s_count = (*copy).s_count.wrapping_add(1);
    }
}

/// Configures all sources that contribute data to the replacement nodes.
///
/// # Safety
///
/// The copy node, both write states, and optional sibling must be valid.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn multi_source_setup(
    copy: *mut bindings::maple_copy,
    left: *mut MaWrState,
    right: *mut MaWrState,
    sibling: *mut MaState,
) {
    // SAFETY: No source was configured yet.
    unsafe { (*copy).s_count = 0 };
    // SAFETY: The sibling state is valid.
    let sibling_end = unsafe { (*sibling).end };
    // SAFETY: The left write state is valid.
    let left_state = unsafe { (*left).mas };

    // SAFETY: The sibling and left state are valid.
    if sibling_end != 0 && unsafe { (*sibling).max < (*left_state).min } {
        // SAFETY: A complete left sibling contributes all of its data.
        unsafe { append_mas_copy_source(copy, sibling, 0, sibling_end) };
    }

    // SAFETY: The left state is valid.
    if unsafe { (*left_state).offset } != 0 {
        // SAFETY: The left offset is valid.
        let end = unsafe { (*left_state).offset.wrapping_sub(1) };
        // SAFETY: The retained left data is within the source node.
        unsafe {
            append_wr_copy_source(copy, left, 0, end);
            let count = usize::from((*copy).s_count.wrapping_sub(1));
            (*copy).src[count].max = (*copy).min.wrapping_sub(1);
        }
    }

    // SAFETY: The private copy node is the next source.
    unsafe { init_copy_source(copy) };

    // SAFETY: The right write state is valid.
    let right_state = unsafe { (*right).mas };
    // SAFETY: Compare the retained right range.
    if unsafe { (*right_state).end != (*right).offset_end } {
        // SAFETY: Copy the right ranges after the overwritten range.
        unsafe {
            append_wr_copy_source(
                copy,
                right,
                (*right).offset_end.wrapping_add(1),
                (*right_state).end,
            )
        };
    }

    // SAFETY: The sibling state is valid.
    if unsafe { (*sibling).end != 0 && (*sibling).min > (*right_state).max } {
        // SAFETY: A complete right sibling contributes all of its data.
        unsafe { append_mas_copy_source(copy, sibling, 0, (*sibling).end) };
    }
}

/// Selects one, two, or three replacement nodes.
///
/// # Safety
///
/// The copy node must contain a complete simulated source.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn destination_setup(
    copy: *mut bindings::maple_copy,
    mas: *mut MaState,
    node_type: bindings::maple_type,
) {
    let node_slots = slot_count(node_type);
    // SAFETY: The copy node is valid.
    let data = unsafe { (*copy).data };

    if data <= node_slots {
        // SAFETY: A single destination can hold the data.
        unsafe {
            (*copy).split = data.wrapping_sub(1);
            (*copy).d_count = 1;
        }
    } else {
        // SAFETY: The copy data size is valid.
        unsafe {
            (*copy).split = data.wrapping_sub(1) / 2;
            (*copy).d_count = 2;
        }

        let full_size = data == node_slots.wrapping_mul(2);
        let larger_size = data > node_slots.wrapping_mul(2);
        if full_size || larger_size {
            let mut three_way = true;

            if full_size {
                three_way = false;
                if super::maple_tree_core::is_leaf(node_type) {
                    // SAFETY: The split and source count are valid.
                    let mut off = unsafe { (*copy).split };
                    // SAFETY: The source count is valid.
                    let source_count = usize::from(unsafe { (*copy).s_count });
                    let mut source = 0usize;

                    while source < source_count {
                        // SAFETY: The source index is valid.
                        let source_width = unsafe {
                            (*copy).src[source]
                                .end
                                .wrapping_sub((*copy).src[source].start)
                        };
                        if source_width >= off {
                            break;
                        }
                        off = off.wrapping_sub(source_width.wrapping_add(1));
                        source += 1;
                    }

                    // SAFETY: The selected source index is in bounds.
                    let source_start = unsafe { (*copy).src[source].start };
                    off = off.wrapping_add(source_start);
                    // SAFETY: The source node and offset are valid.
                    let source_slot = unsafe {
                        *super::maple_tree_core::slots(
                            (*copy).src[source].node,
                            (*copy).src[source].mt,
                        )
                        .add(usize::from(off))
                    };
                    if source_slot.is_null() {
                        // SAFETY: A NULL boundary can move the split by one.
                        unsafe { (*copy).split = (*copy).split.wrapping_add(1) };
                        // SAFETY: The adjusted split remains valid.
                        if unsafe { (*copy).split } >= node_slots {
                            // SAFETY: Move the split to the earlier boundary.
                            unsafe { (*copy).split = (*copy).split.wrapping_sub(2) };
                            // SAFETY: Check that the earlier split still fits.
                            if unsafe {
                                data.wrapping_sub(2).wrapping_sub((*copy).split) >= node_slots
                            } {
                                three_way = true;
                            }
                        }
                    }
                }
            }

            if three_way {
                // SAFETY: Use a balanced three-way split.
                unsafe {
                    (*copy).split = data.wrapping_add(2) / 3;
                    (*copy).d_count = 3;
                }
            }
        }
    }

    // SAFETY: The selected destination count is valid.
    let destination_count = usize::from(unsafe { (*copy).d_count });
    for destination in 0..destination_count {
        // SAFETY: Obtain one preallocated replacement node.
        let node = unsafe { pop_node(mas) };
        // SAFETY: Configure the selected destination.
        unsafe {
            (*copy).dst[destination].mt = node_type;
            (*copy).dst[destination].node = node;
        }
    }
}

/// Copies all simulated data into the selected replacement nodes.
///
/// # Safety
///
/// Every source and destination in the copy setup must be valid.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn copy_data_write(copy: *mut bindings::maple_copy, mas: *mut MaState) {
    let mut data_offset = 0u8;
    let mut source = 0usize;
    let mut destination = 0usize;
    // SAFETY: At least one source exists.
    let mut src = unsafe { (*copy).src[source].node };
    let mut dst;
    // SAFETY: The first source is valid.
    let mut source_offset = unsafe { (*copy).src[source].start };
    // SAFETY: The first source is valid.
    let mut source_end = unsafe { (*copy).src[source].end };
    // SAFETY: The first source is valid.
    let mut split = unsafe { (*copy).split };
    // SAFETY: The first source is valid.
    let mut source_max = unsafe { (*copy).src[source].max };
    // SAFETY: The first source is valid.
    let mut source_type = unsafe { (*copy).src[source].mt };
    let mut destination_type;

    loop {
        let mut destination_offset = 0u8;
        let mut destination_max = 0;
        // SAFETY: The destination index is valid.
        dst = unsafe { (*copy).dst[destination].node };
        // SAFETY: The destination index is valid.
        destination_type = unsafe { (*copy).dst[destination].mt };
        let mut destination_size = split.wrapping_add(1);

        while destination_size != 0 {
            let source_size = source_end.wrapping_sub(source_offset).wrapping_add(1);
            let size = if source_size < destination_size {
                source_size
            } else {
                destination_size
            };

            // SAFETY: Copy a bounded number of source entries.
            destination_max = unsafe {
                spanning_node_copy(
                    mas,
                    NodeCopySource {
                        node: src,
                        start: source_offset,
                        size,
                        max: source_max,
                        node_type: source_type,
                    },
                    NodeCopyDestination {
                        node: dst,
                        start: destination_offset,
                        node_type: destination_type,
                    },
                )
            };

            destination_offset = destination_offset.wrapping_add(size);
            source_offset = source_offset.wrapping_add(size);
            if source_offset > source_end {
                source += 1;
                // SAFETY: The copy is complete unless more data remains.
                if source >= usize::from(unsafe { (*copy).s_count }) {
                    // SAFETY: Record the final destination maximum.
                    unsafe { (*copy).dst[destination].max = destination_max };
                    // SAFETY: Finalize the complete destination.
                    unsafe { spanning_node_finalise(dst, destination_type, destination_offset) };
                    return;
                }

                // SAFETY: Load the next valid source.
                unsafe {
                    src = (*copy).src[source].node;
                    source_offset = (*copy).src[source].start;
                    source_end = (*copy).src[source].end;
                    source_max = (*copy).src[source].max;
                    source_type = (*copy).src[source].mt;
                }
            }

            destination_size = destination_size.wrapping_sub(size);
            data_offset = data_offset.wrapping_add(size);
        }

        // SAFETY: The copy node is valid.
        split = unsafe { (*copy).split };
        // SAFETY: Record the calculated destination maximum.
        unsafe { (*copy).dst[destination].max = destination_max };
        // SAFETY: The last copied slot is valid.
        let last_slot = unsafe {
            *super::maple_tree_core::slots(dst, destination_type)
                .add(usize::from(destination_offset.wrapping_sub(1)))
        };
        if destination_max != !0 && last_slot.is_null() {
            // SAFETY: The current source boundary is valid.
            if source_offset == unsafe { (*copy).src[source].start } {
                source = source.wrapping_sub(1);
                // SAFETY: Load the previous source boundary.
                unsafe {
                    src = (*copy).src[source].node;
                    source_end = (*copy).src[source].end;
                    source_max = (*copy).src[source].max;
                    source_type = (*copy).src[source].mt;
                    source_offset = source_end;
                }
            } else {
                source_offset = source_offset.wrapping_sub(1);
            }

            split = split.wrapping_add(1);
            data_offset = data_offset.wrapping_sub(1);
            destination_offset = destination_offset.wrapping_sub(1);
            // SAFETY: The adjusted destination maximum is a valid pivot.
            unsafe {
                (*copy).dst[destination].max =
                    *super::maple_tree_core::pivots(dst, destination_type)
                        .add(usize::from(destination_offset.wrapping_sub(1)));
            }
        }

        // SAFETY: Finalize the complete destination.
        unsafe { spanning_node_finalise(dst, destination_type, destination_offset) };
        destination += 1;
        // SAFETY: The destination count was set by destination setup.
        let destination_count = usize::from(unsafe { (*copy).d_count });
        if destination + 1 == destination_count {
            // SAFETY: Allocate the remaining data to the final destination.
            split = unsafe { (*copy).data.wrapping_sub(data_offset) };
        }
        if destination >= destination_count {
            // SAFETY: All source data must be consumed.
            unsafe { bindings::rust_maple_tree_warn_on_once(data_offset < (*copy).data) };
            return;
        }
        // SAFETY: The copy data size is valid.
        let copy_data = unsafe { (*copy).data };
        if data_offset > copy_data {
            break;
        }
    }
}

/// Converts replacement nodes into the encoded copy slots.
///
/// # Safety
///
/// Every replacement node must be complete and owned by this write.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn copy_destinations_to_slots(
    copy: *mut bindings::maple_copy,
    min: usize,
    max: usize,
    mas: *mut MaState,
) {
    // SAFETY: The caller supplies a valid tree.
    let allocation_tree = unsafe { spanning_is_alloc((*mas).tree) };
    let mut slot_min = min;
    // SAFETY: The destination count is valid.
    let destination_count = usize::from(unsafe { (*copy).d_count });

    for destination in 0..destination_count {
        // SAFETY: The destination index is valid.
        let node = unsafe { (*copy).dst[destination].node };
        // SAFETY: The destination index is valid.
        let node_type = unsafe { (*copy).dst[destination].mt };
        // SAFETY: The destination index is valid.
        let slot_max = unsafe { (*copy).dst[destination].max };

        // SAFETY: The destination slot is private to this writer.
        unsafe {
            spanning_init_slot((*copy).slot.as_mut_ptr().add(destination), node, node_type);
        }
        // SAFETY: The private copy pivot is available.
        unsafe { copy_set_pivot(copy, destination, slot_max) };

        if allocation_tree {
            if super::maple_tree_core::is_leaf(node_type) {
                // SAFETY: The completed leaf layout is valid.
                let gap = unsafe {
                    spanning_leaf_gap(
                        node,
                        node_type,
                        slot_min,
                        slot_max,
                        super::maple_tree_core::pivots(node, node_type),
                        super::maple_tree_core::slots(node, node_type),
                    )
                };
                // SAFETY: The private gap slot is available.
                unsafe { (*copy).gap[destination] = gap };
            } else {
                // SAFETY: Read the allocation-range metadata.
                let gaps = unsafe { spanning_gaps(node, node_type) };
                if !gaps.is_null() {
                    // SAFETY: The metadata gap offset is valid.
                    let gap_slot = unsafe { spanning_meta_gap(node) };
                    // SAFETY: The gap offset is within this node.
                    unsafe { (*copy).gap[destination] = *gaps.add(usize::from(gap_slot)) };
                }
            }
        }

        slot_min = slot_max.wrapping_add(1);
    }

    // SAFETY: The complete destination range is known.
    unsafe {
        (*copy).end = (*copy).d_count.wrapping_sub(1);
        (*copy).min = min;
        copy_set_max(copy, max);
    }
}

/// Publishes one replacement node as a new root when it covers the full range.
///
/// # Safety
///
/// The copy destinations must be complete and the write lock must be held.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn copy_is_new_root(copy: *mut bindings::maple_copy, mas: *mut MaState) -> bool {
    // SAFETY: The copy range is valid.
    if unsafe { (*copy).min != 0 || copy_max(copy) != !0 } {
        return false;
    }

    // SAFETY: The destination count is valid.
    if unsafe { (*copy).d_count } != 1 {
        // SAFETY: The tree mode selects the root node layout.
        let root_type = if unsafe { spanning_is_alloc((*mas).tree) } {
            bindings::maple_type_maple_arange_64
        } else {
            bindings::maple_type_maple_range_64
        };

        // SAFETY: Convert all completed destinations into root slots.
        unsafe {
            (*copy).data = (*copy).d_count;
            (*copy).s_count = 0;
            destination_setup(copy, mas, root_type);
            init_copy_source(copy);
        }
        // SAFETY: The simulated root contains the complete destination slots.
        unsafe {
            spanning_node_copy(
                mas,
                NodeCopySource {
                    node: (*copy).src[0].node,
                    start: 0,
                    size: (*copy).data,
                    max: copy_max(copy),
                    node_type: bindings::maple_type_maple_copy,
                },
                NodeCopyDestination {
                    node: (*copy).dst[0].node,
                    start: 0,
                    node_type: root_type,
                },
            )
        };
        // SAFETY: Finalize the root node.
        unsafe {
            spanning_node_finalise((*copy).dst[0].node, root_type, (*copy).end.wrapping_add(1))
        };
        // SAFETY: The root slot is private until publication.
        unsafe {
            spanning_init_slot((*copy).slot.as_mut_ptr(), (*copy).dst[0].node, root_type);
            (*copy).height = (*copy).height.wrapping_add(1);
        }
    }

    // SAFETY: Check the private root slot against the selected root node.
    let root_matches = unsafe { spanning_root_matches(mas, copy) };
    // SAFETY: A mismatch is a Maple Tree invariant violation.
    unsafe { bindings::rust_maple_tree_warn_on_once(!root_matches) };
    // SAFETY: Install the completed root location in the write state.
    unsafe { spanning_reset_root(mas, (*copy).dst[0].node) };
    true
}

/// Advances the spanning write to its parent when another level is required.
///
/// # Safety
///
/// Both write states and the completed copy destinations must be valid.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn spanning_ascend(
    copy: *mut bindings::maple_copy,
    mas: *mut MaState,
    left: *mut MaWrState,
    right: *mut MaWrState,
    sibling: *mut MaState,
) -> bool {
    // SAFETY: The sibling state is valid.
    if unsafe { (*sibling).end } != 0 {
        // SAFETY: The left state is valid.
        let left_state = unsafe { (*left).mas };
        // SAFETY: The sibling and left range are valid.
        if unsafe { (*sibling).max < (*left_state).min } {
            // SAFETY: Replace the complete left traversal state.
            unsafe { *left_state = *sibling };
        } else {
            // SAFETY: Replace the complete right traversal state.
            unsafe { *(*right).mas = *sibling };
        }
    }

    // SAFETY: Both traversal states are valid.
    let left_state = unsafe { (*left).mas };
    // SAFETY: Both traversal states are valid.
    let right_state = unsafe { (*right).mas };
    // SAFETY: Convert completed destinations for the parent level.
    unsafe { copy_destinations_to_slots(copy, (*left_state).min, (*right_state).max, mas) };
    // SAFETY: A one-destination global copy becomes a new root.
    if unsafe { copy_is_new_root(copy, mas) } {
        return false;
    }

    // SAFETY: The destination and traversal states are valid.
    if unsafe { (*copy).d_count == 1 && (*left_state).node == (*right_state).node } {
        // SAFETY: Preserve the current encoded parent on the converged node.
        unsafe { spanning_set_parent((*copy).dst[0].node, mas) };
        return false;
    }

    // SAFETY: Account for the parent level.
    unsafe { (*copy).height = (*copy).height.wrapping_add(1) };
    // SAFETY: Move each side to its parent write state.
    unsafe {
        spanning_wr_ascend(left);
        spanning_wr_ascend(right);
    }
    true
}

/// Executes a native Rust spanning store across one or more tree levels.
///
/// # Safety
///
/// `wr_mas` must describe a selected spanning write under the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
unsafe fn spanning_store(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: Emit the operation tracepoint from the selected state.
    unsafe { trace_op(mas) };

    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).index == 0 && (*mas).last == !0 } {
        // SAFETY: The complete range becomes a single root.
        unsafe { spanning_new_root(mas, (*wr_mas).entry) };
        return;
    }

    // SAFETY: Copy the selected right traversal state.
    let mut right_mas = unsafe { *mas };
    if right_mas.last.wrapping_add(1) != 0 {
        right_mas.last = right_mas.last.wrapping_add(1);
    }
    right_mas.index = right_mas.last;
    // SAFETY: The caller supplies a valid write state.
    let entry = unsafe { (*wr_mas).entry };
    let mut right_wr = MaWrState {
        mas: &raw mut right_mas,
        entry,
        ..Default::default()
    };

    // SAFETY: Walk the right side to the leaf containing the next slot.
    unsafe { spanning_walk_index(&raw mut right_wr) };
    // SAFETY: Restore the requested final index.
    let requested_last = unsafe { (*mas).last };
    right_mas.last = requested_last;
    right_mas.index = requested_last;
    right_wr.end_piv = right_wr.r_max;

    // SAFETY: Walk the left side to its leaf.
    unsafe { spanning_walk_index(wr_mas) };

    // SAFETY: A NULL write extends over adjacent NULL ranges.
    if unsafe { (*wr_mas).entry.is_null() } {
        // SAFETY: Both sides identify one spanning write.
        unsafe { extend_spanning_null(wr_mas, &raw mut right_wr) };
        // SAFETY: Use the extended final range.
        unsafe { (*mas).last = right_mas.last };
    }

    // SAFETY: Extending NULLs can cover the entire tree.
    if unsafe { (*mas).index == 0 && right_mas.last == !0 } {
        // SAFETY: Reset the walk state and set the exact complete range.
        unsafe { spanning_set_full_range(mas) };
        // SAFETY: Replace the complete tree with one root.
        unsafe { spanning_new_root(mas, (*wr_mas).entry) };
        return;
    }

    let mut copy = bindings::maple_copy::default();
    let mut sibling = MaState::default();

    // SAFETY: Both sides identify one selected spanning write.
    unsafe { copy_leaf_init(&raw mut copy, mas, wr_mas, &raw mut right_wr) };
    loop {
        // SAFETY: The copy data and possible sibling are valid.
        unsafe { spanning_data(&raw mut copy, wr_mas, &raw mut right_wr, &raw mut sibling) };
        // SAFETY: Configure every source for the copy engine.
        unsafe { multi_source_setup(&raw mut copy, wr_mas, &raw mut right_wr, &raw mut sibling) };
        // SAFETY: Select and allocate the bounded destination set.
        unsafe { destination_setup(&raw mut copy, mas, (*wr_mas).type_) };
        // SAFETY: Write every source into the replacement nodes.
        unsafe { copy_data_write(&raw mut copy, mas) };
        // SAFETY: Continue at the parent only when required.
        if !unsafe {
            spanning_ascend(
                &raw mut copy,
                mas,
                wr_mas,
                &raw mut right_wr,
                &raw mut sibling,
            )
        } {
            break;
        }
    }

    // SAFETY: Publish the complete replacement subtree.
    unsafe { spanning_publish(mas, &raw mut copy) };
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
        #[cfg(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE)]
        bindings::store_type_wr_spanning_store => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { spanning_store(wr_mas) };
        }
        #[cfg(not(CONFIG_RUST_MAPLE_TREE_SPANNING_STORE))]
        bindings::store_type_wr_spanning_store => {
            // SAFETY: The selected complex strategy remains a retained boundary.
            unsafe { store_complex(wr_mas) };
        }
        #[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
        bindings::store_type_wr_new_root => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { root_store_new_root(mas, (*wr_mas).entry) };
        }
        #[cfg(CONFIG_RUST_MAPLE_TREE_ROOT_STORE)]
        bindings::store_type_wr_store_root => {
            // SAFETY: The selected strategy and state are valid.
            unsafe { root_store_entry(mas, (*wr_mas).entry) };
        }
        #[cfg(not(CONFIG_RUST_MAPLE_TREE_ROOT_STORE))]
        bindings::store_type_wr_new_root | bindings::store_type_wr_store_root => {
            // SAFETY: The selected complex strategies remain retained boundaries.
            unsafe { store_complex(wr_mas) };
        }
        bindings::store_type_wr_split_store
        | bindings::store_type_wr_rebalance
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
