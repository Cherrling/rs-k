// SPDX-License-Identifier: GPL-2.0

//! Native XArray empty-node deletion and root shrinking.
//!
//! Rust owns the deletion and shrinking control flow, parent accounting
//! propagation, root clearing decision, and repeated root promotion policy. C
//! retains the XArray layout, locked slot access, RCU publication, mark bits,
//! update callbacks, and node reclamation primitive.

use crate::bindings;
use core::ffi::c_void;

type Mark = bindings::xa_mark_t;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const FREE_MARK: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;

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
fn is_zero(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_zero(entry) }
}

#[inline(always)]
/// Loads a locked slot from a live node.
///
/// # Safety
///
/// The XArray lock must be held and `offset` must identify a slot in `node`.
unsafe fn load_slot(xas: *const XaState, node: *mut XaNode, offset: u32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_locked(xas, node, offset) }
}

#[inline(always)]
/// Publishes a locked node slot or the XArray root.
///
/// # Safety
///
/// The XArray lock must be held. A null `node` selects the root slot.
unsafe fn publish_slot(xas: *mut XaState, node: *mut XaNode, offset: u32, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_assign_slot(xas, node, offset, entry) };
}

#[inline(always)]
/// Gets the parent of a live node.
///
/// # Safety
///
/// The XArray lock must be held and `node` must be live.
unsafe fn parent_locked(xas: *const XaState, node: *mut XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent_locked((*xas).xa, node) }
}

#[inline(always)]
/// Validates node accounting before policy decisions.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn check_node(node: *const XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_check(node) };
}

#[inline(always)]
/// Reports whether the XArray tracks free entries.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn track_free(xa: *const bindings::xarray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

#[inline(always)]
/// Reports whether reserved zero entries are marked busy.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn zero_busy(xa: *const bindings::xarray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_zero_busy(xa) }
}

#[inline(always)]
/// Reads a mark from a slot in a live node.
///
/// # Safety
///
/// `node` must be live and `offset` must identify one of its slots.
unsafe fn node_get_mark(node: *const XaNode, offset: u32, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_get(node, offset, mark) }
}

#[inline(always)]
/// Clears a mark from the XArray root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray whose lock is held.
unsafe fn root_mark_clear(xa: *mut bindings::xarray, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_mark_clear(xa, mark) };
}

#[inline(always)]
/// Clears the parent pointer of a promoted root node.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn clear_parent(node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_clear_parent(node) };
}

#[inline(always)]
/// Replaces a visible slot in a retired node with a retry marker.
///
/// # Safety
///
/// `node` must be live, the XArray lock must be held, and offset zero must
/// identify a slot.
unsafe fn set_retry(node: *mut XaNode, offset: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_slot_set_retry(node, offset) };
}

#[inline(always)]
/// Notifies the XArray update callback.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn notify_node(xas: *mut XaState, node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_notify_node(xas, node) };
}

#[inline(always)]
/// Schedules a detached node for RCU reclamation.
///
/// # Safety
///
/// `node` must be detached from the tree and ready for reclamation.
unsafe fn retire_node(node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_retire_node(node) };
}

/// Promotes a single surviving child until the root no longer needs shrinking.
///
/// # Safety
///
/// `xas` must point to a valid locked XArray state. `xas.xa_node` must be the
/// current root node.
unsafe fn shrink(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let mut node = unsafe { (*xas).xa_node };

    loop {
        // SAFETY: The root node is live and the XArray lock is held.
        unsafe { check_node(node) };
        // SAFETY: The root node is live and the lock serializes its accounting.
        if unsafe { (*node).count } != 1 {
            break;
        }
        // SAFETY: The root node is live and slot zero is the promotion source.
        let mut entry = unsafe { load_slot(xas, node, 0) };
        if entry.is_null() {
            break;
        }
        // SAFETY: The root node is live and the lock serializes its shift.
        if !is_node(entry) && unsafe { (*node).shift } != 0 {
            break;
        }
        // SAFETY: The XArray pointer is valid for the locked state.
        let xa = unsafe { (*xas).xa };
        // SAFETY: The XArray is valid for the locked state.
        if unsafe { zero_busy(xa) } && is_zero(entry) {
            entry = core::ptr::null_mut();
        }

        // SAFETY: The promoted entry replaces the root before node retirement.
        unsafe { bindings::xarray_set_bounds(xas) };
        // SAFETY: The XArray lock is held and a null node selects the root.
        unsafe { publish_slot(xas, core::ptr::null_mut(), 0, entry) };
        // SAFETY: The XArray and node are live and locked.
        if unsafe { track_free(xa) }
            // SAFETY: The root node remains live and slot zero identifies its mark.
            && !unsafe { node_get_mark(node, 0, FREE_MARK) }
        {
            // SAFETY: The XArray is live and locked.
            unsafe { root_mark_clear(xa, FREE_MARK) };
        }

        // SAFETY: The root node is detached and the lock serializes its state.
        unsafe {
            (*node).count = 0;
            (*node).nr_values = 0;
        }
        if !is_node(entry) {
            // SAFETY: Readers that retained the detached root must restart.
            unsafe { set_retry(node, 0) };
        }
        // SAFETY: The detached root accounting is final.
        unsafe { notify_node(xas, node) };
        // SAFETY: The root has been replaced and its descendant promoted.
        unsafe { retire_node(node) };

        if !is_node(entry) {
            break;
        }
        node = to_node(entry);
        // SAFETY: The promoted child is the new root and remains live.
        unsafe { clear_parent(node) };
    }
}

/// Deletes empty nodes from the current cursor and shrinks the resulting root.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock. The caller must invoke this function after a negative node
/// accounting delta.
pub(crate) unsafe fn delete_node(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let mut node = unsafe { (*xas).xa_node };

    loop {
        // SAFETY: The cursor node is live and the XArray lock is held.
        unsafe { check_node(node) };
        // SAFETY: The lock serializes node accounting.
        if unsafe { (*node).count } != 0 {
            break;
        }

        // SAFETY: The empty node is live and its parent pointer is protected.
        let parent = unsafe { parent_locked(xas, node) };
        // SAFETY: The caller holds the lock and both pointers are live.
        let offset = u32::from(unsafe { (*node).offset });
        // SAFETY: The parent pointer is valid, including the root null value.
        unsafe {
            (*xas).xa_node = parent;
            (*xas).xa_offset = offset as u8;
        }
        // SAFETY: The empty node is detached and must be reclaimed.
        unsafe { retire_node(node) };

        if parent.is_null() {
            // SAFETY: The empty root is published as null under the lock.
            unsafe { publish_slot(xas, parent, 0, core::ptr::null_mut()) };
            // SAFETY: End the cursor at the newly empty root.
            unsafe { bindings::xarray_set_bounds(xas) };
            return;
        }

        // SAFETY: The parent is live and offset identifies the removed child.
        unsafe { publish_slot(xas, parent, offset, core::ptr::null_mut()) };
        // SAFETY: The parent is live and the lock serializes its accounting.
        unsafe { (*parent).count -= 1 };
        // SAFETY: The updated parent is live and the lock is held.
        unsafe { check_node(parent) };
        node = parent;
        // SAFETY: The parent accounting now reflects the removed child.
        unsafe { notify_node(xas, node) };
    }

    // SAFETY: The surviving node is live and the XArray lock is held.
    if unsafe { parent_locked(xas, node) }.is_null() {
        // SAFETY: The surviving node is the root of the locked XArray.
        unsafe { shrink(xas) };
    }
}
