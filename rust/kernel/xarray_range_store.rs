// SPDX-License-Identifier: GPL-2.0

//! Native XArray multi-index range stores and order queries.
//!
//! Rust owns range decomposition, order calculation, store retries, cursor
//! positioning, and sibling-run order discovery. C retains XArray locking,
//! RCU protection, tagged-entry encoding, protected slot access, and node
//! layout.

use crate::{bindings, xarray_create, xarray_load, xarray_nomem, xarray_store};
use core::{ffi::c_void, mem::MaybeUninit, ptr};
use macros::export;

type Gfp = bindings::gfp_t;
type XArray = bindings::xarray;
type XaNode = bindings::xa_node;
type XaState = bindings::xa_state;

const CHUNK_MASK: usize = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_MASK as usize;
const CHUNK_SHIFT: u32 = bindings::rust_xarray_constant_RUST_XARRAY_CHUNK_SHIFT;
const BITS_PER_LONG: u32 = bindings::BITS_PER_LONG;
const EINVAL: i32 = bindings::rust_xarray_range_store_constant_RUST_XARRAY_RANGE_STORE_EINVAL;

#[inline(always)]
fn is_internal(entry: *const c_void) -> bool {
    entry.addr() & 3 == 2
}

#[inline(always)]
/// Emits the existing diagnostic for an internal entry.
///
/// # Safety
///
/// The caller must have classified `entry` as internal.
unsafe fn warn_internal(entry: *const c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_range_store_warn_internal(entry) };
}

#[inline(always)]
/// Encodes an errno as an XArray error entry.
///
/// # Safety
///
/// `error` must be a negative errno accepted by the XArray API.
unsafe fn error_entry(error: i32) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_range_store_error(error as isize) }
}

#[inline(always)]
/// Locks an XArray for an exclusive update.
///
/// # Safety
///
/// `xa` must point to a valid initialized XArray.
unsafe fn lock(xa: *mut XArray) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xa_lock(xa) };
}

#[inline(always)]
/// Unlocks an XArray after an exclusive update.
///
/// # Safety
///
/// The caller must hold the lock for `xa`.
unsafe fn unlock(xa: *mut XArray) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xa_unlock(xa) };
}

#[inline(always)]
/// Resets an XArray cursor to restart at the root.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn reset(xas: *mut XaState) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_reset(xas) };
}

#[inline(always)]
/// Reads the errno stored in an XArray cursor.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn error(xas: *const XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_error(xas) }
}

#[inline(always)]
/// Reads one XArray slot with the API's RCU or lock protection.
///
/// # Safety
///
/// `xas`, `node`, and `offset` must identify a valid slot. The caller must hold
/// the XArray lock or the RCU read lock.
unsafe fn load_slot(xas: *const XaState, node: *const XaNode, offset: usize) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_range_store_entry(xas, node, offset as u32) }
}

#[inline(always)]
/// Checks the resolved-first-slot invariant.
///
/// # Safety
///
/// `node` and `entry` must identify the first slot of a resolved entry.
unsafe fn check_not_sibling(node: *const XaNode, entry: *const c_void) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { bindings::xarray_range_store_check_not_sibling(node, entry) };
}

#[inline(always)]
fn is_sibling(entry: *const c_void) -> bool {
    // SAFETY: Entry classification does not dereference the pointer.
    unsafe { bindings::xarray_is_sibling(entry) }
}

/// Positions a cursor for a multi-index entry of the requested order.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state.
unsafe fn set_order(xas: *mut XaState, index: usize, order: u32) {
    // SAFETY: The caller guarantees a valid writable cursor.
    let state = unsafe { &mut *xas };

    state.xa_index = if order < BITS_PER_LONG {
        (index >> order) << order
    } else {
        0
    };
    state.xa_shift = (order - order % CHUNK_SHIFT) as u8;
    state.xa_sibs = ((1u32 << (order % CHUNK_SHIFT)) - 1) as u8;
    // SAFETY: The caller provides a writable cursor.
    unsafe { reset(xas) };
}

/// Positions a cursor on the next natural multi-index range.
///
/// # Safety
///
/// `xas` must point to a valid writable XArray state and `first` must not be
/// greater than `last`.
unsafe fn set_range(xas: *mut XaState, mut first: usize, last: usize) {
    let mut shift = 0u32;
    let mut sibs = last - first;
    let mut offset = CHUNK_MASK;

    // SAFETY: The caller provides a valid cursor.
    unsafe {
        (*xas).xa_index = first;
        reset(xas);
    }

    while first & CHUNK_MASK == 0 {
        if sibs < CHUNK_MASK {
            break;
        }
        if sibs == CHUNK_MASK && offset < CHUNK_MASK {
            break;
        }
        shift += CHUNK_SHIFT;
        if offset == CHUNK_MASK {
            offset = sibs & CHUNK_MASK;
        }
        sibs >>= CHUNK_SHIFT;
        first >>= CHUNK_SHIFT;
    }

    offset = first & CHUNK_MASK;
    if offset.wrapping_add(sibs) > CHUNK_MASK {
        sibs = CHUNK_MASK - offset;
    }
    let end = first
        .wrapping_add(sibs)
        .wrapping_add(1)
        .wrapping_shl(shift)
        .wrapping_sub(1);
    if end > last {
        sibs = sibs.wrapping_sub(1);
    }

    // SAFETY: The caller provides a writable cursor.
    unsafe {
        (*xas).xa_shift = shift as u8;
        (*xas).xa_sibs = sibs as u8;
    }
}

#[inline(always)]
/// Returns the number of indices covered by the current range cursor.
///
/// # Safety
///
/// `xas` must point to a valid initialized XArray state.
unsafe fn size(xas: *const XaState) -> usize {
    // SAFETY: The caller guarantees a valid initialized cursor.
    let state = unsafe { &*xas };
    (usize::from(state.xa_sibs) + 1).wrapping_shl(u32::from(state.xa_shift))
}

/// Converts the final cursor state to the public XArray result.
///
/// # Safety
///
/// `xas` must point to a valid XArray state.
unsafe fn result(xas: *const XaState) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { error(xas) } == 0 {
        ptr::null_mut()
    } else {
        // SAFETY: An error state stores the encoded result in `xa_node`.
        unsafe { (*xas).xa_node.cast() }
    }
}

/// Implements the complete public range-store policy.
///
/// # Safety
///
/// `xa` must point to a valid initialized XArray.
unsafe fn store_range(
    xa: *mut XArray,
    mut first: usize,
    last: usize,
    entry: *mut c_void,
    gfp: Gfp,
) -> *mut c_void {
    if is_internal(entry.cast_const()) {
        // SAFETY: The classification selected the existing diagnostic.
        unsafe { warn_internal(entry.cast_const()) };
        // SAFETY: EINVAL is a valid XArray errno.
        return unsafe { error_entry(EINVAL) };
    }
    if last < first {
        // SAFETY: EINVAL is a valid XArray errno.
        return unsafe { error_entry(EINVAL) };
    }

    let mut state = MaybeUninit::<XaState>::uninit();
    // SAFETY: The output points to writable storage for one full state.
    unsafe { bindings::xarray_state_init(state.as_mut_ptr(), xa, 0) };
    // SAFETY: The helper initialized the complete state.
    let mut state = unsafe { state.assume_init() };

    loop {
        // SAFETY: The caller provides a valid XArray.
        unsafe { lock(xa) };

        if !entry.is_null() {
            let order = last.wrapping_add(1).trailing_zeros();
            // SAFETY: The cursor is writable and the lock is held.
            unsafe { set_order(&mut state, last, order) };
            // SAFETY: The cursor is valid, writable, and the lock is held.
            let _ = unsafe { xarray_create::create(&mut state, true) };
        }

        // SAFETY: The cursor is valid and the lock is held.
        let mut state_error = unsafe { error(&state) };
        while state_error == 0 {
            // SAFETY: The cursor is writable and the lock is held.
            unsafe { set_range(&mut state, first, last) };
            // SAFETY: The cursor is valid and the lock is held.
            let _ = unsafe { xarray_store::store(&mut state, entry) };
            // SAFETY: The cursor remains valid and the lock is still held.
            state_error = unsafe { error(&state) };
            if state_error != 0 {
                break;
            }

            // SAFETY: A successful store leaves a valid range cursor.
            let covered = unsafe { size(&state) };
            if first == last || covered == 0 {
                break;
            }
            let next = first.wrapping_add(covered);
            if next <= first || next > last {
                break;
            }
            first = next;
        }

        // SAFETY: This function acquired the lock immediately above.
        unsafe { unlock(xa) };

        // SAFETY: The lock is released and the cursor remains valid.
        if !unsafe { xarray_nomem::nomem(&mut state, gfp) } {
            // SAFETY: The cursor is valid and no lock is held.
            return unsafe { result(&state) };
        }
    }
}

/// Implements the order query for a loaded cursor.
///
/// # Safety
///
/// `xas` must be a valid loaded cursor that does not reference a sibling
/// entry. The caller must hold the XArray lock or the RCU read lock.
unsafe fn cursor_order(xas: *const XaState) -> i32 {
    // SAFETY: The caller guarantees a valid loaded cursor.
    let state = unsafe { &*xas };
    let node = state.xa_node;
    if node.is_null() {
        return 0;
    }

    let mut order = 0u32;
    // SAFETY: The loaded cursor identifies the first slot of the entry.
    let first = unsafe { load_slot(xas, node, usize::from(state.xa_offset)) };
    // SAFETY: The contract requires a resolved first slot.
    unsafe { check_not_sibling(node, first) };

    loop {
        let slot = usize::from(state.xa_offset) + (1usize << order);
        if slot > CHUNK_MASK {
            break;
        }

        // SAFETY: The scan stays within this node's slots.
        let entry = unsafe { load_slot(xas, node, slot) };
        if !is_sibling(entry.cast_const()) {
            break;
        }
        order += 1;
    }

    // SAFETY: The caller guarantees a live node.
    (order + u32::from(unsafe { (*node).shift })) as i32
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
/// Enters an RCU read-side critical section.
fn rcu_read_lock() -> RcuGuard {
    // SAFETY: The matching unlock is performed by the returned guard.
    unsafe { bindings::rcu_read_lock() };
    RcuGuard
}

/// Implements the RCU-protected public order query.
///
/// # Safety
///
/// `xa` must point to a valid initialized XArray.
unsafe fn public_order(xa: *const XArray, index: usize) -> i32 {
    let mut state = MaybeUninit::<XaState>::uninit();
    // SAFETY: The output points to writable storage for one full state.
    unsafe { bindings::xarray_state_init(state.as_mut_ptr(), xa.cast_mut(), index) };
    // SAFETY: The helper initialized the complete state.
    let mut state = unsafe { state.assume_init() };

    let _guard = rcu_read_lock();
    // SAFETY: The cursor is valid and the RCU read lock is held.
    let entry = unsafe { xarray_load::load_advanced(&mut state) };
    if entry.is_null() {
        return 0;
    }

    // SAFETY: The load resolved a non-NULL entry under RCU protection.
    unsafe { cursor_order(&state) }
}

/// Stores an entry over an inclusive index range through the production ABI.
///
/// # Safety
///
/// The C caller must provide a valid XArray and valid allocation flags.
#[export]
unsafe extern "C" fn rust_xarray_store_range(
    xa: *mut XArray,
    first: usize,
    last: usize,
    entry: *mut c_void,
    gfp: Gfp,
) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { store_range(xa, first, last, entry, gfp) }
}

/// Returns the order of a loaded cursor through the production ABI.
///
/// # Safety
///
/// The C caller must provide a valid loaded cursor and the required lock or
/// RCU protection.
#[export]
unsafe extern "C" fn rust_xarray_xas_get_order(xas: *mut XaState) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { cursor_order(xas) }
}

/// Returns the order at one index through the production ABI.
///
/// # Safety
///
/// The C caller must provide a valid XArray.
#[export]
unsafe extern "C" fn rust_xarray_get_order(xa: *mut XArray, index: usize) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { public_order(xa, index) }
}
