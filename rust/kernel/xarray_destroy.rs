// SPDX-License-Identifier: GPL-2.0

//! Native XArray teardown.
//!
//! Rust owns root detachment ordering, mark reinitialization, empty allocation
//! handling, subtree reclamation control, and destruction reuse semantics. C
//! retains interrupt-safe locking, root publication, flag storage, node layout,
//! RCU reclamation, and the locked XArray state constructor.

use crate::bindings;
use crate::{xarray_free_nodes, xarray_marks};
use core::ffi::c_void;
use macros::export;

type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const FREE_MARK: bindings::xa_mark_t = bindings::rust_xarray_mark_constant_RUST_XARRAY_FREE_MARK;

#[inline(always)]
/// Detaches and returns the locked root entry.
///
/// # Safety
///
/// `xa` must be valid and the caller must hold the XArray lock with interrupts
/// disabled.
unsafe fn detach_root_locked(xa: *mut XArray) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_detach_root_locked(xa) }
}

#[inline(always)]
/// Returns whether the XArray uses the zero-busy allocation configuration.
///
/// # Safety
///
/// `xa` must point to a valid XArray.
unsafe fn zero_busy(xa: *const XArray) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_zero_busy(xa) }
}

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

/// Frees all internal XArray data and resets the root marks.
///
/// Referenced entries are not owned by the XArray and are not released by this
/// operation.
///
/// # Safety
///
/// `xas` must point to a valid XArray state. The caller must hold the XArray
/// lock with interrupts disabled. The state must reference the root with no
/// selected node or slot.
pub(crate) unsafe fn destroy_tree(xas: *mut XaState) {
    // SAFETY: The caller supplies a valid locked state.
    let xa = unsafe { (*xas).xa };
    // SAFETY: The caller holds the lock and the state references the root.
    let root = unsafe { detach_root_locked(xa) };

    // SAFETY: The caller holds the XArray lock.
    unsafe { xarray_marks::marks_init(xas) };
    // SAFETY: The caller holds the lock and zero-busy is a flag query.
    if unsafe { zero_busy(xa) } {
        // SAFETY: The caller holds the lock that protects root marks.
        unsafe { xarray_marks::root_mark_clear(xa, FREE_MARK) };
    }

    if is_node(root) {
        // SAFETY: The root was detached while the lock was held. Subtree
        // reclamation publishes retry entries before RCU reclamation.
        unsafe { xarray_free_nodes::free_nodes(xas, to_node(root)) };
    }
}

/// Tears down an XArray through the internal ABI.
///
/// # Safety
///
/// `xas` must point to a valid XArray root state under the XArray lock with
/// interrupts disabled.
#[export]
unsafe extern "C" fn rust_xarray_destroy_tree(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { destroy_tree(xas) };
}
