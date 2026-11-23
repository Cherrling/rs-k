// SPDX-License-Identifier: GPL-2.0-only

//! Native `list_lru` node-local state machine.
//!
//! Rust directly defines the public node-local entry points. It owns item
//! presence decisions, list placement, per-slice count transitions, shrinker
//! notification decisions, node count transitions, and walk status handling.
//! C helpers retain the intrusive list operations, spinlock and interrupt
//! modes, memcg resolution, atomic primitives, and callback dispatch.

use crate::bindings;
use core::{ffi::c_void, mem::MaybeUninit, ptr};
use macros::export;

type List = bindings::list_head;
type ListLru = bindings::list_lru;
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
unsafe extern "C" fn __list_lru_add(
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
unsafe extern "C" fn __list_lru_del(
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
unsafe extern "C" fn list_lru_isolate(l: *mut ListLruOne, item: *mut List) {
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
unsafe extern "C" fn list_lru_isolate_move(l: *mut ListLruOne, item: *mut List, head: *mut List) {
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe { bindings::rust_list_lru_item_move(item, head) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    // SAFETY: The caller guarantees valid objects and lock ownership.
    unsafe {
        bindings::rust_list_lru_slice_count_write(l, count.wrapping_sub(1));
    }
}

/// Returns the item count for a memory-cgroup slice.
///
/// # Safety
///
/// The public `list_lru_count_one()` contract applies.
#[export]
unsafe extern "C" fn list_lru_count_one(
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> usize {
    // SAFETY: The public count contract supplies valid lru and memcg state.
    let l = unsafe { bindings::rust_list_lru_rcu_lock_slice(lru, nid, memcg) };
    if l.is_null() {
        // SAFETY: The RCU read-side critical section remains active.
        unsafe { bindings::rust_list_lru_rcu_unlock() };
        return 0;
    }

    // SAFETY: The caller guarantees valid storage.
    let count = unsafe { bindings::rust_list_lru_slice_count_read(l) };
    // SAFETY: The RCU read-side critical section remains active.
    unsafe { bindings::rust_list_lru_rcu_unlock() };

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
/// `lru` and `nid` must identify a valid list node.
#[export]
unsafe extern "C" fn list_lru_count_node(lru: *mut ListLru, nid: core::ffi::c_int) -> usize {
    // SAFETY: `nid` identifies a valid node in the caller-provided array.
    let nlru = unsafe { (*lru).node.add(nid as usize) };
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
unsafe extern "C" fn walk_one_inner(
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

/// Adds an item after locking the selected slice.
///
/// # Safety
///
/// The public `list_lru_add()` contract applies. `irq` selects the lock mode.
unsafe fn add_with_lock(
    lru: *mut ListLru,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
    irq: bool,
) -> bool {
    let mut memcg = memcg;
    // SAFETY: The public add contract supplies valid lru and memcg state.
    let l = unsafe { bindings::rust_list_lru_walk_lock(lru, nid, &mut memcg, irq, false) };
    if l.is_null() {
        return false;
    }

    // SAFETY: The helper returned a locked slice in the selected node.
    let ret = unsafe { __list_lru_add(lru, l, item, nid, memcg) };
    // SAFETY: The slice remains locked after the add operation.
    unsafe { bindings::rust_list_lru_walk_unlock(l, irq) };
    ret
}

/// Deletes an item after locking the selected slice.
///
/// # Safety
///
/// The public `list_lru_del()` contract applies.
unsafe fn del_with_lock(
    lru: *mut ListLru,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> bool {
    let mut memcg = memcg;
    // SAFETY: The public delete contract supplies valid lru and memcg state.
    let l = unsafe { bindings::rust_list_lru_walk_lock(lru, nid, &mut memcg, false, false) };
    if l.is_null() {
        return false;
    }

    // SAFETY: The helper returned a locked slice in the selected node.
    let ret = unsafe { __list_lru_del(lru, l, item, nid) };
    // SAFETY: The slice remains locked after the delete operation.
    unsafe { bindings::rust_list_lru_walk_unlock(l, false) };
    ret
}

/// Adds an item to the node-local LRU.
///
/// # Safety
///
/// The public `list_lru_add()` contract applies.
#[export]
unsafe extern "C" fn list_lru_add(
    lru: *mut ListLru,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { add_with_lock(lru, item, nid, memcg, false) }
}

/// Adds an item with interrupts disabled while the slice is locked.
///
/// # Safety
///
/// The public `list_lru_add_irq()` contract applies.
#[export]
unsafe extern "C" fn list_lru_add_irq(
    lru: *mut ListLru,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { add_with_lock(lru, item, nid, memcg, true) }
}

/// Adds an object to the slice selected by the object's page.
///
/// # Safety
///
/// The public `list_lru_add_obj()` contract applies.
#[export]
unsafe extern "C" fn list_lru_add_obj(lru: *mut ListLru, item: *mut List) -> bool {
    let mut ctx = MaybeUninit::<bindings::rust_list_lru_obj_lock>::uninit();
    // SAFETY: The public add contract supplies a valid object-backed list node.
    if !unsafe { bindings::rust_list_lru_lock_obj(lru, item, false, ctx.as_mut_ptr()) } {
        return false;
    }
    // SAFETY: The helper initialized the context after it returned true.
    let mut ctx = unsafe { ctx.assume_init() };
    // SAFETY: The helper returned a locked slice for the object.
    let ret = unsafe { __list_lru_add(lru, ctx.l, item, ctx.nid, ctx.memcg) };
    // SAFETY: The slice remains locked after the add operation.
    unsafe { bindings::rust_list_lru_unlock_obj(&mut ctx, false) };
    ret
}

/// Deletes an item from the node-local LRU.
///
/// # Safety
///
/// The public `list_lru_del()` contract applies.
#[export]
unsafe extern "C" fn list_lru_del(
    lru: *mut ListLru,
    item: *mut List,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
) -> bool {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { del_with_lock(lru, item, nid, memcg) }
}

/// Deletes an object from the slice selected by the object's page.
///
/// # Safety
///
/// The public `list_lru_del_obj()` contract applies.
#[export]
unsafe extern "C" fn list_lru_del_obj(lru: *mut ListLru, item: *mut List) -> bool {
    let mut ctx = MaybeUninit::<bindings::rust_list_lru_obj_lock>::uninit();
    // SAFETY: The public delete contract supplies a valid object-backed list node.
    if !unsafe { bindings::rust_list_lru_lock_obj(lru, item, false, ctx.as_mut_ptr()) } {
        return false;
    }
    // SAFETY: The helper initialized the context after it returned true.
    let mut ctx = unsafe { ctx.assume_init() };
    // SAFETY: The helper returned a locked slice for the object.
    let ret = unsafe { __list_lru_del(lru, ctx.l, item, ctx.nid) };
    // SAFETY: The slice remains locked after the delete operation.
    unsafe { bindings::rust_list_lru_unlock_obj(&mut ctx, false) };
    ret
}

/// Walks one node-local list slice.
///
/// # Safety
///
/// The public `list_lru_walk_one()` contract applies.
#[export]
unsafe extern "C" fn list_lru_walk_one(
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
    isolate: WalkCallback,
    cb_arg: *mut c_void,
    nr_to_walk: *mut usize,
) -> usize {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { walk_one_inner(lru, nid, memcg, isolate, cb_arg, nr_to_walk, false) }
}

/// Walks one node-local list slice with interrupts disabled while locked.
///
/// # Safety
///
/// The public `list_lru_walk_one_irq()` contract applies.
#[export]
unsafe extern "C" fn list_lru_walk_one_irq(
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    memcg: *mut Memcg,
    isolate: WalkCallback,
    cb_arg: *mut c_void,
    nr_to_walk: *mut usize,
) -> usize {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { walk_one_inner(lru, nid, memcg, isolate, cb_arg, nr_to_walk, true) }
}

struct WalkMemcgArg {
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    isolate: WalkCallback,
    cb_arg: *mut c_void,
    nr_to_walk: *mut usize,
}

/// Adapts the memory-cgroup walker to the single-slice Rust walker.
///
/// # Safety
///
/// `memcg` must identify a live memory cgroup. `arg` must point to a valid
/// `WalkMemcgArg` for the duration of the call.
unsafe extern "C" fn walk_memcg_cb(memcg: *mut Memcg, arg: *mut c_void) -> usize {
    // SAFETY: The caller passes a valid `WalkMemcgArg` for the callback.
    let arg = unsafe { &mut *arg.cast::<WalkMemcgArg>() };
    // SAFETY: The callback contract preserves all stored pointers.
    unsafe {
        walk_one_inner(
            arg.lru,
            arg.nid,
            memcg,
            arg.isolate,
            arg.cb_arg,
            arg.nr_to_walk,
            false,
        )
    }
}

/// Walks every memory-cgroup slice on one node.
///
/// # Safety
///
/// The public `list_lru_walk_node()` contract applies.
#[export]
unsafe extern "C" fn list_lru_walk_node(
    lru: *mut ListLru,
    nid: core::ffi::c_int,
    isolate: WalkCallback,
    cb_arg: *mut c_void,
    nr_to_walk: *mut usize,
) -> usize {
    // SAFETY: Guaranteed by this function's contract.
    let mut isolated = unsafe {
        walk_one_inner(
            lru,
            nid,
            ptr::null_mut(),
            isolate,
            cb_arg,
            nr_to_walk,
            false,
        )
    };
    let mut arg = WalkMemcgArg {
        lru,
        nid,
        isolate,
        cb_arg,
        nr_to_walk,
    };

    // SAFETY: The helper walks only valid memory-cgroup slices and preserves
    // the scan budget until the callback returns.
    isolated += unsafe {
        bindings::rust_list_lru_walk_memcg(
            lru,
            nid,
            Some(walk_memcg_cb),
            ptr::addr_of_mut!(arg).cast(),
            nr_to_walk,
        )
    };
    isolated
}
