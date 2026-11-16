// SPDX-License-Identifier: GPL-2.0

//! Native XArray subtree reclamation.
//!
//! Rust owns the post-order traversal, retry-marker publication, node
//! accounting reset, and reclamation order. C retains the node layout, parent
//! lookup, update callbacks, and RCU reclamation primitive.

use crate::bindings;
use core::ffi::c_void;
use macros::export;

type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK;

#[inline(always)]
fn is_node(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_node(entry) }
}

#[inline(always)]
fn to_node(entry: *mut c_void) -> *mut XaNode {
    // SAFETY: The caller checks that `entry` is a node entry first.
    unsafe { bindings::xarray_to_node(entry) }
}

#[inline(always)]
/// Loads a locked slot from a live node.
///
/// # Safety
///
/// The XArray lock must be held and the node and offset must identify a slot.
unsafe fn load_slot(xas: *const XaState, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_locked(xas, node, offset) }
}

#[inline(always)]
/// Replaces a locked node slot with a retry marker.
///
/// # Safety
///
/// The XArray lock must be held, the node must be live, and `offset` must
/// identify one of its slots.
unsafe fn set_retry(node: *mut XaNode, offset: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_set_retry(node, offset) };
}

#[inline(always)]
/// Gets the locked parent of a live XArray node.
///
/// # Safety
///
/// The XArray lock must be held and `node` must be live.
unsafe fn parent_locked(xas: *const XaState, node: *mut XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent_locked((*xas).xa, node) }
}

#[inline(always)]
/// Notifies the XArray node update callback.
///
/// # Safety
///
/// The XArray lock must be held and `node` must be live.
unsafe fn notify_node(xas: *mut XaState, node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_notify_node(xas, node) };
}

#[inline(always)]
/// Schedules an initialized node for RCU reclamation.
///
/// # Safety
///
/// `node` must be detached from the tree and ready for reclamation.
unsafe fn retire_node(node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_retire_node(node) };
}

/// Removes a detached subtree and publishes retry markers for RCU readers.
///
/// # Safety
///
/// `top` must be detached from the XArray and the caller must hold the XArray
/// lock. Every node reachable from `top` must remain live until this function
/// schedules it for reclamation.
pub(crate) unsafe fn free_nodes(xas: *mut XaState, top: *mut XaNode) {
    let mut offset = 0;
    let mut node = top;

    loop {
        // SAFETY: The cursor identifies a live node and slot under the lock.
        let entry = unsafe { load_slot(xas, node, offset) };

        // SAFETY: The lock is held and `node` is still live.
        if unsafe { (*node).shift != 0 } && is_node(entry) {
            node = to_node(entry);
            offset = 0;
            continue;
        }
        if !entry.is_null() {
            // SAFETY: A non-NULL slot must provide a retry result to readers.
            unsafe { set_retry(node, offset) };
        }
        offset += 1;
        while offset == CHUNK_MASK + 1 {
            // SAFETY: The node is live and the lock is held.
            let parent = unsafe { parent_locked(xas, node) };
            // SAFETY: The node is live and its offset identifies a parent slot.
            offset = u32::from(unsafe { (*node).offset }) + 1;
            // SAFETY: The node is live and the lock serializes its accounting.
            unsafe {
                (*node).count = 0;
                (*node).nr_values = 0;
            }
            // SAFETY: The node is live and fully initialized.
            unsafe { notify_node(xas, node) };
            // SAFETY: All descendants have been processed.
            unsafe { retire_node(node) };
            if node == top {
                return;
            }
            node = parent;
        }
    }
}

/// Removes a detached subtree through the internal XArray ABI.
///
/// # Safety
///
/// `top` must be detached from the XArray and the caller must hold the XArray
/// lock. Every node reachable from `top` must remain live until this function
/// schedules it for reclamation.
#[export]
unsafe extern "C" fn rust_xarray_free_nodes(xas: *mut XaState, top: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { free_nodes(xas, top) };
}
