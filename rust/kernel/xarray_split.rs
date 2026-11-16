// SPDX-License-Identifier: GPL-2.0

//! Native XArray two-phase multi-index split lifecycle.
//!
//! Rust owns order validation, split-node preparation, pending-chain growth,
//! child and sibling publication, mark propagation, and value accounting. C
//! retains slab allocation, node and entry layout, RCU publication, mark
//! bitmaps, update callbacks, and error encoding.

use crate::{bindings, xarray_nomem};
use core::ffi::c_void;
use core::ptr;
use macros::export;

type Mark = bindings::xa_mark_t;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK;
const CHUNK_SHIFT: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
const CHUNK_SIZE: u32 = CHUNK_MASK + 1;
const MARK_MAX: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_MARK_MAX;
const ENOMEM: i32 = -(bindings::rust_xarray_nomem_constant_RUST_XARRAY_NOMEM_ENOMEM as i32);
#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
const EINVAL: i32 = -(bindings::EINVAL as i32);

#[inline(always)]
/// Reports whether a cursor points at the root pseudo-node or a sentinel.
///
/// # Safety
///
/// Cursor classification does not dereference `node`.
unsafe fn top(node: *mut XaNode) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_top(node) }
}

#[inline(always)]
fn is_value(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_value(entry) }
}

#[inline(always)]
fn sibling(offset: u32) -> *mut c_void {
    // SAFETY: Sibling encoding accepts every in-range slot offset.
    unsafe { bindings::xarray_mk_sibling(offset) }
}

#[inline(always)]
fn node_entry(node: *const XaNode) -> *mut c_void {
    // SAFETY: The encoding accepts every live node pointer.
    unsafe { bindings::xarray_mk_node(node) }
}

#[inline(always)]
/// Reads every mark bit for one live node slot.
///
/// # Safety
///
/// `node` must be live and `offset` must identify one of its slots.
unsafe fn node_marks(node: *const XaNode, offset: u32) -> u32 {
    let mut marks = 0;
    let mut mark = 0;

    loop {
        // SAFETY: Guaranteed by this function's contract.
        if unsafe { bindings::xarray_node_mark_get(node, offset, mark) } {
            marks |= 1 << mark;
        }
        if mark == MARK_MAX {
            break;
        }
        mark += 1;
    }
    marks
}

#[inline(always)]
/// Sets one mark bit on a live node slot.
///
/// # Safety
///
/// The node must be live and the caller must hold the XArray lock.
unsafe fn node_mark_set(node: *mut XaNode, mark: Mark, offset: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_set(node, mark, offset) };
}

/// Propagates one mark mask to a canonical node slot and its child slots.
///
/// # Safety
///
/// The node and optional child must be live and the caller must hold the
/// XArray lock.
unsafe fn node_set_marks(
    node: *mut XaNode,
    offset: u32,
    child: *mut XaNode,
    sibs: u32,
    marks: u32,
) {
    let mut mark = 0;

    loop {
        if marks & (1 << mark) != 0 {
            // SAFETY: The parent node is live and the slot is in range.
            unsafe { node_mark_set(node, mark, offset) };
            if !child.is_null() {
                let stride = sibs + 1;
                let mut slot = 0;
                while slot < CHUNK_SIZE {
                    // SAFETY: The child node is live and each slot is in range.
                    unsafe { node_mark_set(child, mark, slot) };
                    slot += stride;
                }
            }
        }
        if mark == MARK_MAX {
            break;
        }
        mark += 1;
    }
}

#[inline(always)]
/// Initializes every slot in a detached split node.
///
/// # Safety
///
/// `xas` must be valid and `node` must be a live detached allocation.
unsafe fn init_split_node(xas: *const XaState, node: *mut XaNode, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { (*node).array = (*xas).xa };
    // SAFETY: The caller supplies a valid XArray state.
    let mask = u32::from(unsafe { (*xas).xa_sibs });
    let mut sibling_entry = ptr::null_mut();

    for slot in 0..CHUNK_SIZE {
        let current = if slot & mask == 0 {
            sibling_entry = sibling(slot);
            entry
        } else {
            sibling_entry
        };
        // SAFETY: The detached node is live and every slot is in range.
        unsafe { bindings::xarray_set_slot_relaxed(node, slot, current) };
    }
}

#[inline(always)]
/// Appends a detached split node to the pending preallocation chain.
///
/// # Safety
///
/// `node` must be live and `parent` must be NULL or a live pending allocation.
unsafe fn link_prealloc(node: *mut XaNode, parent: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_parent_relaxed(node, parent) };
}

#[inline(always)]
/// Pops the next node from the pending preallocation chain.
///
/// # Safety
///
/// `node` must be a live pending allocation.
unsafe fn prealloc_parent(node: *const XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_parent_prealloc(node) }
}

#[inline(always)]
/// Publishes a child node slot with the required RCU assignment operation.
///
/// # Safety
///
/// The caller must hold the XArray lock and supply live initialized objects.
unsafe fn publish_slot(xas: *mut XaState, node: *mut XaNode, offset: u32, entry: *mut c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_assign_slot(xas, node, offset, entry) };
}

#[inline(always)]
/// Notifies the XArray node update callback.
///
/// # Safety
///
/// The caller must hold the XArray lock and `node` must be live.
unsafe fn notify_node(xas: *mut XaState, node: *mut XaNode) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::rust_xarray_notify_node(xas, node) };
}

#[inline(always)]
/// Stores an error encoding in an XArray cursor.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn set_error(xas: *mut XaState, error: i32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_err(xas, error as isize) };
}

/// Releases every prepared split node and records ENOMEM.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn allocation_failed(xas: *mut XaState) {
    // SAFETY: The split preparation chain is fully detached.
    unsafe { xarray_nomem::destroy(xas) };
    // SAFETY: The caller provides a valid writable cursor.
    unsafe { set_error(xas, ENOMEM) };
}

/// Allocates and initializes the pending nodes needed by a multi-index split.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
pub(crate) unsafe fn split_alloc(
    xas: *mut XaState,
    entry: *mut c_void,
    order: u32,
    gfp: bindings::gfp_t,
) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let mut sibs = (1u32 << (order % CHUNK_SHIFT)) - 1;
    let unsupported = u32::from(state.xa_shift) + 2 * CHUNK_SHIFT <= order;

    if crate::warn_on!(unsupported) {
        // SAFETY: The unsupported request releases stale prepared nodes.
        unsafe { allocation_failed(xas) };
        return;
    }
    if u32::from(state.xa_shift) + CHUNK_SHIFT > order {
        return;
    }

    loop {
        // SAFETY: The allocation boundary accepts this cursor and flag set.
        let node = unsafe { bindings::rust_xarray_alloc_prealloc(xas, gfp) };
        if node.is_null() {
            // SAFETY: Allocation failure releases every node prepared so far.
            unsafe { allocation_failed(xas) };
            return;
        }

        // SAFETY: The allocator returned a live detached node.
        unsafe { init_split_node(xas, node, entry) };
        // SAFETY: The pending chain contains either NULL or live allocations.
        unsafe { link_prealloc(node, state.xa_alloc) };
        state.xa_alloc = node;

        if sibs == 0 {
            break;
        }
        sibs -= 1;
    }
}

/// Commits a prepared multi-index split while the caller holds the lock.
///
/// # Safety
///
/// `xas` must point to a valid XArray state, the caller must hold the XArray
/// lock, and prepared nodes must cover the requested split.
pub(crate) unsafe fn split(xas: *mut XaState, entry: *mut c_void, order: u32) {
    // SAFETY: Guaranteed by this function's contract.
    let curr = unsafe { bindings::xas_load(xas) };
    // SAFETY: Guaranteed by this function's contract.
    let node = unsafe { (*xas).xa_node };
    // SAFETY: Cursor classification accepts sentinels and live node pointers.
    if unsafe { top(node) } {
        return;
    }

    // SAFETY: The loaded cursor identifies a live node slot.
    let marks = unsafe { node_marks(node, u32::from((*xas).xa_offset)) };
    // SAFETY: The caller supplies a valid XArray state.
    let start = u32::from(unsafe { (*xas).xa_offset });
    // SAFETY: The caller supplies a valid XArray state.
    let sibs = u32::from(unsafe { (*xas).xa_sibs });
    let mut offset = start + ((1u32 << (order % CHUNK_SHIFT)) - 1);
    let mut values = 0i32;

    loop {
        // SAFETY: The caller holds the lock on a live node.
        if u32::from(unsafe { (*xas).xa_shift }) < u32::from(unsafe { (*node).shift }) {
            // SAFETY: A prepared vertical split has one live detached child.
            let child = unsafe { (*xas).xa_alloc };
            // SAFETY: The prepared chain starts with this child.
            unsafe { (*xas).xa_alloc = prealloc_parent(child) };
            // SAFETY: The child and parent are live and the lock is held.
            unsafe {
                (*child).shift = (*node).shift - CHUNK_SHIFT as u8;
                (*child).offset = offset as u8;
                (*child).count = CHUNK_SIZE as u8;
                (*child).nr_values = if is_value(entry) { CHUNK_SIZE as u8 } else { 0 };
            }
            // SAFETY: The child is detached and the parent is live.
            unsafe { bindings::xarray_set_parent_relaxed(child, node) };
            // SAFETY: The parent and child are live under the lock.
            unsafe { node_set_marks(node, offset, child, sibs, marks) };
            // SAFETY: The initialized child can be published at this slot.
            unsafe { publish_slot(xas, node, offset, node_entry(child)) };
            if is_value(curr) {
                values -= 1;
            }
            // SAFETY: The child is initialized and the lock is held.
            unsafe { notify_node(xas, child) };
        } else {
            let canon = offset - sibs;
            // SAFETY: The canonical slot is live under the lock.
            unsafe { node_set_marks(node, canon, ptr::null_mut(), 0, marks) };
            // SAFETY: The caller owns the locked slot publication.
            unsafe { publish_slot(xas, node, canon, entry) };
            while offset > canon {
                // SAFETY: Sibling slots are in range and covered by the split.
                unsafe { publish_slot(xas, node, offset, sibling(canon)) };
                offset -= 1;
            }
            values += (i32::from(is_value(entry)) - i32::from(is_value(curr))) * (sibs as i32 + 1);
        }

        if offset == start {
            break;
        }
        offset -= 1;
    }

    // SAFETY: The caller holds the lock and the node accounting is in range.
    unsafe {
        (*node).nr_values = (i32::from((*node).nr_values) + values) as u8;
        notify_node(xas, node);
    }
}

#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
pub(crate) fn try_split_min_order(order: u32) -> u32 {
    let remainder = order % CHUNK_SHIFT;

    if remainder == 0 {
        if order == 0 {
            0
        } else {
            order - 1
        }
    } else {
        order - remainder
    }
}

#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
/// Rejects a one-shot split and releases every prepared node.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn reject_try_split(xas: *mut XaState, error: i32) {
    // SAFETY: The caller provides a valid writable state.
    unsafe { xarray_nomem::destroy(xas) };
    // SAFETY: The caller provides a valid writable state.
    unsafe { set_error(xas, error) };
}

#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
/// Attempts one level of multi-index splitting under the XArray lock.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
pub(crate) unsafe fn try_split(xas: *mut XaState, entry: *mut c_void, order: u32) {
    // SAFETY: Guaranteed by this function's contract.
    let curr = unsafe { bindings::xas_load(xas) };
    // SAFETY: Guaranteed by this function's contract.
    let node = unsafe { (*xas).xa_node };
    // SAFETY: Cursor classification accepts sentinels and live node pointers.
    if unsafe { top(node) } {
        return;
    }

    // SAFETY: The loaded cursor identifies a live node slot.
    let marks = unsafe { node_marks(node, u32::from((*xas).xa_offset)) };
    // SAFETY: The caller supplies a valid XArray state.
    let start = u32::from(unsafe { (*xas).xa_offset });
    // SAFETY: The caller supplies a valid XArray state.
    let cursor_sibs = u32::from(unsafe { (*xas).xa_sibs });
    let sibs = (1u32 << (order % CHUNK_SHIFT)) - 1;
    let mut offset = start + sibs;
    let mut values = 0i32;

    // SAFETY: The caller holds the lock on a live node.
    if u32::from(unsafe { (*xas).xa_shift }) < u32::from(unsafe { (*node).shift }) {
        // SAFETY: The caller supplies a valid writable XArray state.
        let mut child = unsafe { (*xas).xa_alloc };
        let expected_sibs = (1u32 << (order.wrapping_sub(1) % CHUNK_SHIFT)) - 1;

        if sibs != 0 || cursor_sibs != expected_sibs {
            // SAFETY: The caller supplies a valid writable state.
            unsafe { reject_try_split(xas, EINVAL) };
            return;
        }

        if child.is_null() {
            // SAFETY: The allocation boundary accepts this cursor and flag set.
            child = unsafe { bindings::rust_xarray_alloc_prealloc(xas, bindings::GFP_NOWAIT) };
            if child.is_null() {
                // SAFETY: The caller supplies a valid writable state.
                unsafe { reject_try_split(xas, ENOMEM) };
                return;
            }

            // SAFETY: The allocator returned a live detached node.
            unsafe { link_prealloc(child, (*xas).xa_alloc) };
        }

        // SAFETY: The selected child is a live detached allocation.
        unsafe { init_split_node(xas, child, entry) };
        // SAFETY: A selected child retains the rest of the pending chain.
        unsafe { (*xas).xa_alloc = prealloc_parent(child) };
        // SAFETY: The child and parent are live under the lock.
        unsafe {
            (*child).shift = (*node).shift - CHUNK_SHIFT as u8;
            (*child).offset = offset as u8;
            (*child).count = CHUNK_SIZE as u8;
            (*child).nr_values = if is_value(entry) { CHUNK_SIZE as u8 } else { 0 };
        }
        // SAFETY: The child is detached and the parent is live.
        unsafe { bindings::xarray_set_parent_relaxed(child, node) };
        // SAFETY: The parent and child are live under the lock.
        unsafe { node_set_marks(node, offset, child, cursor_sibs, marks) };
        // SAFETY: The initialized child can be published at this slot.
        unsafe { publish_slot(xas, node, offset, node_entry(child)) };
        if is_value(curr) {
            values -= 1;
        }
        // SAFETY: The child is initialized and the lock is held.
        unsafe { notify_node(xas, child) };
    } else {
        loop {
            let canon = offset - cursor_sibs;
            // SAFETY: The canonical slot is live under the lock.
            unsafe { node_set_marks(node, canon, ptr::null_mut(), 0, marks) };
            // SAFETY: The caller owns the locked slot publication.
            unsafe { publish_slot(xas, node, canon, entry) };
            while offset > canon {
                // SAFETY: Sibling slots are in range and covered by the split.
                unsafe { publish_slot(xas, node, offset, sibling(canon)) };
                offset -= 1;
            }
            values +=
                (i32::from(is_value(entry)) - i32::from(is_value(curr))) * (cursor_sibs as i32 + 1);

            if offset == start {
                break;
            }
            offset -= 1;
        }
    }

    // SAFETY: The caller holds the lock and the node accounting is in range.
    unsafe {
        (*node).nr_values = (i32::from((*node).nr_values) + values) as u8;
        notify_node(xas, node);
    }
}

/// Allocates split preparation state through the public ABI.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
#[export]
unsafe extern "C" fn rust_xarray_split_alloc(
    xas: *mut XaState,
    entry: *mut c_void,
    order: u32,
    gfp: bindings::gfp_t,
) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { split_alloc(xas, entry, order, gfp) };
}

/// Commits a split through the public ABI.
///
/// # Safety
///
/// `xas` must point to a valid locked XArray state.
#[export]
unsafe extern "C" fn rust_xarray_split(xas: *mut XaState, entry: *mut c_void, order: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { split(xas, entry, order) };
}

#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
#[export]
extern "C" fn rust_xarray_try_split_min_order(order: u32) -> u32 {
    try_split_min_order(order)
}

#[cfg(CONFIG_RUST_XARRAY_TRY_SPLIT)]
#[export]
/// Performs one level of a multi-index split through the C ABI.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
unsafe extern "C" fn rust_xarray_try_split(xas: *mut XaState, entry: *mut c_void, order: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { try_split(xas, entry, order) };
}
