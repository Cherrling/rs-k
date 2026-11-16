// SPDX-License-Identifier: GPL-2.0

//! Native XArray root expansion.
//!
//! Rust owns the expansion height calculation, repeated root wrapping, mark
//! propagation, parent-link order, and publication order. C retains slab
//! allocation, node layout, mark bitmaps, root publication, update callbacks,
//! and the XArray entry encoding.

use crate::bindings;
use core::ffi::c_void;
use macros::export;

type Mark = bindings::xa_mark_t;
type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_SHIFT: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
const CHUNK_SIZE: usize = 1usize << CHUNK_SHIFT;
const EINVAL: i32 = bindings::rust_xarray_expand_constant_RUST_XARRAY_EINVAL as i32;
const ENOMEM: i32 = bindings::rust_xarray_expand_constant_RUST_XARRAY_ENOMEM as i32;
const MARK_MAX: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_MARK_MAX;
const FREE_MARK: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;

#[inline(always)]
fn is_node(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_node(entry) }
}

#[inline(always)]
fn is_value(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_value(entry) }
}

#[inline(always)]
fn to_node(entry: *mut c_void) -> *mut XaNode {
    // SAFETY: The caller checks that `entry` is a node entry first.
    unsafe { bindings::xarray_to_node(entry) }
}

#[inline(always)]
fn mk_node(node: *const XaNode) -> *mut c_void {
    // SAFETY: The encoding accepts every live node pointer.
    unsafe { bindings::xarray_mk_node(node) }
}

#[inline(always)]
/// Allocates and initializes one XArray node.
///
/// # Safety
///
/// `xas` must point to a valid, locked XArray state.
unsafe fn alloc_node(xas: *mut XaState, shift: u32) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_alloc_node(xas, shift) }
}

#[inline(always)]
/// Notifies the retained XArray update callback.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn notify_node(xas: *mut XaState, node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_notify_node(xas, node) }
}

#[inline(always)]
/// Reports whether the XArray tracks free entries.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn track_free(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

#[inline(always)]
/// Reads a mark from the root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn root_marked(xa: *const XArray, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_marked(xa, mark) }
}

#[inline(always)]
/// Sets a mark in the root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn root_mark_set(xa: *mut XArray, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_mark_set(xa, mark) };
}

#[inline(always)]
/// Sets a mark on one slot in a live node.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn node_mark_set(node: *mut XaNode, offset: u32, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_test_and_set(node, offset, mark) };
}

#[inline(always)]
/// Clears a mark from one slot in a live node.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn node_mark_clear(node: *mut XaNode, offset: u32, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_test_and_clear(node, offset, mark) };
}

#[inline(always)]
/// Sets a mark on every slot in a live node.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn node_mark_all(node: *mut XaNode, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_all(node, mark) };
}

#[inline(always)]
/// Initializes slot zero and publishes `head` through `RCU_INIT_POINTER()`.
///
/// # Safety
///
/// `node` must be a newly allocated node and `head` must remain live.
unsafe fn init_slot_zero(node: *mut XaNode, head: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_slot_zero_init(node, head) };
}

#[inline(always)]
/// Connects an existing root child to its new parent.
///
/// # Safety
///
/// `child` and `parent` must be live and the XArray lock must be held.
unsafe fn link_root_child(child: *mut XaNode, parent: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_link_root_child(child, parent) };
}

#[inline(always)]
/// Publishes a new root through `rcu_assign_pointer()`.
///
/// # Safety
///
/// `xas` must be valid, the lock must be held, and `head` must be fully
/// initialized and remain live.
unsafe fn publish_root(xas: *mut XaState, head: *mut c_void) {
    // SAFETY: A null node selects the root slot.
    unsafe { bindings::xarray_assign_slot(xas, core::ptr::null_mut(), 0, head) };
}

#[inline(always)]
/// Preserves the retained width check before allocating another level.
///
/// # Safety
///
/// `xas` must point to a valid locked XArray state.
unsafe fn check_width(xas: *mut XaState, shift: u32) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_expand_check(xas, shift) }
}

/// Calculates the maximum index covered by the current root.
fn max_index(entry: *mut c_void) -> usize {
    if !is_node(entry) {
        return 0;
    }

    let node = to_node(entry);
    // SAFETY: The node-entry check above establishes a live node.
    CHUNK_SIZE
        .wrapping_shl(u32::from(unsafe { (*node).shift }))
        .wrapping_sub(1)
}

/// Calculates the highest index required by the current cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn max_required(xas: *const XaState) -> usize {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let mut max = state.xa_index;

    if state.xa_shift != 0 || state.xa_sibs != 0 {
        let size = (usize::from(state.xa_sibs) + 1).wrapping_shl(u32::from(state.xa_shift));
        let mask = size.wrapping_sub(1);
        max |= mask;
        if mask == max {
            max = max.wrapping_add(1);
        }
    }

    max
}

/// Grows the root until it can contain the current cursor index.
///
/// # Safety
///
/// `xas` must point to a valid locked XArray state and `head` must contain the
/// current root entry.
pub(crate) unsafe fn expand(xas: *mut XaState, mut head: *mut c_void) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    let max = unsafe { max_required(xas) };
    let mut node = core::ptr::null_mut();
    let mut shift = 0u32;

    if head.is_null() {
        if max == 0 {
            return 0;
        }
        while (max >> shift) >= CHUNK_SIZE {
            shift += CHUNK_SHIFT;
        }
        return (shift + CHUNK_SHIFT) as i32;
    }

    if is_node(head) {
        node = to_node(head);
        // SAFETY: `head` is a live node entry.
        shift = u32::from(unsafe { (*node).shift }) + CHUNK_SHIFT;
    }

    // SAFETY: The state is valid and the caller holds the XArray lock.
    unsafe { (*xas).xa_node = core::ptr::null_mut() };

    while max > max_index(head) {
        // SAFETY: The state is valid and locked.
        if !unsafe { check_width(xas, shift) } {
            return EINVAL;
        }
        // SAFETY: The state is valid and locked.
        node = unsafe { alloc_node(xas, shift) };
        if node.is_null() {
            return -ENOMEM;
        }

        // SAFETY: `node` was just allocated for exclusive use.
        unsafe {
            (*node).count = 1;
            (*node).nr_values = u8::from(is_value(head));
        }
        // SAFETY: `node` is new and `head` stays reachable through slot zero.
        unsafe { init_slot_zero(node, head) };

        // SAFETY: The XArray is live and locked.
        let xa = unsafe { (*xas).xa };
        let mut mark: Mark = 0;

        // Propagate aggregate marks from the old root into the new root.
        loop {
            // SAFETY: The XArray is live and the lock protects root flags.
            if unsafe { track_free(xa) } && mark == FREE_MARK {
                // SAFETY: `node` is new and owned by this expansion.
                unsafe { node_mark_all(node, FREE_MARK) };
                // SAFETY: The XArray is live and the lock protects root flags.
                if !unsafe { root_marked(xa, FREE_MARK) } {
                    // SAFETY: `node` is new and slot zero exists.
                    unsafe { node_mark_clear(node, 0, FREE_MARK) };
                    // SAFETY: The XArray is live and locked.
                    unsafe { root_mark_set(xa, FREE_MARK) };
                }
            // SAFETY: The XArray is live and the lock protects root flags.
            } else if unsafe { root_marked(xa, mark) } {
                // SAFETY: `node` is new and slot zero exists.
                unsafe { node_mark_set(node, 0, mark) };
            }

            if mark == MARK_MAX {
                break;
            }
            mark += 1;
        }

        if is_node(head) {
            // SAFETY: Both nodes are live and the old root becomes a child.
            unsafe { link_root_child(to_node(head), node) };
        }

        head = mk_node(node);
        // SAFETY: `node` is fully initialized and remains live.
        unsafe { publish_root(xas, head) };
        // SAFETY: `node` is the new live root.
        unsafe { notify_node(xas, node) };

        shift += CHUNK_SHIFT;
    }

    // SAFETY: The state is valid and the caller holds the XArray lock.
    unsafe { (*xas).xa_node = node };
    shift as i32
}

/// Expands the XArray root for the current cursor.
///
/// # Safety
///
/// `xas` must point to a valid locked XArray state. `head` must contain the
/// current root entry.
#[export]
unsafe extern "C" fn rust_xarray_expand(xas: *mut XaState, head: *mut c_void) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { expand(xas, head) }
}
