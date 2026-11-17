// SPDX-License-Identifier: GPL-2.0

//! Native XArray marked forward search.
//!
//! Rust owns cursor traversal, mark-driven chunk jumps, sibling normalization,
//! empty-subtree retreat, index updates, and terminal cursor states. C retains
//! mark bitmaps, node and cursor layouts, slot access, and tagged encodings.

use crate::bindings;
use core::ffi::c_void;
use core::ptr;
use macros::export;

type Mark = bindings::xa_mark_t;
type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SIZE: usize = 1usize << bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
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
fn is_sibling(entry: *mut c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_sibling(entry) }
}

#[inline(always)]
fn to_sibling(entry: *mut c_void) -> u32 {
    // SAFETY: The caller checks that `entry` is a sibling entry first.
    unsafe { bindings::xarray_to_sibling(entry) as u32 }
}

#[inline(always)]
fn is_top(node: *mut XaNode) -> bool {
    // SAFETY: Top-state classification does not dereference the pointer.
    unsafe { bindings::xarray_top(node) }
}

#[inline(always)]
/// Reports whether the cursor contains an error.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
}

#[inline(always)]
/// Moves the cursor to the bounds sentinel.
///
/// # Safety
///
/// `xas` must point to a writable XArray state.
unsafe fn set_bounds(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_bounds(xas) }
}

#[inline(always)]
/// Moves the cursor to the restart sentinel.
///
/// # Safety
///
/// `xas` must point to a writable XArray state.
unsafe fn set_restart(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_set_restart(xas) }
}

/// Loads the root entry under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` must point to a valid XArray. If `externally_locked` is true, the
/// caller must hold a lock that excludes writers. Otherwise, the caller must
/// satisfy the RCU locking contract of `xa_head()`.
unsafe fn head(xa: *mut XArray, externally_locked: bool) -> *mut c_void {
    if externally_locked {
        // SAFETY: The caller provides external writer exclusion.
        unsafe { bindings::xarray_head_external(xa) }
    } else {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { bindings::xarray_head(xa) }
    }
}

/// Loads one slot under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` and `node` must be valid and `offset` must be in range. If
/// `externally_locked` is true, the caller must hold a lock that excludes
/// writers. Otherwise, the caller must satisfy the RCU locking contract of
/// `xa_entry()`.
unsafe fn entry(
    xa: *const XArray,
    node: *mut XaNode,
    offset: u32,
    externally_locked: bool,
) -> *mut c_void {
    if externally_locked {
        // SAFETY: The caller provides external writer exclusion.
        unsafe { bindings::xarray_entry_external(xa, node, offset) }
    } else {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { bindings::xarray_entry(xa, node, offset) }
    }
}

/// Loads the parent under the caller's RCU or lock protection.
///
/// # Safety
///
/// `xa` and `node` must be valid. If `externally_locked` is true, the caller
/// must hold a lock that excludes writers. Otherwise, the caller must satisfy
/// the RCU locking contract of `xa_parent()`.
unsafe fn parent(xa: *const XArray, node: *mut XaNode, externally_locked: bool) -> *mut XaNode {
    if externally_locked {
        // SAFETY: The caller provides external writer exclusion.
        unsafe { bindings::xarray_parent_external(xa, node) }
    } else {
        // SAFETY: Guaranteed by this function's contract.
        unsafe { bindings::xarray_parent(xa, node) }
    }
}

/// Reports whether the root has `mark` set.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn root_marked(xa: *const XArray, mark: Mark) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_root_marked(xa, mark) }
}

/// Reports whether the root requests free-entry tracking.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn track_free(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_track_free(xa) }
}

/// Finds the next set mark bit at or after `offset`.
///
/// # Safety
///
/// `node` must be live and `offset` must not exceed the chunk size.
unsafe fn find_mark(node: *const XaNode, offset: u32, mark: Mark) -> u32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_node_find_mark(node, offset, mark) }
}

/// Returns the maximum index that a root entry can cover.
fn max_index(loaded: *mut c_void) -> usize {
    if !is_node(loaded) {
        return 0;
    }

    let node = to_node(loaded);
    // SAFETY: `is_node` verified that `loaded` decodes to a live node.
    let shift = u32::from(unsafe { (*node).shift });
    CHUNK_SIZE.wrapping_shl(shift).wrapping_sub(1)
}

/// Updates the index from a node offset.
///
/// # Safety
///
/// `state` must identify a live node.
unsafe fn move_index(state: &mut XaState, offset: u32) {
    let node = state.xa_node;
    // SAFETY: Guaranteed by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });

    state.xa_index &= (!CHUNK_MASK).wrapping_shl(shift);
    state.xa_index = state
        .xa_index
        .wrapping_add((offset as usize).wrapping_shl(shift));
}

/// Sets the offset from the current index.
///
/// # Safety
///
/// `state` must identify a live node.
unsafe fn set_offset(state: &mut XaState) {
    let node = state.xa_node;
    // SAFETY: Guaranteed by this function's contract.
    let shift = u32::from(unsafe { (*node).shift });
    state.xa_offset = ((state.xa_index >> shift) & CHUNK_MASK) as u8;
}

/// Finishes an exhausted walk from the `out` label.
///
/// # Safety
///
/// `xas` must point to a writable XArray state under RCU or lock protection.
unsafe fn finish_out(xas: *mut XaState, max: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { (*xas).xa_index } > max {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_restart(xas) };
    } else {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_bounds(xas) };
    }
    ptr::null_mut()
}

/// Finds the next marked entry at or after the cursor index.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU, XArray lock, or external
/// writer-lock protection.
unsafe fn find_inner(
    xas: *mut XaState,
    max: usize,
    mark: Mark,
    externally_locked: bool,
) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let state = unsafe { &mut *xas };
    let mut advance = true;

    // SAFETY: The state remains addressable for the complete operation.
    if unsafe { error(xas) } != 0 {
        return ptr::null_mut();
    }
    if state.xa_index > max {
        // SAFETY: The state is writable under RCU or lock protection.
        unsafe { set_restart(xas) };
        return ptr::null_mut();
    }

    if state.xa_node.is_null() {
        state.xa_index = 1;
        // SAFETY: The state is writable under RCU or lock protection.
        return unsafe { finish_out(xas, max) };
    }

    if is_top(state.xa_node) {
        advance = false;
        // SAFETY: A valid state points to a valid XArray.
        let loaded = unsafe { head(state.xa, externally_locked) };
        state.xa_node = ptr::null_mut();
        if state.xa_index > max_index(loaded) {
            // SAFETY: The state is writable under RCU or lock protection.
            return unsafe { finish_out(xas, max) };
        }
        if !is_node(loaded) {
            // SAFETY: A valid state points to a valid XArray.
            if unsafe { root_marked(state.xa, mark) } {
                return loaded;
            }
            state.xa_index = 1;
            // SAFETY: The state is writable under RCU or lock protection.
            return unsafe { finish_out(xas, max) };
        }

        state.xa_node = to_node(loaded);
        // SAFETY: The node was selected from the root.
        let shift = u32::from(unsafe { (*state.xa_node).shift });
        state.xa_offset = (state.xa_index >> shift) as u8;
    }

    while state.xa_index <= max {
        if usize::from(state.xa_offset) == CHUNK_SIZE {
            // SAFETY: The cursor identifies a live node.
            state.xa_offset = unsafe { (*state.xa_node).offset }.wrapping_add(1);
            // SAFETY: The node is live under RCU or lock protection.
            state.xa_node = unsafe { parent(state.xa, state.xa_node, externally_locked) };
            if state.xa_node.is_null() {
                break;
            }
            advance = false;
            continue;
        }

        if !advance {
            // SAFETY: The cursor offset identifies a slot in the live node.
            let loaded = unsafe {
                entry(
                    state.xa,
                    state.xa_node,
                    u32::from(state.xa_offset),
                    externally_locked,
                )
            };
            if is_sibling(loaded) {
                state.xa_offset = to_sibling(loaded) as u8;
                // SAFETY: The cursor identifies a live node.
                unsafe { move_index(state, u32::from(state.xa_offset)) };
            }
        }

        let offset = if advance {
            state.xa_offset.wrapping_add(1)
        } else {
            state.xa_offset
        };
        // SAFETY: The cursor identifies a live node and offset is in range.
        let offset = unsafe { find_mark(state.xa_node, u32::from(offset), mark) };
        if offset > u32::from(state.xa_offset) {
            advance = false;
            // SAFETY: The cursor identifies a live node.
            unsafe { move_index(state, offset) };
            if state.xa_index.wrapping_sub(1) >= max {
                // SAFETY: The state is writable under RCU or lock protection.
                unsafe { set_restart(xas) };
                return ptr::null_mut();
            }
            state.xa_offset = offset as u8;
            if usize::from(state.xa_offset) == CHUNK_SIZE {
                continue;
            }
        }

        // SAFETY: The cursor offset identifies a slot in the live node.
        let loaded = unsafe {
            entry(
                state.xa,
                state.xa_node,
                u32::from(state.xa_offset),
                externally_locked,
            )
        };
        if loaded.is_null()
            // SAFETY: A valid state points to a valid XArray.
            && !(unsafe { track_free(state.xa) } && mark == FREE_MARK)
        {
            continue;
        }
        if is_sibling(loaded) {
            continue;
        }
        if !is_node(loaded) {
            return loaded;
        }

        state.xa_node = to_node(loaded);
        // SAFETY: The cursor identifies the selected live node.
        unsafe { set_offset(state) };
    }

    // SAFETY: The state is writable under RCU or lock protection.
    unsafe { finish_out(xas, max) }
}

/// Finds the next marked entry under the caller's XArray lock or RCU lock.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or XArray lock
/// protection.
pub(crate) unsafe fn find(xas: *mut XaState, max: usize, mark: Mark) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_inner(xas, max, mark, false) }
}

/// Finds the next marked entry under caller-provided external synchronization.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold a lock that
/// excludes every concurrent writer for the complete operation.
#[cfg(CONFIG_RUST_IDR_ALLOC)]
pub(crate) unsafe fn find_external(xas: *mut XaState, max: usize, mark: Mark) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find_inner(xas, max, mark, true) }
}

/// Finds the next marked entry through the advanced XArray API.
///
/// # Safety
///
/// `xas` must point to a valid XArray state under RCU or lock protection.
#[export]
unsafe extern "C" fn rust_xarray_find_marked(
    xas: *mut XaState,
    max: usize,
    mark: Mark,
) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { find(xas, max, mark) }
}
