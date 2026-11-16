// SPDX-License-Identifier: GPL-2.0

//! Native XArray mark lifecycle.
//!
//! Rust owns cursor validation, mark lookup, ancestor propagation, propagation
//! stop conditions, and mark initialization. C retains mark bitmaps, root flag
//! storage, parent traversal, locked-root rules, and the XArray data layout.

use crate::bindings;
use macros::export;

type Mark = bindings::xa_mark_t;
type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const MARK_MAX: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_MARK_MAX;
const FREE_MARK: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;

#[inline(always)]
fn is_invalid(node: *mut XaNode) -> bool {
    node.addr() & 3 != 0
}

#[inline(always)]
/// Reads a mark bit from a live XArray node.
///
/// # Safety
///
/// `node` must be live for the duration of the call and `offset` must identify
/// one of its slots.
unsafe fn node_mark_get(node: *const XaNode, offset: u32, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_get(node, offset, mark) }
}

#[inline(always)]
/// Sets a mark bit in a live XArray node.
///
/// # Safety
///
/// `node` must be live, the XArray lock must be held, and `offset` must
/// identify one of its slots.
unsafe fn node_mark_test_and_set(node: *mut XaNode, offset: u32, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_test_and_set(node, offset, mark) }
}

#[inline(always)]
/// Clears a mark bit in a live XArray node.
///
/// # Safety
///
/// `node` must be live, the XArray lock must be held, and `offset` must
/// identify one of its slots.
unsafe fn node_mark_test_and_clear(node: *mut XaNode, offset: u32, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_test_and_clear(node, offset, mark) }
}

#[inline(always)]
/// Checks whether any slot in a live XArray node has a mark.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn node_any_mark(node: *const XaNode, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_any_mark(node, mark) }
}

#[inline(always)]
/// Reads a mark from the root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn root_marked(xa: *const XArray, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_marked(xa, mark) }
}

#[inline(always)]
/// Sets a mark in the root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray and the XArray lock must be held.
unsafe fn root_mark_set(xa: *mut XArray, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_mark_set(xa, mark) }
}

#[inline(always)]
/// Clears a mark from the root flags.
///
/// # Safety
///
/// `xa` must point to a valid XArray and the XArray lock must be held.
pub(crate) unsafe fn root_mark_clear(xa: *mut XArray, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_mark_clear(xa, mark) }
}

#[inline(always)]
/// Returns the locked parent of an XArray node.
///
/// # Safety
///
/// `xa` and `node` must be valid and the XArray lock must be held.
unsafe fn parent_locked(xa: *const XArray, node: *const XaNode) -> *mut XaNode {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_parent_locked(xa, node) }
}

#[inline(always)]
/// Returns whether root flags request free-entry tracking.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn track_free(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

/// Returns whether the current cursor entry has `mark` set.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under the caller's RCU or lock
/// protection.
unsafe fn mark_get(xas: *const XaState, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let node = state.xa_node;

    if is_invalid(node) {
        return false;
    }
    if node.is_null() {
        // SAFETY: A valid state points to a valid XArray.
        return unsafe { root_marked(state.xa, mark) };
    }

    // SAFETY: A valid state with a non-NULL node points to a real node.
    unsafe { node_mark_get(node, u32::from(state.xa_offset), mark) }
}

/// Sets `mark` on the current cursor entry and propagates it to ancestors.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
unsafe fn mark_set(xas: *const XaState, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let mut node = state.xa_node;
    let mut offset = u32::from(state.xa_offset);

    if is_invalid(node) {
        return;
    }

    while !node.is_null() {
        // SAFETY: `node` is a real node and `offset` identifies one of its slots.
        if unsafe { node_mark_test_and_set(node, offset, mark) } {
            return;
        }
        // SAFETY: A real node stores its slot offset in the parent.
        offset = u32::from(unsafe { (*node).offset });
        // SAFETY: The caller holds the XArray lock.
        node = unsafe { parent_locked(state.xa, node) };
    }

    // SAFETY: The caller holds the lock that protects the root flags.
    if !unsafe { root_marked(state.xa, mark) } {
        // SAFETY: The caller holds the lock that protects the root flags.
        unsafe { root_mark_set(state.xa, mark) };
    }
}

/// Clears `mark` on the current cursor entry and ancestors that no longer have
/// another marked descendant.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
unsafe fn mark_clear(xas: *const XaState, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let mut node = state.xa_node;
    let mut offset = u32::from(state.xa_offset);

    if is_invalid(node) {
        return;
    }

    while !node.is_null() {
        // SAFETY: `node` is a real node and `offset` identifies one of its slots.
        if !unsafe { node_mark_test_and_clear(node, offset, mark) } {
            return;
        }
        // SAFETY: The node is still live under the XArray lock.
        if unsafe { node_any_mark(node, mark) } {
            return;
        }
        // SAFETY: A real node stores its slot offset in the parent.
        offset = u32::from(unsafe { (*node).offset });
        // SAFETY: The caller holds the XArray lock.
        node = unsafe { parent_locked(state.xa, node) };
    }

    // SAFETY: The caller holds the lock that protects the root flags.
    if unsafe { root_marked(state.xa, mark) } {
        // SAFETY: The caller holds the lock that protects the root flags.
        unsafe { root_mark_clear(state.xa, mark) };
    }
}

/// Initializes every mark for the current cursor entry.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
pub(crate) unsafe fn marks_init(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let mut mark = 0;

    loop {
        // SAFETY: The caller holds the XArray lock for every mark update.
        if unsafe { track_free(state.xa) } && mark == FREE_MARK {
            // SAFETY: The caller holds the XArray lock for every mark update.
            unsafe { mark_set(xas, mark) };
        } else {
            // SAFETY: The caller holds the XArray lock for every mark update.
            unsafe { mark_clear(xas, mark) };
        }

        if mark == MARK_MAX {
            break;
        }
        mark += 1;
    }
}

/// Returns whether the current advanced XArray cursor has a mark set.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_mark_get(xas: *const XaState, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { mark_get(xas, mark) }
}

/// Sets a mark through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_mark_set(xas: *const XaState, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { mark_set(xas, mark) };
}

/// Clears a mark through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_mark_clear(xas: *const XaState, mark: Mark) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { mark_clear(xas, mark) };
}

/// Initializes all marks through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state and the caller must hold the
/// XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_marks_init(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { marks_init(xas) };
}
