// SPDX-License-Identifier: GPL-2.0

//! Native largest-gap calculation for Maple Tree leaves.
//!
//! C retains concrete node layout, slot and pivot addressing, and metadata
//! access. Rust owns the dense slot-run and range-hole search policy.

use super::maple_tree_core::{data_end, MapleNode};
use crate::bindings;
use core::ffi::c_void;
use macros::export;

/// Returns whether a Maple node type omits explicit pivots.
fn is_dense(node_type: bindings::maple_type) -> bool {
    node_type == bindings::maple_type_maple_dense
}

/// Finds the longest NULL run in a dense leaf.
///
/// # Safety
///
/// `slots` must contain `slot_count` valid entries and remain immovable during
/// the call.
unsafe fn dense_max_gap(slots: *mut *mut c_void, slot_count: u8) -> usize {
    let mut max_gap = 0;
    let mut gap = 0;

    for offset in 0..usize::from(slot_count) {
        // SAFETY: The caller guarantees the complete dense slot range.
        if unsafe { *slots.add(offset) }.is_null() {
            gap += 1;
        } else {
            if gap > max_gap {
                max_gap = gap;
            }
            gap = 0;
        }
    }

    if gap > max_gap {
        max_gap = gap;
    }

    max_gap
}

/// Finds the largest allocation hole in a pivot-bearing leaf.
///
/// # Safety
///
/// `node`, `pivots`, and `slots` must describe the same protected leaf.
/// `node_type` must be a pivot-bearing leaf layout.
unsafe fn range_max_gap(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    min: usize,
    max: usize,
    pivots: *mut usize,
    slots: *mut *mut c_void,
) -> usize {
    let mut max_gap = 0;

    // A NULL first slot creates a gap from the lower node boundary. Its
    // neighbors cannot both be gaps, so slot one is also skipped.
    // SAFETY: The first slot belongs to the protected leaf.
    let mut offset = if unsafe { *slots }.is_null() {
        // SAFETY: The first pivot covers the protected lower boundary.
        max_gap = unsafe { *pivots }.wrapping_sub(min).wrapping_add(1);
        2
    } else {
        1
    };

    // SAFETY: The caller supplies the complete protected leaf layout.
    let max_pivot = unsafe { data_end(node, node_type, pivots, max) }.wrapping_sub(1);

    // A NULL trailing slot on the rightmost node extends to ULONG_MAX.
    if max == usize::MAX {
        let right_slot = usize::from(max_pivot).wrapping_add(1);
        // SAFETY: A valid range leaf has an implied pivot after the last
        // internal pivot.
        if unsafe { *slots.add(right_slot) }.is_null() {
            // SAFETY: `max_pivot` is the final internal pivot of the leaf.
            let right_pivot = unsafe { *pivots.add(usize::from(max_pivot)) };
            let gap = usize::MAX.wrapping_sub(right_pivot);
            if gap > max_gap {
                max_gap = gap;
            }

            if max_gap > right_pivot.wrapping_sub(min) {
                return max_gap;
            }
        }
    }

    while offset <= max_pivot {
        // SAFETY: The loop keeps `offset` below the node data end.
        if unsafe { *slots.add(usize::from(offset)) }.is_null() {
            // SAFETY: The gap lies between two pivots in the leaf layout.
            let previous = unsafe { *pivots.add(usize::from(offset.wrapping_sub(1))) };
            // SAFETY: The gap lies between two pivots in the leaf layout.
            let current = unsafe { *pivots.add(usize::from(offset)) };
            let gap = current.wrapping_sub(previous);
            if gap > max_gap {
                max_gap = gap;
            }

            // Two adjacent NULL slots cannot both be gaps.
            offset = offset.wrapping_add(2);
        } else {
            offset = offset.wrapping_add(1);
        }
    }

    max_gap
}

/// Computes the largest allocation gap in a leaf.
///
/// # Safety
///
/// The node, pivots, and slots must describe one valid protected leaf. Dense
/// nodes use a NULL pivot pointer.
pub(crate) unsafe fn max_gap(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    min: usize,
    max: usize,
    pivots: *mut usize,
    slots: *mut *mut c_void,
) -> usize {
    if is_dense(node_type) {
        // SAFETY: The concrete layout determines the complete slot count.
        let slot_count = unsafe { bindings::rust_maple_tree_slot_count(node_type) };

        // SAFETY: A dense node contains no explicit pivots.
        unsafe { dense_max_gap(slots, slot_count) }
    } else {
        // SAFETY: The caller supplies a valid pivot-bearing leaf.
        unsafe { range_max_gap(node, node_type, min, max, pivots, slots) }
    }
}

/// Computes the largest allocation gap in a Maple Tree leaf.
///
/// # Safety
///
/// The C caller must provide one valid protected leaf. Dense nodes must have a
/// NULL `pivots` pointer.
#[export]
unsafe extern "C" fn rust_maple_tree_leaf_gap(
    node: *mut MapleNode,
    node_type: bindings::maple_type,
    min: usize,
    max: usize,
    pivots: *mut usize,
    slots: *mut *mut c_void,
) -> usize {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { max_gap(node, node_type, min, max, pivots, slots) }
}
