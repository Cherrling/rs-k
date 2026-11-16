// SPDX-License-Identifier: GPL-2.0

//! Native XArray multi-index mark squashing.
//!
//! Rust owns mark iteration, union detection, and sibling-range clearing. C
//! retains mark bitmap search, single-bit updates, bulk clearing, and the
//! XArray node layout.

use crate::bindings;
use macros::export;

type Mark = bindings::xa_mark_t;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const MARK_MAX: Mark = bindings::rust_xarray_mark_constant_RUST_XARRAY_MARK_MAX;

#[inline(always)]
/// Returns the next set bit at or after `start` and before `limit`.
///
/// # Safety
///
/// `node` must be live and the XArray lock must be held.
unsafe fn find_next_mark(node: *const XaNode, mark: Mark, limit: u32, start: u32) -> u32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_find_next(node, mark, limit, start) }
}

#[inline(always)]
/// Sets one slot mark in a live XArray node.
///
/// # Safety
///
/// `node` must be live, the XArray lock must be held, and `offset` must
/// identify one of its slots.
unsafe fn set_mark(node: *mut XaNode, mark: Mark, offset: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_set(node, mark, offset) };
}

#[inline(always)]
/// Clears a consecutive range of slot marks in a live XArray node.
///
/// # Safety
///
/// `node` must be live, the XArray lock must be held, and the requested range
/// must fit in the node.
unsafe fn clear_marks(node: *mut XaNode, mark: Mark, start: u32, nbits: u32) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_mark_clear(node, mark, start, nbits) };
}

/// Combines marks from every slot in the current sibling range onto the first
/// slot, then clears the marks on the remaining sibling slots.
///
/// # Safety
///
/// `xas` must identify a valid node with a non-empty sibling range, and the
/// caller must hold the XArray lock.
pub(crate) unsafe fn squash_marks(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &*xas };
    let node = state.xa_node;
    let offset = u32::from(state.xa_offset);
    let sibs = u32::from(state.xa_sibs);
    let start = offset + 1;
    let limit = start + sibs;
    let mut mark = 0;

    loop {
        // SAFETY: The cursor identifies a live node and range under the lock.
        if unsafe { find_next_mark(node, mark, limit, start) } != limit {
            // SAFETY: `offset` is the first slot in the sibling range.
            unsafe { set_mark(node, mark, offset) };
            // SAFETY: `start` and `sibs` identify the remaining sibling slots.
            unsafe { clear_marks(node, mark, start, sibs) };
        }

        if mark == MARK_MAX {
            break;
        }
        mark += 1;
    }
}

/// Combines marks through the internal C ABI used by the Rust store path.
///
/// # Safety
///
/// `xas` must identify a valid node with a non-empty sibling range, and the
/// caller must hold the XArray lock.
#[export]
unsafe extern "C" fn rust_xarray_squash_marks(xas: *const XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { squash_marks(xas) };
}
