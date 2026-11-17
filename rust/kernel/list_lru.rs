// SPDX-License-Identifier: GPL-2.0-only

//! Native `list_lru` node-local state machine.
//!
//! Rust owns item presence decisions, list placement, per-slice count
//! transitions, shrinker notification decisions, node count transitions, and
//! walk status handling. C helpers retain the intrusive list operations,
//! spinlock and interrupt modes, memcg resolution, atomic primitives, and
//! callback dispatch.

use crate::bindings;
use core::{ffi::c_void, ptr};
use macros::export;

type List = bindings::list_head;
type ListLru = bindings::list_lru;
type ListLruNode = bindings::list_lru_node;
type ListLruOne = bindings::list_lru_one;
type Memcg = bindings::mem_cgroup;
type Status = bindings::lru_status;
type WalkCallback = bindings::list_lru_walk_cb;

/// Adds a queued item and updates both slice and node counts.
///
/// # Safety
///
/// `lru`, `l`, and `item` must be valid. The caller must hold `l->lock`.
/// `nid` must identify the node that contains `l`. `memcg` must identify the
/// cgroup that owns `l` and must remain live until this function returns.
#[export]
unsafe extern "C" fn rust_list_lru_add_slice(
    lru: *mut ListLru,
    l: *mut ListLruOne,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> bool {
    // SAFETY: The caller guarantees valid objects and lock ownership.
    if unsafe { bindings::rust_list_lru_item_empty(item) } {
        // SAFETY: The caller guarantees valid objects and lock ownership.
        unsafe { bindings::rust_list_lru_item_add_tail(item, l) };

        // SAFETY: The caller guarantees valid objects and lock ownership.
        let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
        // SAFETY: The caller guarantees valid objects and lock ownership.
        unsafe {
            bindings::rust_list_lru_slice_count_write(l, count.wrapping_add(1));
        }

        if count == 0 {
            // SAFETY: The caller guarantees that the owning memcg is live.
            let shrinker_id = unsafe { bindings::rust_list_lru_shrinker_id(lru) };
            // SAFETY: The caller guarantees that the owning memcg is live.
            unsafe { bindings::rust_list_lru_set_shrinker_bit(memcg, nid, shrinker_id) };
        }

        // SAFETY: `nid` identifies the node that contains `l`.
        let nlru = unsafe { (*lru).node.add(nid as usize) };
        // SAFETY: The caller guarantees a valid node and lock ownership.
        unsafe { bindings::rust_list_lru_node_count_inc(nlru) };
        true
    } else {
        false
    }
}

/// Removes a queued item and updates both slice and node counts.
///
/// # Safety
///
/// `lru` and `l` must be valid, and `nid` must identify the node that contains
/// `l`. `item` must remain valid until this function returns. The caller must
/// hold `l->lock`.
#[export]
unsafe extern "C" fn rust_list_lru_del_slice(
    lru: *mut ListLru,
    l: *mut ListLruOne,
    item: *mut List,
    nid: core::ffi::c_int,
) -> bool {
    // SAFETY: The caller guarantees valid objects and lock ownership.
    if unsafe { bindings::rust_list_lru_item_empty(item) } {
        return false;
    }

    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe { bindings::rust_list_lru_item_del_init(item) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe {
        bindings::rust_list_lru_slice_count_write(l, count.wrapping_sub(1));
    }

    // SAFETY: `nid` identifies the node that contains `l`.
    let nlru = unsafe { (*lru).node.add(nid as usize) };
    // SAFETY: The caller guarantees a valid node and lock ownership.
    unsafe { bindings::rust_list_lru_node_count_dec(nlru) };
    true
}

/// Removes an item from a slice without changing the node count.
///
/// The walker owns the node count transition that follows an isolate
/// callback.
///
/// # Safety
///
/// `l` and `item` must be valid. The caller must hold `l->lock`, and `item`
/// must be queued in `l`.
#[export]
unsafe extern "C" fn rust_list_lru_isolate_slice(l: *mut ListLruOne, item: *mut List) {
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe { bindings::rust_list_lru_item_del_init(item) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe {
        bindings::rust_list_lru_slice_count_write(l, count.wrapping_sub(1));
    }
}

/// Moves an item out of a slice without changing the node count.
///
/// The walker owns the node count transition that follows an isolate
/// callback.
///
/// # Safety
///
/// `l`, `item`, and `head` must be valid. The caller must hold `l->lock`, and
/// `item` must be queued in `l`.
#[export]
unsafe extern "C" fn rust_list_lru_isolate_move_slice(
    l: *mut ListLruOne,
    item: *mut List,
    head: *mut List,
) {
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe { bindings::rust_list_lru_item_move(item, head) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe {
        bindings::rust_list_lru_slice_count_write(l, count.wrapping_sub(1));
    }
}

/// Returns a slice count with dead or transient negative states clamped to
/// zero.
///
/// # Safety
///
/// `l` must point to a valid list slice. The caller must keep its storage live.
#[export]
unsafe extern "C" fn rust_list_lru_count_slice(l: *mut ListLruOne) -> usize {
    // SAFETY: The caller guarantees valid storage.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    if count < 0 {
        0
    } else {
        count as usize
    }
}

/// Returns the per-node item count.
///
/// # Safety
///
/// `nlru` must point to a valid list node.
#[export]
unsafe extern "C" fn rust_list_lru_count_node(nlru: *mut ListLruNode) -> usize {
    // SAFETY: The caller guarantees valid storage.
    unsafe { bindings::rust_list_lru_node_count_read(nlru) as usize }
}

/// Walks one node-local list slice and applies callback status transitions.
///
/// The callback owns the status-specific lock disposition used by the
/// existing C walker. In particular, restart and stop statuses return with
/// the slice lock released.
///
/// # Safety
///
/// All pointers must satisfy the public `list_lru_walk_one()` contract.
/// `nid` must identify a valid node. `nr_to_walk` must point to writable
/// storage. The callback must obey the existing list walk callback contract.
#[export]
unsafe extern "C" fn rust_list_lru_walk_one(
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
    isolate: WalkCallback,
    cb_arg: *mut c_void,
    nr_to_walk: *mut usize,
    irq_off: bool,
) -> usize {
    let Some(isolate) = isolate else {
        // SAFETY: A missing callback violates the public walk contract.
        unsafe { bindings::rust_list_lru_invalid_status() };
    };

    // SAFETY: `nid` identifies a valid node in the caller-provided array.
    let nlru = unsafe { (*lru).node.add(nid as usize) };
    let mut current_memcg = memcg;
    let mut isolated = 0;

    'restart: loop {
        // SAFETY: The public walk contract supplies valid lru and memcg state.
        let l = unsafe {
            bindings::rust_list_lru_walk_lock(lru, nid, &mut current_memcg, irq_off, true)
        };
        if l.is_null() {
            return isolated;
        }

        // SAFETY: `l` is locked and valid until a restart or stop status.
        let head = unsafe { ptr::addr_of_mut!((*l).list) };
        // SAFETY: `l` is locked, so its list links are stable.
        let mut item = unsafe { (*l).list.next };

        while item != head {
            // SAFETY: The list iteration invariant leaves `item` queued.
            let next = unsafe { (*item).next };
            // SAFETY: The caller supplies writable scan budget storage.
            if unsafe { *nr_to_walk } == 0 {
                break;
            }
            // SAFETY: The caller supplies writable scan budget storage.
            unsafe { *nr_to_walk -= 1 };

            // SAFETY: The callback contract permits this item and locked list.
            let status = unsafe { isolate(item, l, cb_arg) };

            if status == Status::LRU_REMOVED {
                isolated += 1;
                // SAFETY: The removed item belongs to the node.
                unsafe { bindings::rust_list_lru_node_count_dec(nlru) };
                item = next;
            } else if status == Status::LRU_REMOVED_RETRY {
                isolated += 1;
                // SAFETY: The removed item belongs to the node.
                unsafe { bindings::rust_list_lru_node_count_dec(nlru) };
                continue 'restart;
            } else if status == Status::LRU_RETRY {
                continue 'restart;
            } else if status == Status::LRU_ROTATE {
                // SAFETY: The callback returned with the slice lock held.
                unsafe { bindings::rust_list_lru_item_move_tail(item, l) };
                item = next;
            } else if status == Status::LRU_SKIP {
                item = next;
            } else if status == Status::LRU_STOP {
                return isolated;
            } else {
                // SAFETY: The callback returned a status outside the enum.
                unsafe { bindings::rust_list_lru_invalid_status() };
            }
        }

        // SAFETY: Iteration ends with the slice lock still held.
        unsafe { bindings::rust_list_lru_walk_unlock(l, irq_off) };
        return isolated;
    }
}
