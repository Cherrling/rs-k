// SPDX-License-Identifier: GPL-2.0

//! Native Maple Tree write preparation and node preallocation.
//!
//! Rust owns write state normalization, write type selection, node request
//! calculation, allocation lifecycle, and error reset for the public
//! `mas_preallocate()` operation. C retains node layout, protected slot
//! access, write traversal primitives, node-pool allocation, and lockdep.

use super::maple_tree_core::{
    MaState, MaWrState, is_active, is_leaf, is_none, is_ptr, is_start, may_init_lock_check,
    node_type, reset, start,
};
use crate::bindings;
use core::ffi::c_void;
use macros::export;

const MA_STATE_PREALLOC: u8 = 1;

/// Resets a state and initializes the content of its write state.
///
/// # Safety
///
/// `wr_mas` and `wr_mas.mas` must be valid and protected by RCU or the tree
/// lock.
#[inline(always)]
unsafe fn reset_content(wr_mas: *mut MaWrState) -> *mut c_void {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: Guaranteed by this function's contract.
    unsafe { reset(mas) };
    // SAFETY: The caller supplies a valid protected state.
    unsafe { start(mas) }
}

/// Normalizes the state and records the content replaced by a write.
///
/// # Safety
///
/// `wr_mas` and `wr_mas.mas` must be valid and protected by RCU or the tree
/// lock.
unsafe fn prealloc_setup(wr_mas: *mut MaWrState) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid state.
    let status = unsafe { (*mas).status };

    if !is_active(mas) {
        if is_start(mas) {
            // SAFETY: The caller supplies a valid protected state.
            unsafe { (*wr_mas).content = start(mas) };
            return;
        }
        if status == bindings::maple_status_ma_pause
            || status == bindings::maple_status_ma_none
            || status == bindings::maple_status_ma_overflow
            || status == bindings::maple_status_ma_underflow
        {
            // SAFETY: The caller supplies a valid protected state.
            unsafe { (*wr_mas).content = reset_content(wr_mas) };
            return;
        }
    }

    // A spanning write cannot retain a partial walk.
    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).last > (*mas).max } {
        // SAFETY: The caller supplies a valid protected state.
        unsafe { (*wr_mas).content = reset_content(wr_mas) };
        return;
    }

    // SAFETY: The caller supplies a valid write state.
    if !unsafe { (*wr_mas).entry }.is_null() {
        // SAFETY: The caller supplies a valid active state.
        unsafe { (*wr_mas).content = start(mas) };
        return;
    }

    // SAFETY: The caller supplies a valid state.
    let encoded = unsafe { (*mas).node };
    // SAFETY: Node type extraction only inspects encoding bits.
    let node_type = unsafe { node_type(encoded.cast_const()) };
    // SAFETY: The caller supplies a valid state.
    if is_leaf(node_type) && unsafe { (*mas).last == (*mas).max } {
        // SAFETY: The caller supplies a valid protected state.
        unsafe { (*wr_mas).content = reset_content(wr_mas) };
        return;
    }

    // SAFETY: The caller supplies a valid active state.
    unsafe { (*wr_mas).content = start(mas) };
}

/// Returns the number of slots in a concrete node type.
#[inline(always)]
fn slot_count(node_type: bindings::maple_type) -> u8 {
    // SAFETY: The node type is valid.
    unsafe { bindings::rust_maple_tree_slot_count(node_type) }
}

/// Chooses the mutation strategy for a prepared write.
///
/// # Safety
///
/// `wr_mas` and `wr_mas.mas` must be valid and protected by the tree write
/// lock or an equivalent external lock.
unsafe fn store_type(wr_mas: *mut MaWrState) -> bindings::store_type {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };

    if is_none(mas) || is_ptr(mas) {
        return bindings::store_type_wr_store_root;
    }

    // SAFETY: The caller holds the write lock.
    if !unsafe { bindings::rust_maple_tree_wr_walk(wr_mas) } {
        return bindings::store_type_wr_spanning_store;
    }

    // SAFETY: The caller holds the write lock.
    unsafe { bindings::rust_maple_tree_wr_end_piv(wr_mas) };
    // SAFETY: The caller supplies a valid write state.
    if unsafe { (*wr_mas).entry }.is_null() {
        // SAFETY: The caller holds the write lock.
        unsafe { bindings::rust_maple_tree_wr_extend_null(wr_mas) };
    }

    // SAFETY: The caller supplies a valid state.
    if unsafe { (*wr_mas).r_min == (*mas).index && (*wr_mas).r_max == (*mas).last } {
        return bindings::store_type_wr_exact_fit;
    }

    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).index == 0 && (*mas).last == usize::MAX } {
        return bindings::store_type_wr_new_root;
    }

    // SAFETY: The caller holds the write lock.
    let new_end = unsafe { bindings::rust_maple_tree_wr_new_end(wr_mas) };
    // SAFETY: The caller supplies a valid initialized tree.
    let in_rcu = unsafe { bindings::rust_maple_tree_in_rcu((*mas).tree) };
    // SAFETY: The caller supplies a valid state.
    let appending = unsafe { (*mas).offset == (*mas).end };
    // SAFETY: `offset_end` cannot be less than `offset` after write traversal.
    let one_slot = unsafe { (*wr_mas).offset_end.wrapping_sub((*mas).offset) == 1 };
    // SAFETY: Write traversal selected a valid concrete node type.
    let node_type = unsafe { (*wr_mas).type_ };
    // SAFETY: Write traversal selected a valid concrete node type.
    let min_slots = unsafe { bindings::rust_maple_tree_min_slots(node_type) };

    if new_end < min_slots {
        // SAFETY: Write traversal selected a valid encoded node.
        if !unsafe { bindings::rust_maple_tree_enode_is_root((*mas).node) } {
            return bindings::store_type_wr_rebalance;
        }
        if !in_rcu {
            if appending {
                return bindings::store_type_wr_append;
            }
            // SAFETY: The caller supplies a valid state.
            if unsafe { (*mas).end == new_end } && one_slot {
                return bindings::store_type_wr_slot_store;
            }
        }
        return bindings::store_type_wr_node_store;
    }

    if new_end >= slot_count(node_type) {
        return bindings::store_type_wr_split_store;
    }

    if !in_rcu && appending {
        return bindings::store_type_wr_append;
    }

    // SAFETY: The caller supplies a valid state.
    if unsafe { (*mas).end == new_end } && (!in_rcu || one_slot) {
        return bindings::store_type_wr_slot_store;
    }

    bindings::store_type_wr_node_store
}

/// Calculates the number of nodes required by the selected write strategy.
///
/// # Safety
///
/// `wr_mas` and `wr_mas.mas` must be valid and the write type must have been
/// selected.
unsafe fn prealloc_count(wr_mas: *mut MaWrState, entry: *mut c_void) {
    // SAFETY: The caller supplies a valid write state.
    let mas = unsafe { (*wr_mas).mas };
    // SAFETY: The caller supplies a valid initialized state.
    let height = usize::try_from(unsafe { bindings::rust_maple_tree_height(mas) }).unwrap();
    let mut count = height * 3 + 1;
    // SAFETY: The caller supplies a valid write state.
    let delta = height.wrapping_sub(usize::from(unsafe { (*wr_mas).vacant_height }));

    // SAFETY: The caller supplies a valid state.
    match unsafe { (*mas).store_type } {
        bindings::store_type_wr_exact_fit
        | bindings::store_type_wr_append
        | bindings::store_type_wr_slot_store => count = 0,
        bindings::store_type_wr_spanning_store => {
            // SAFETY: The caller supplies a valid write state.
            let sufficient = unsafe { (*wr_mas).sufficient_height };
            // SAFETY: The caller supplies a valid write state.
            let vacant = unsafe { (*wr_mas).vacant_height };
            if sufficient < vacant {
                count = (height - usize::from(sufficient)) * 3 + 1;
            } else {
                count = delta * 3 + 1;
            }
        }
        bindings::store_type_wr_split_store => count = delta * 2 + 1,
        bindings::store_type_wr_rebalance => {
            // SAFETY: The caller supplies a valid write state.
            let sufficient = unsafe { (*wr_mas).sufficient_height };
            // SAFETY: The caller supplies a valid write state.
            let vacant = unsafe { (*wr_mas).vacant_height };
            if sufficient < vacant {
                count = (height - usize::from(sufficient)) * 2 + 1;
            } else {
                count = delta * 2 + 1;
            }
        }
        bindings::store_type_wr_node_store => {
            // SAFETY: The caller supplies a valid initialized tree.
            count = usize::from(unsafe { bindings::rust_maple_tree_in_rcu((*mas).tree) });
        }
        bindings::store_type_wr_new_root => count = 1,
        bindings::store_type_wr_store_root => {
            // SAFETY: The caller supplies a valid state.
            count = usize::from(
                unsafe { (*mas).last != 0 || (*mas).index != 0 } || (entry as usize) & 3 == 2,
            );
        }
        bindings::store_type_wr_invalid => {
            // SAFETY: This helper emits the retained impossible-state warning.
            unsafe { bindings::rust_maple_tree_warn_on_once(true) };
        }
        _ => {}
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).node_request = count };
}

/// Implements the complete public preallocation operation.
///
/// # Safety
///
/// `mas` must identify a valid initialized Maple tree and `entry` must be a
/// valid Maple Tree entry.
unsafe fn preallocate(mas: *mut MaState, entry: *mut c_void, gfp: bindings::gfp_t) -> i32 {
    let mut wr_mas = MaWrState {
        mas,
        entry,
        ..Default::default()
    };

    // SAFETY: The caller supplies a valid initialized state.
    unsafe { may_init_lock_check(mas) };
    // SAFETY: The complete write state is initialized.
    unsafe { prealloc_setup(&mut wr_mas) };
    // SAFETY: The caller holds the write lock.
    let store_type = unsafe { store_type(&mut wr_mas) };
    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).store_type = store_type };
    // SAFETY: Write preparation selected a valid strategy.
    unsafe { prealloc_count(&mut wr_mas, entry) };

    // SAFETY: The caller supplies a valid writable state.
    if unsafe { (*mas).node_request } != 0 {
        // SAFETY: The caller supplies a valid writable state.
        unsafe { (*mas).mas_flags &= !MA_STATE_PREALLOC };
        // SAFETY: The complete state and write request select a valid allocation.
        unsafe { bindings::rust_maple_tree_alloc_nodes(mas, gfp) };
        // SAFETY: The caller supplies a valid state after allocation.
        if unsafe { (*mas).status } == bindings::maple_status_ma_error {
            // SAFETY: Allocation failure installed an encoded errno.
            let ret = unsafe { bindings::rust_maple_tree_error(mas) };
            // SAFETY: The caller supplies a valid writable state.
            unsafe { (*mas).node_request = 0 };
            // SAFETY: The caller supplies a valid state with allocated nodes.
            unsafe { bindings::rust_maple_tree_destroy(mas) };
            // SAFETY: The caller supplies a valid writable state.
            unsafe { reset(mas) };
            return ret;
        }
    }

    // SAFETY: The caller supplies a valid writable state.
    unsafe { (*mas).mas_flags |= MA_STATE_PREALLOC };
    0
}

/// Preallocates nodes for a later Maple Tree store.
///
/// # Safety
///
/// The C caller must provide a valid state and entry.
#[export]
unsafe extern "C" fn rust_mas_preallocate(
    mas: *mut MaState,
    entry: *mut c_void,
    gfp: bindings::gfp_t,
) -> i32 {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { preallocate(mas, entry, gfp) }
}
