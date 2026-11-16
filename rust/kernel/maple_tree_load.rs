// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree load traversal.
//!
//! Rust owns lookup state initialization, root selection, single-entry tree
//! handling, node descent, dead-node restart, and the RCU lifecycle. C keeps
//! the Maple node encoding, node layout, protected slot access, tracepoint,
//! and lockdep primitives.

use crate::bindings;
use core::{ffi::c_void, ptr};
use macros::export;

type MapleNode = bindings::maple_node;
type MapleTree = bindings::maple_tree;
type MapleEnode = bindings::maple_enode;
type MaState = bindings::ma_state;

#[inline(always)]
fn is_start(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_start }
}

#[inline(always)]
fn is_none(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_none }
}

#[inline(always)]
fn is_ptr(mas: *const MaState) -> bool {
    // SAFETY: The caller supplies a valid Maple state.
    unsafe { (*mas).status == bindings::maple_status_ma_root }
}

#[inline(always)]
/// Reads the current tree root with the required RCU or lock protection.
///
/// # Safety
///
/// `mas` must identify a valid initialized Maple tree.
unsafe fn root(mas: *mut MaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_root(mas) }
}

#[inline(always)]
/// Converts an encoded tree node into its raw Maple node address.
///
/// # Safety
///
/// `enode` must contain a valid encoded node.
unsafe fn to_node(enode: *mut MapleEnode) -> *mut MapleNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_to_node(enode.cast::<c_void>().cast_const()).cast() }
}

#[inline(always)]
/// Returns the concrete layout type of an encoded node.
///
/// # Safety
///
/// `enode` must contain a valid encoded node.
unsafe fn node_type(enode: *const MapleEnode) -> bindings::maple_type {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_node_type(enode.cast::<c_void>()) }
}

#[inline(always)]
/// Returns the pivot array of a Maple node.
///
/// # Safety
///
/// `node` and `node_type` must identify the same live Maple node.
unsafe fn pivots(node: *mut MapleNode, node_type: bindings::maple_type) -> *mut usize {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_pivots(node, node_type) }
}

#[inline(always)]
/// Returns the slot array of a Maple node.
///
/// # Safety
///
/// `node` and `node_type` must identify the same live Maple node.
unsafe fn slots(node: *mut MapleNode, node_type: bindings::maple_type) -> *mut *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_slots(node, node_type).cast() }
}

#[inline(always)]
/// Reads one slot under RCU or the Maple tree lock.
///
/// # Safety
///
/// `mas`, `slots`, and `offset` must identify a valid slot. The caller must
/// hold the RCU read lock or the tree lock.
unsafe fn slot(mas: *mut MaState, slots: *mut *mut c_void, offset: u8) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_maple_tree_load_slot(mas, slots.cast(), offset) }
}

/// Initializes the Maple state used by the public load operation.
fn new_state(tree: *mut MapleTree, index: usize) -> MaState {
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
unsafe fn start(mas: *mut MaState) -> *mut c_void {
    if !is_start(mas) {
        return ptr::null_mut();
    }

    // SAFETY: The state is valid and protected by the caller.
    unsafe { bindings::rust_maple_tree_load_init_lock_check(mas) };
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
        if unsafe { bindings::rust_maple_tree_load_is_node(entry) } {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).depth = 0;
                (*mas).status = bindings::maple_status_ma_active;
                (*mas).node = bindings::rust_maple_tree_load_safe_root(entry).cast();
                (*mas).offset = 0;
            }

            // SAFETY: `mas.node` now contains a root encoded node.
            if unsafe { bindings::rust_maple_tree_load_enode_is_dead((*mas).node.cast()) } {
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
                (*mas).offset = bindings::rust_maple_tree_load_node_slots();
            }
            return ptr::null_mut();
        }

        // A non-node root covers exactly index zero.
        // SAFETY: The caller supplies a valid writable state.
        unsafe {
            (*mas).status = bindings::maple_status_ma_root;
            (*mas).offset = bindings::rust_maple_tree_load_node_slots();
        }
        // SAFETY: The caller supplies a valid Maple state.
        if unsafe { (*mas).index } > 0 {
            return ptr::null_mut();
        }

        return entry;
    }
}

/// Descends a lookup from the active root node to a leaf entry.
///
/// # Safety
///
/// `mas` must be active and protected by RCU or the tree lock.
unsafe fn lookup_walk(mas: *mut MaState) -> *mut c_void {
    // SAFETY: The caller guarantees that the active state has an encoded node.
    let mut next = unsafe { (*mas).node };

    loop {
        // SAFETY: `next` is an encoded node reached from a protected tree.
        let node = unsafe { to_node(next) };
        // SAFETY: `next` is a live encoded node.
        let node_type = unsafe { node_type(next.cast_const()) };
        // SAFETY: `node` and `node_type` describe the same live node.
        let node_pivots = unsafe { pivots(node, node_type) };
        // SAFETY: The layout type is valid.
        let pivot_count = unsafe { bindings::rust_maple_tree_load_pivot_count(node_type) };

        let mut offset = 0u8;
        loop {
            // SAFETY: `offset` is below the type's pivot count.
            if unsafe { *node_pivots.add(usize::from(offset)) } >= unsafe { (*mas).index } {
                break;
            }
            offset += 1;
            if offset >= pivot_count {
                break;
            }
        }

        // SAFETY: `node` and `node_type` describe the same live node.
        let node_slots = unsafe { slots(node, node_type) };
        // SAFETY: The slot offset is within this node and the caller holds
        // RCU or the tree lock.
        next = unsafe { slot(mas, node_slots, offset).cast() };

        // SAFETY: The node was acquired under RCU or the tree lock.
        if unsafe { bindings::rust_maple_tree_load_node_is_dead(node) } {
            // SAFETY: The caller supplies a valid writable state.
            unsafe {
                (*mas).status = bindings::maple_status_ma_start;
                (*mas).node = ptr::null_mut();
            }
            return ptr::null_mut();
        }

        // SAFETY: The layout type is valid.
        if unsafe { bindings::rust_maple_tree_load_is_leaf(node_type) } {
            return next.cast();
        }
    }
}

/// Releases an RCU read-side critical section on drop.
struct RcuGuard;

impl Drop for RcuGuard {
    fn drop(&mut self) {
        // SAFETY: This guard represents one held RCU read-side section.
        unsafe { bindings::rcu_read_unlock() };
    }
}

#[inline(always)]
fn rcu_read_lock() -> RcuGuard {
    // SAFETY: The matching unlock is performed by the returned guard.
    unsafe { bindings::rcu_read_lock() };
    RcuGuard
}

/// Implements the complete public Maple Tree load operation.
///
/// # Safety
///
/// `tree` must point to a valid initialized Maple tree.
unsafe fn load(tree: *mut MapleTree, index: usize) -> *mut c_void {
    let mut state = new_state(tree, index);

    // SAFETY: The complete state has been initialized.
    unsafe { bindings::rust_maple_tree_load_trace(&mut state) };

    let guard = rcu_read_lock();
    let entry = loop {
        // SAFETY: The state is valid and protected by RCU.
        let entry = unsafe { start(&mut state) };
        if is_none(&state) {
            break entry;
        }
        if is_ptr(&state) {
            break if index == 0 { entry } else { ptr::null_mut() };
        }

        // SAFETY: The active state was set up by `start`.
        let entry = unsafe { lookup_walk(&mut state) };
        if !entry.is_null() || !is_start(&state) {
            break entry;
        }
    };
    drop(guard);

    // SAFETY: Entry classification only compares the reserved pointer value.
    if unsafe { bindings::rust_maple_tree_load_is_zero(entry.cast_const()) } {
        ptr::null_mut()
    } else {
        entry
    }
}

/// Loads a Maple Tree entry through the production ABI.
///
/// # Safety
///
/// The C caller must provide a valid initialized Maple tree.
#[export]
unsafe extern "C" fn rust_mtree_load(tree: *mut MapleTree, index: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { load(tree, index) }
}
