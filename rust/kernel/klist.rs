// SPDX-License-Identifier: GPL-2.0-only

//! Native klist operations.
//!
//! Rust defines the public symbols for insertion, deletion, attachment state,
//! and iteration. It owns list placement, node lifecycle state, reference
//! transitions, iterator reference transfer, dead-node skipping, and
//! traversal direction. C retains the intrusive layouts, locking primitives,
//! linked-list pointer updates, refcount primitives, initialization, and the
//! blocking remove wakeup boundary.

use crate::{bindings, container_of};
use core::{
    ffi::{c_int, c_void},
    ptr,
};
use macros::export;

type Iter = bindings::klist_iter;
type KList = bindings::klist;
type List = bindings::list_head;
type Node = bindings::klist_node;

const KNODE_DEAD: usize = 1;
const KNODE_KLIST_MASK: usize = !KNODE_DEAD;

/// Returns the klist pointer stored in a node after removing the dead bit.
///
/// # Safety
///
/// `node` must point to a valid klist node.
#[inline]
unsafe fn node_klist(node: *mut Node) -> *mut KList {
    // SAFETY: The caller guarantees a valid node.
    ((unsafe { (*node).n_klist } as usize) & KNODE_KLIST_MASK) as *mut KList
}

/// Returns whether a node has the deleted bit set.
///
/// # Safety
///
/// `node` must point to a valid klist node.
#[inline]
unsafe fn node_dead(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees a valid node.
    (unsafe { (*node).n_klist } as usize) & KNODE_DEAD != 0
}

/// Stores an owner pointer while preserving the invariant that a new node is
/// not dead.
///
/// # Safety
///
/// `node` must point to a valid klist node. `klist` must be null or point to
/// the initialized list that owns the node.
#[inline]
unsafe fn set_node_klist(node: *mut Node, klist: *mut KList) {
    // SAFETY: The caller guarantees a valid node.
    unsafe { (*node).n_klist = klist.cast::<c_void>() };
    // SAFETY: The node was just updated and remains valid.
    let dead = unsafe { node_dead(node) };
    let _ = crate::warn_on!(dead);
}

/// Marks a node dead while preserving its owner pointer.
///
/// # Safety
///
/// `node` must point to a valid, live klist node.
#[inline]
unsafe fn kill_node(node: *mut Node) {
    // SAFETY: The caller guarantees a valid node.
    let dead = unsafe { node_dead(node) };
    let _ = crate::warn_on!(dead);
    // SAFETY: The owner pointer field is writable.
    unsafe {
        (*node).n_klist = ((*node).n_klist as usize | KNODE_DEAD) as *mut c_void;
    }
}

/// Returns the embedded list link for a klist node.
///
/// # Safety
///
/// `node` must point to a valid klist node.
#[inline]
unsafe fn node_list(node: *mut Node) -> *mut List {
    // SAFETY: The caller guarantees a valid node.
    unsafe { ptr::addr_of_mut!((*node).n_node) }
}

/// Returns the klist node that contains a linked list entry.
///
/// # Safety
///
/// `list` must point to the `n_node` member of a klist node.
#[inline]
unsafe fn node_from_list(list: *mut List) -> *mut Node {
    // SAFETY: The caller guarantees the list link is embedded in a node.
    unsafe { container_of!(list, Node, n_node) }
}

/// Returns the list sentinel used by a klist.
///
/// The returned pointer is suitable only for comparisons with real nodes.
///
/// # Safety
///
/// `klist` must point to an initialized klist.
#[inline]
unsafe fn list_sentinel(klist: *mut KList) -> *mut Node {
    // SAFETY: The k_list field is embedded in the caller-provided klist.
    unsafe { container_of!(ptr::addr_of_mut!((*klist).k_list), Node, n_node) }
}

/// Locks a klist.
///
/// # Safety
///
/// `klist` must point to an initialized klist.
#[inline]
unsafe fn lock(klist: *mut KList) {
    // SAFETY: The lock is embedded in the caller-provided klist.
    unsafe { bindings::klist_spin_lock(ptr::addr_of_mut!((*klist).k_lock)) };
}

/// Unlocks a klist.
///
/// # Safety
///
/// The caller must hold the klist lock.
#[inline]
unsafe fn unlock(klist: *mut KList) {
    // SAFETY: The caller guarantees that the klist lock is held.
    unsafe { bindings::klist_spin_unlock(ptr::addr_of_mut!((*klist).k_lock)) };
}

/// Locks a klist and saves interrupt flags.
///
/// # Safety
///
/// `klist` must point to an initialized klist.
#[inline]
unsafe fn lock_irqsave(klist: *mut KList, flags: &mut usize) {
    // SAFETY: The caller guarantees a valid klist and writable flags.
    unsafe {
        bindings::klist_spin_lock_irqsave(ptr::addr_of_mut!((*klist).k_lock), flags);
    }
}

/// Unlocks a klist and restores interrupt flags.
///
/// # Safety
///
/// The caller must hold the klist lock and pass the matching saved flags.
#[inline]
unsafe fn unlock_irqrestore(klist: *mut KList, flags: usize) {
    // SAFETY: The caller guarantees that the klist lock is held.
    unsafe {
        bindings::klist_spin_unlock_irqrestore(ptr::addr_of_mut!((*klist).k_lock), flags);
    }
}

/// Initializes a klist node and takes its initial reference.
///
/// # Safety
///
/// `klist` and `node` must be valid and uninitialized for klist use.
#[inline]
unsafe fn init_node(klist: *mut KList, node: *mut Node) {
    // SAFETY: The caller guarantees valid uninitialized objects.
    unsafe {
        bindings::klist_list_init(node_list(node));
        bindings::klist_refcount_set(ptr::addr_of_mut!((*node).n_ref), 1);
    }
    // SAFETY: The node is now ready to be associated with the klist.
    unsafe { set_node_klist(node, klist) };
    // SAFETY: The callback contract belongs to the klist owner.
    if let Some(get) = unsafe { (*klist).get } {
        // SAFETY: Public klist initialization requires a valid callback
        // contract and this node is initialized before the callback runs.
        unsafe { get(node) };
    }
}

/// Adds a node after a list entry.
///
/// # Safety
///
/// `klist` and `node` must be valid, and `node` must be isolated.
#[inline]
unsafe fn add_after(klist: *mut KList, node: *mut Node, position: *mut List) {
    // SAFETY: The caller guarantees valid objects and an isolated node.
    unsafe {
        lock(klist);
        bindings::klist_list_add(node_list(node), position);
        unlock(klist);
    }
}

/// Adds a node before a list entry.
///
/// # Safety
///
/// `klist` and `node` must be valid, and `node` must be isolated.
#[inline]
unsafe fn add_before(klist: *mut KList, node: *mut Node, position: *mut List) {
    // SAFETY: The caller guarantees valid objects and an isolated node.
    unsafe {
        lock(klist);
        bindings::klist_list_add_tail(node_list(node), position);
        unlock(klist);
    }
}

/// Takes an additional reference on a node.
///
/// # Safety
///
/// `node` must be alive and the caller must already own a reference or hold
/// the klist lock.
#[inline]
unsafe fn refcount_inc(node: *mut Node) {
    // SAFETY: The caller guarantees a node with a live reference.
    unsafe { bindings::klist_refcount_inc(ptr::addr_of_mut!((*node).n_ref)) };
}

/// Takes a reference only when the node is not already released.
///
/// # Safety
///
/// `node` must point to a valid klist node allocation.
#[inline]
unsafe fn refcount_inc_not_zero(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees a valid node allocation.
    unsafe { bindings::klist_refcount_inc_not_zero(ptr::addr_of_mut!((*node).n_ref)) }
}

/// Drops a reference and reports whether it was the final reference.
///
/// # Safety
///
/// The caller must own a reference to the node.
#[inline]
unsafe fn refcount_dec_and_test(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees that it owns a reference.
    unsafe { bindings::klist_refcount_dec_and_test(ptr::addr_of_mut!((*node).n_ref)) }
}

/// Releases a klist node while the klist lock is held.
///
/// # Safety
///
/// The caller must hold the list lock, the node must be dead, and the final
/// reference must have just been dropped.
unsafe fn release(node: *mut Node) {
    // SAFETY: The caller guarantees a valid dead node.
    let dead = unsafe { node_dead(node) };
    let _ = crate::warn_on!(!dead);
    // SAFETY: The caller guarantees the node is linked in its list.
    unsafe { bindings::klist_list_del(node_list(node)) };
    // SAFETY: The node is still valid while it is detached.
    unsafe { set_node_klist(node, ptr::null_mut()) };
    // SAFETY: Scheduler and task wakeup state remain a C boundary.
    unsafe { bindings::klist_wake_waiters(node) };
}

/// Drops the initial or iterator reference held by the caller.
///
/// # Safety
///
/// `node` must be attached and owned by a klist. The caller must hold a
/// reference to the node.
#[inline]
unsafe fn dec_and_del(node: *mut Node) -> bool {
    // SAFETY: The caller guarantees the reference and attachment contract.
    if unsafe { refcount_dec_and_test(node) } {
        // SAFETY: The release runs under the same lock as the final drop.
        unsafe { release(node) };
        true
    } else {
        false
    }
}

/// Drops one node reference and optionally marks the node dead.
///
/// # Safety
///
/// `node` must be attached and owned by a klist. The caller must hold a
/// reference to the node.
unsafe fn put(node: *mut Node, kill: bool) {
    // SAFETY: The caller guarantees an attached node.
    let klist = unsafe { node_klist(node) };
    // SAFETY: The klist owns the callback configuration.
    let mut put_callback = unsafe { (*klist).put };

    // SAFETY: The klist lock serializes list and dead-bit state.
    unsafe {
        lock(klist);
        if kill {
            kill_node(node);
        }
        if !dec_and_del(node) {
            put_callback = None;
        }
        unlock(klist);
    }

    if let Some(put_callback) = put_callback {
        // SAFETY: The callback contract belongs to the klist owner. It runs
        // only after the final reference and after the list lock is released.
        unsafe { put_callback(node) };
    }
}

/// Moves an iterator by one node.
///
/// `forward` selects `next`; otherwise the iterator moves backwards.
///
/// # Safety
///
/// `iter` must be an initialized, live klist iterator.
unsafe fn iter_move(iter: *mut Iter, forward: bool) -> *mut Node {
    // SAFETY: The caller guarantees a live iterator.
    let klist = unsafe { (*iter).i_klist };
    // SAFETY: The iterator belongs to an initialized klist.
    let mut put_callback = unsafe { (*klist).put };
    // SAFETY: The iterator field is valid.
    let last = unsafe { (*iter).i_cur };
    let mut next;

    let mut flags = 0;
    // SAFETY: The caller guarantees a live iterator and initialized klist.
    unsafe { lock_irqsave(klist, &mut flags) };

    if last.is_null() {
        // SAFETY: The klist sentinel is valid and the iterator starts at the
        // head when no current node is stored.
        next = unsafe {
            if forward {
                node_from_list((*klist).k_list.next)
            } else {
                node_from_list((*klist).k_list.prev)
            }
        };
    } else {
        // SAFETY: The current node is linked in this klist.
        next = unsafe {
            if forward {
                node_from_list((*last).n_node.next)
            } else {
                node_from_list((*last).n_node.prev)
            }
        };
        // SAFETY: The iterator owns a reference to the current node.
        if !unsafe { dec_and_del(last) } {
            put_callback = None;
        }
    }

    // SAFETY: The iterator is writable.
    unsafe { (*iter).i_cur = ptr::null_mut() };
    // SAFETY: The sentinel remains valid while the klist lock is held.
    let sentinel = unsafe { list_sentinel(klist) };
    while next != sentinel {
        // SAFETY: Every node encountered while traversing the locked list is
        // valid. Dead nodes remain linked until their final iterator release.
        if !unsafe { node_dead(next) } {
            // SAFETY: The klist lock protects the node reference count.
            unsafe { refcount_inc(next) };
            // SAFETY: The iterator field is writable.
            unsafe { (*iter).i_cur = next };
            break;
        }
        // SAFETY: The next link belongs to the current linked node.
        next = unsafe {
            if forward {
                node_from_list((*next).n_node.next)
            } else {
                node_from_list((*next).n_node.prev)
            }
        };
    }

    // SAFETY: Restore the matching interrupt state after all list accesses.
    unsafe { unlock_irqrestore(klist, flags) };

    if !last.is_null() {
        if let Some(put_callback) = put_callback {
            // SAFETY: The callback contract permits the previous node to be
            // discarded after its final iteration reference is gone.
            unsafe { put_callback(last) };
        }
    }

    // SAFETY: The current node was updated while the list lock was held.
    unsafe { (*iter).i_cur }
}

/// Implements `klist_add_head()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_add_head(node: *mut Node, klist: *mut KList) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { init_node(klist, node) };
    // SAFETY: The list sentinel is a valid insertion position.
    unsafe { add_after(klist, node, ptr::addr_of_mut!((*klist).k_list)) };
}

/// Implements `klist_add_tail()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_add_tail(node: *mut Node, klist: *mut KList) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { init_node(klist, node) };
    // SAFETY: The list sentinel is a valid insertion position.
    unsafe { add_before(klist, node, ptr::addr_of_mut!((*klist).k_list)) };
}

/// Implements `klist_add_behind()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_add_behind(node: *mut Node, position: *mut Node) {
    // SAFETY: Guaranteed by the public contract.
    let klist = unsafe { node_klist(position) };
    // SAFETY: Guaranteed by the public contract.
    unsafe { init_node(klist, node) };
    // SAFETY: The position link is in the owning list.
    unsafe { add_after(klist, node, node_list(position)) };
}

/// Implements `klist_add_before()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_add_before(node: *mut Node, position: *mut Node) {
    // SAFETY: Guaranteed by the public contract.
    let klist = unsafe { node_klist(position) };
    // SAFETY: Guaranteed by the public contract.
    unsafe { init_node(klist, node) };
    // SAFETY: The position link is in the owning list.
    unsafe { add_before(klist, node, node_list(position)) };
}

/// Implements `klist_del()`.
///
/// # Safety
///
/// The argument must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_del(node: *mut Node) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { put(node, true) };
}

/// Implements `klist_node_attached()`.
///
/// # Safety
///
/// `node` must point to a valid initialized klist node.
#[export]
unsafe extern "C" fn klist_node_attached(node: *mut Node) -> c_int {
    // SAFETY: The caller guarantees a valid node.
    i32::from(!unsafe { (*node).n_klist }.is_null())
}

/// Implements `klist_iter_init()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_iter_init(klist: *mut KList, iter: *mut Iter) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { klist_iter_init_node(klist, iter, ptr::null_mut()) };
}

/// Implements `klist_iter_init_node()`.
///
/// # Safety
///
/// The arguments must satisfy the public klist contract.
#[export]
unsafe extern "C" fn klist_iter_init_node(klist: *mut KList, iter: *mut Iter, node: *mut Node) {
    // SAFETY: The iterator is writable.
    unsafe {
        (*iter).i_klist = klist;
        (*iter).i_cur = ptr::null_mut();
    }
    if !node.is_null() {
        // SAFETY: The caller guarantees a valid node allocation.
        if unsafe { refcount_inc_not_zero(node) } {
            // SAFETY: The iterator is writable.
            unsafe { (*iter).i_cur = node };
        }
    }
}

/// Implements `klist_iter_exit()`.
///
/// # Safety
///
/// `iter` must be a live klist iterator.
#[export]
unsafe extern "C" fn klist_iter_exit(iter: *mut Iter) {
    // SAFETY: The caller guarantees a live iterator.
    let current = unsafe { (*iter).i_cur };
    if current.is_null() {
        return;
    }
    // SAFETY: A live iterator owns a reference to its current node.
    unsafe { put(current, false) };
    // SAFETY: The iterator is writable.
    unsafe { (*iter).i_cur = ptr::null_mut() };
}

/// Implements `klist_prev()`.
///
/// # Safety
///
/// `iter` must be a live klist iterator.
#[export]
unsafe extern "C" fn klist_prev(iter: *mut Iter) -> *mut Node {
    // SAFETY: Guaranteed by the public contract.
    unsafe { iter_move(iter, false) }
}

/// Implements `klist_next()`.
///
/// # Safety
///
/// `iter` must be a live klist iterator.
#[export]
unsafe extern "C" fn klist_next(iter: *mut Iter) -> *mut Node {
    // SAFETY: Guaranteed by the public contract.
    unsafe { iter_move(iter, true) }
}
