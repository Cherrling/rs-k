// SPDX-License-Identifier: GPL-2.0

//! Shared Maple Tree traversal primitives written in Rust.
//!
//! C retains node encoding, node layout, protected slot access, traversal
//! metadata, and lockdep primitives. Rust owns the state transitions and
//! traversal policy that consume those primitives.

use crate::bindings;
use core::{ffi::c_void, ptr};

pub(crate) type MapleNode = bindings::maple_node;
pub(crate) type MapleTree = bindings::maple_tree;
pub(crate) type MapleEnode = bindings::maple_enode;
pub(crate) type MaState = bindings::ma_state;

#[inline(always)]
pub(crate) fn is_start(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_start }
}

#[inline(always)]
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) fn is_active(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_active }
}

#[inline(always)]
pub(crate) fn is_none(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_none }
}

#[inline(always)]
pub(crate) fn is_ptr(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_root }
}

#[inline(always)]
/// Reads the current tree root with the required RCU or lock protection.
///
/// # Safety
///
/// `mas` must identify a valid initialized Maple tree.
pub(crate) unsafe fn root(mas: *mut MaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_root(mas) }
}

#[inline(always)]
/// Converts an encoded tree node into its raw Maple node address.
///
/// # Safety
///
/// `enode` must contain a valid encoded node.
pub(crate) unsafe fn to_node(enode: *mut MapleEnode) -> *mut MapleNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_to_node(enode.cast::<c_void>().cast_const()).cast() }
}

#[inline(always)]
/// Returns the concrete layout type of an encoded node.
///
/// # Safety
///
/// `enode` must contain a valid encoded node.
pub(crate) unsafe fn node_type(enode: *const MapleEnode) -> bindings::maple_type {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_node_type(enode.cast::<c_void>()) }
}

#[inline(always)]
/// Returns the pivot array of a Maple node.
///
/// # Safety
///
/// `node` and `node_type` must identify the same live Maple node.
pub(crate) unsafe fn pivots(node: *mut MapleNode, node_type: bindings::maple_type) -> *mut usize {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_pivots(node, node_type) }
}

#[inline(always)]
/// Returns the slot array of a Maple node.
///
/// # Safety
///
/// `node` and `node_type` must identify the same live Maple node.
pub(crate) unsafe fn slots(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
) -> *mut *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_slots(node, node_type).cast() }
}

#[inline(always)]
/// Reads one slot under RCU or the Maple tree lock.
///
/// # Safety
///
/// `mas`, `slots`, and `offset` must identify a valid slot. The caller must
/// hold the RCU read lock or the tree lock.
pub(crate) unsafe fn slot(mas: *mut MaState, slots: *mut *mut c_void, offset: u8) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_slot(mas, slots.cast(), offset) }
}

#[inline(always)]
/// Returns the number of pivots stored by a concrete node type.
pub(crate) fn pivot_count(node_type: bindings::maple_type) -> u8 {
    // SAFETY: The layout type is valid.
    unsafe { bindings::rust_maple_tree_pivot_count(node_type) }
}

#[inline(always)]
/// Returns the traversal end offset for this node.
///
/// # Safety
///
/// `node`, `node_type`, and `pivots` must describe the same live node.
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) unsafe fn data_end(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    pivots: *mut usize,
    max: usize,
) -> u8 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_data_end(node, node_type, pivots, max) }
}

#[inline(always)]
/// Returns whether a concrete node type is a leaf.
pub(crate) fn is_leaf(node_type: bindings::maple_type) -> bool {
    // SAFETY: The layout type is valid.
    unsafe { bindings::rust_maple_tree_is_leaf(node_type) }
}

#[inline(always)]
/// Returns whether the dead-node read barrier found a dead node.
///
/// # Safety
///
/// `node` must identify a live Maple node.
pub(crate) unsafe fn node_is_dead(node: *const MapleNode) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_node_is_dead(node) }
}

/// Initializes a Maple state for a start-state walk.
pub(crate) fn new_state(tree: *mut MapleTree, index: usize) -> MaState {
    MaState {
        tree,
        index,
        last: index,
        node: ptr::null_mut(),
        status: bindings::maple_status_ma_start,
        min: 0,
        max: usize::MAX,
        ..Default::default()
    }
}

/// Sets up a start-state walk at the root.
///
/// # Safety
///
/// `mas` must be a valid Maple state protected by RCU or the tree lock.
pub(crate) unsafe fn start(mas: *mut MaState) -> *mut c_void {
    if !is_start(mas) {
        return ptr::null_mut();
    }

    // SAFETY: The state is valid and protected by the caller.
    unsafe { bindings::rust_maple_tree_init_lock_check(mas) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe {
        (*mas).min = 0;
        (*mas).max = usize::MAX;
    }

    loop {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).depth = 0 };
        // SAFETY: The state and tree are valid and protected by the caller.
        let entry = unsafe { root(mas) };

        // SAFETY: Entry classification only inspects pointer bits.
        if unsafe { bindings::rust_maple_tree_is_node(entry) } {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).depth = 0;
                (*mas).status = bindings::maple_status_ma_active;
                (*mas).node = bindings::rust_maple_tree_safe_root(entry).cast();
                (*mas).offset = 0;
            }

            // SAFETY: `mas.node` now contains a root encoded node.
            if unsafe { bindings::rust_maple_tree_enode_is_dead((*mas).node.cast()) } {
                continue;
            }

            return ptr::null_mut();
        }

        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).node = ptr::null_mut() };
        if entry.is_null() {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).status = bindings::maple_status_ma_none;
                (*mas).offset = bindings::rust_maple_tree_node_slots();
            }
            return ptr::null_mut();
        }

        // A non-node root covers exactly index zero.
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).status = bindings::maple_status_ma_root;
            (*mas).offset = bindings::rust_maple_tree_node_slots();
        }
        // SAFETY: The caller supplies a valid Maple state.
        if unsafe { (*mas).index } > 0 {
            return ptr::null_mut();
        }

        return entry;
    }
}

/// Performs a stateful walk and records the containing range in `mas`.
///
/// # Safety
///
/// `mas` must be active and protected by RCU or the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) unsafe fn range_walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: The caller guarantees that the active state has an encoded node.
    let mut next = unsafe { (*mas).node };
    // SAFETY: The caller supplies a valid active state.
    let mut min = unsafe { (*mas).min };
    // SAFETY: The caller supplies a valid active state.
    let mut max = unsafe { (*mas).max };

    loop {
        let last = next;
        // SAFETY: `next` is an encoded node reached from a protected tree.
        let node = unsafe { to_node(next) };
        // SAFETY: `next` is a live encoded node.
        let node_type = unsafe { node_type(next.cast_const()) };
        // SAFETY: `node` and `node_type` describe the same live node.
        let node_pivots = unsafe { pivots(node, node_type) };
        // SAFETY: The node and protected range describe a live traversal.
        let end = unsafe { data_end(node, node_type, node_pivots, max) };
        let prev_min = min;
        let prev_max = max;
        let mut offset;

        // SAFETY: A live node stores a valid first pivot.
        if unsafe { *node_pivots } >= unsafe { (*mas).index } {
            offset = 0;
            // SAFETY: The first pivot bounds the selected slot.
            max = unsafe { *node_pivots };
        } else {
            offset = 1;
            loop {
                if offset >= end {
                    break;
                }

                // SAFETY: `offset` is below the traversal end.
                let pivot = unsafe { *node_pivots.add(usize::from(offset)) };
                // SAFETY: The caller supplies a valid active state.
                if pivot >= unsafe { (*mas).index } {
                    max = pivot;
                    break;
                }
                offset += 1;
            }

            // SAFETY: `offset` is at least one and below the pivot count.
            min = unsafe { *node_pivots.add(usize::from(offset - 1)) } + 1;
        }

        // SAFETY: `node` and `node_type` describe the same live node.
        let node_slots = unsafe { slots(node, node_type) };
        // SAFETY: The slot offset is within this node and the caller holds
        // RCU or the tree lock.
        next = unsafe { slot(mas, node_slots, offset).cast() };

        // SAFETY: The node was acquired under RCU or the tree lock.
        if unsafe { node_is_dead(node) } {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).status = bindings::maple_status_ma_start;
                (*mas).node = ptr::null_mut();
            }
            return ptr::null_mut();
        }

        if is_leaf(node_type) {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).end = end;
                (*mas).offset = offset;
                (*mas).index = min;
                (*mas).last = max;
                (*mas).min = prev_min;
                (*mas).max = prev_max;
                (*mas).node = last;
            }
            return next.cast();
        }
    }
}

/// Walks from the current state to a node-backed entry.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) unsafe fn state_walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let entry = unsafe { start(mas) };
    if is_none(mas) || is_ptr(mas) {
        return entry;
    }

    // SAFETY: The state is active and protected by the caller.
    unsafe { range_walk(mas) }
}

/// Resets the state to restart from the current tree root.
///
/// # Safety
///
/// `mas` must be a valid writable Maple state.
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) unsafe fn make_walkable(mas: *mut MaState) {
    if !is_active(mas) && !is_start(mas) {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { (*mas).status = bindings::maple_status_ma_start };
    }
}

/// Updates lockdep and strict RCU bookkeeping for a walk.
///
/// # Safety
///
/// `mas` must identify a valid initialized Maple tree.
#[cfg(CONFIG_RUST_MAPLE_TREE_WALK)]
pub(crate) unsafe fn may_init_lock_check(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_may_init_lock_check(mas) };
}

/// Releases an RCU read-side critical section on drop.
pub(crate) struct RcuGuard;

impl Drop for RcuGuard {
    fn drop(&mut self) {
        // SAFETY: This guard represents one held RCU read-side section.
        unsafe { bindings::rcu_read_unlock() };
    }
}

#[inline(always)]
pub(crate) fn rcu_read_lock() -> RcuGuard {
    // SAFETY: The matching unlock is performed by the returned guard.
    unsafe { bindings::rcu_read_lock() };
    RcuGuard
}
