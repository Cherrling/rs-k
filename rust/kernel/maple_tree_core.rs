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
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) fn is_overflow(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_overflow }
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

#[inline(always)]
/// Returns whether a raw Maple node is the tree root.
///
/// # Safety
///
/// `node` must contain a valid Maple node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn node_is_root(node: *const MapleNode) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_node_is_root(node) }
}

#[inline(always)]
/// Returns whether an encoded Maple node is the tree root.
///
/// # Safety
///
/// `enode` must contain a valid encoded node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn enode_is_root(enode: *const MapleEnode) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_enode_is_root(enode) }
}

#[inline(always)]
/// Returns the parent of an encoded Maple node.
///
/// # Safety
///
/// `enode` must contain a valid non-root encoded node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn parent(enode: *const MapleEnode) -> *mut MapleNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_parent(enode) }
}

#[inline(always)]
/// Returns the parent node type of an encoded Maple node.
///
/// # Safety
///
/// `mas` must identify a valid Maple tree and `enode` a valid child node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn parent_type(
    mas: *mut MaState,
    enode: *mut MapleEnode,
) -> bindings::maple_type {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_parent_type(mas, enode) }
}

#[inline(always)]
/// Returns the slot that contains an encoded Maple node.
///
/// # Safety
///
/// `enode` must contain a valid non-root encoded node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn parent_slot(enode: *const MapleEnode) -> u32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_parent_slot(enode) }
}

#[inline(always)]
/// Encodes a raw Maple node with its concrete node type.
///
/// # Safety
///
/// `node` must contain a valid live Maple node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn make_node(
    node: *const MapleNode,
    node_type: bindings::maple_type,
) -> *mut MapleEnode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_make_node(node, node_type) }
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

#[inline(always)]
/// Returns a pivot or the current node maximum when the offset is absent.
///
/// # Safety
///
/// `mas` must contain a valid active state. `pivots` must describe the
/// current node.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn safe_pivot(
    mas: *const MaState,
    pivots: *mut usize,
    offset: u8,
    node_type: bindings::maple_type,
) -> usize {
    if offset >= pivot_count(node_type) {
        // SAFETY: The caller supplies a valid active state.
        return unsafe { (*mas).max };
    }

    // SAFETY: The caller guarantees that `offset` is in the pivot array.
    unsafe { *pivots.add(usize::from(offset)) }
}

/// Resets a Maple state to start a new walk.
///
/// # Safety
///
/// `mas` must be a valid writable Maple state.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn reset(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe {
        (*mas).status = bindings::maple_status_ma_start;
        (*mas).node = ptr::null_mut();
    }
}

/// Restarts a walk at a requested index.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn rewalk(mas: *mut MaState, index: usize) {
    loop {
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            reset(mas);
            (*mas).index = index;
            (*mas).last = index;
        }

        // SAFETY: The state is start-state and protected by the caller.
        unsafe { state_walk(mas) };
        if !is_start(mas) {
            break;
        }
    }
}

/// Restarts a walk if the current node was retired.
///
/// # Safety
///
/// `mas` must be valid and protected by RCU or the tree lock. `node` must
/// belong to the current state.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn rewalk_if_dead(
    mas: *mut MaState,
    node: *const MapleNode,
    index: usize,
) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { node_is_dead(node) } {
        // SAFETY: The caller supplies a valid protected state.
        unsafe { rewalk(mas, index) };
        return true;
    }

    false
}

/// Runs the lockdep check for an already active Maple state.
///
/// # Safety
///
/// `mas` must identify a valid initialized Maple tree.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn lock_check(mas: *mut MaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_lock_check(mas) };
}

/// Emits a one-time warning for an impossible traversal state.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) fn warn_on_once(condition: bool) -> bool {
    // SAFETY: This helper only emits the kernel diagnostic.
    unsafe { bindings::rust_maple_tree_warn_on_once(condition) }
}

/// Moves a Maple state to its parent and updates the covered range.
///
/// # Safety
///
/// `mas` must contain a valid active state protected by RCU or the tree lock.
#[cfg(CONFIG_RUST_MAPLE_TREE_NEXT)]
pub(crate) unsafe fn ascend(mas: *mut MaState) -> bool {
    // SAFETY: The caller supplies a valid active state.
    let mut ancestor_node = unsafe { to_node((*mas).node) };
    // SAFETY: The caller supplies a valid active state.
    let mut mas_max = unsafe { (*mas).max };
    // SAFETY: The caller supplies a valid active state.
    let mut mas_min = unsafe { (*mas).min };
    let mut ancestor_enode;
    let mut ancestor_slot;
    let mut ancestor_type;
    let mut max;
    let mut min;
    let mut node_pivots: *mut usize;
    let mut set_max;
    let mut set_min;

    // SAFETY: The current node came from the protected state.
    if unsafe { node_is_root(ancestor_node) } {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).offset = 0 };
        return false;
    }

    // SAFETY: The caller supplies a valid non-root active state.
    let parent_node = unsafe { parent((*mas).node) };
    if ancestor_node == parent_node {
        return true;
    }

    // SAFETY: The caller supplies a valid active state and child node.
    ancestor_type = unsafe { parent_type(mas, (*mas).node) };
    // SAFETY: The caller supplies a valid non-root active state.
    ancestor_slot = unsafe { parent_slot((*mas).node) } as u8;
    // SAFETY: `parent_node` and `ancestor_type` describe the parent.
    ancestor_enode = unsafe { make_node(parent_node, ancestor_type) };
    // SAFETY: The caller supplies a valid active state.
    if parent_node != unsafe { parent((*mas).node) } {
        return true;
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe {
        (*mas).offset = ancestor_slot;
        (*mas).node = ancestor_enode;
    }

    // SAFETY: The parent was just validated as a live encoded node.
    if unsafe { enode_is_root(ancestor_enode) } {
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).max = usize::MAX;
            (*mas).min = 0;
        }
        return false;
    }

    min = 0;
    max = usize::MAX;
    set_min = false;
    set_max = false;
    if ancestor_slot == 0 {
        min = mas_min;
        set_min = true;
    }
    if mas_max == usize::MAX {
        set_max = true;
    }

    loop {
        let parent_enode = ancestor_enode;
        // SAFETY: `parent_enode` is a valid non-root encoded node.
        ancestor_type = unsafe { parent_type(mas, parent_enode) };
        // SAFETY: `parent_enode` contains a valid parent pointer.
        ancestor_node = unsafe { parent(parent_enode) };
        // SAFETY: `parent_enode` contains a valid parent slot.
        ancestor_slot = unsafe { parent_slot(parent_enode) } as u8;
        // SAFETY: The parent node and type identify the same node.
        ancestor_enode = unsafe { make_node(ancestor_node, ancestor_type) };
        // SAFETY: The ancestor type is valid.
        node_pivots = unsafe { pivots(ancestor_node, ancestor_type) };

        // SAFETY: The ancestor was reached under the caller's protection.
        if unsafe { node_is_dead(ancestor_node) } {
            return true;
        }
        if !set_min && ancestor_slot != 0 {
            set_min = true;
            // SAFETY: A nonzero parent slot has a preceding pivot.
            min = unsafe { *node_pivots.add(usize::from(ancestor_slot - 1)) } + 1;
        }
        if !set_max && ancestor_slot < pivot_count(ancestor_type) {
            set_max = true;
            // SAFETY: The parent slot is within the pivot array.
            max = unsafe { *node_pivots.add(usize::from(ancestor_slot)) };
        }

        // SAFETY: The ancestor was reached under the caller's protection.
        if unsafe { node_is_dead(ancestor_node) } {
            return true;
        }
        // SAFETY: The ancestor was reached under the caller's protection.
        if unsafe { node_is_root(ancestor_node) } || (set_min && set_max) {
            break;
        }
    }

    mas_min = min;
    mas_max = max;
    // SAFETY: The caller supplies a valid writable state.
    unsafe {
        (*mas).min = mas_min;
        (*mas).max = mas_max;
    }
    false
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
