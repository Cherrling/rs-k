// SPDX-License-Identifier: GPL-2.0-only

//! Native lock-less list entry points.
//!
//! Rust defines the production symbols for first-node deletion, conditional
//! first-node deletion, and detached-chain reversal. The intrusive layouts and
//! inline producer operations remain C boundaries.

use crate::bindings;
use crate::sync::atomic::{Acquire, Atomic, Full, Relaxed};
use macros::export;

type Head = bindings::llist_head;
type Node = bindings::llist_node;

/// Returns the atomic view of the list head.
///
/// # Safety
///
/// `head` must remain valid for the returned reference. All other accesses to
/// the first field must use the same atomic access semantics.
unsafe fn head_atomic<'a>(head: *mut Head) -> &'a Atomic<*mut Node> {
    // SAFETY: The caller guarantees a live list head. The field is aligned,
    // and all list operations access it atomically.
    unsafe { Atomic::from_ptr(&raw mut (*head).first) }
}

/// Returns the atomic view of a node's successor pointer.
///
/// # Safety
///
/// `node` must remain valid for the returned reference. All other accesses to
/// the successor field must use the same atomic access semantics.
unsafe fn next_atomic<'a>(node: *mut Node) -> &'a Atomic<*mut Node> {
    // SAFETY: The caller guarantees a live node. The field is aligned, and all
    // list operations access it atomically.
    unsafe { Atomic::from_ptr(&raw mut (*node).next) }
}

/// Deletes and returns the first node, or null when the list is empty.
///
/// # Safety
///
/// `head` must remain valid. The caller must serialize other first-node
/// consumers as required by the lock-less list contract.
unsafe fn del_first(head: *mut Head) -> *mut Node {
    // SAFETY: The caller guarantees a live list head.
    let first = unsafe { head_atomic(head) };
    let mut entry = first.load(Acquire);

    loop {
        if entry.is_null() {
            return entry;
        }

        // SAFETY: A non-null head entry points to a live list node.
        let next = unsafe { next_atomic(entry) }.load(Relaxed);

        match first.cmpxchg(entry, next, Full) {
            Ok(_) => return entry,
            Err(actual) => entry = actual,
        }
    }
}

/// Deletes `this` when it is still the first node.
///
/// # Safety
///
/// `head` and `this` must remain valid. Callers that provide different
/// `this` nodes may run concurrently with producers.
unsafe fn del_first_this(head: *mut Head, this: *mut Node) -> bool {
    // SAFETY: The caller guarantees a live list head.
    let first = unsafe { head_atomic(head) };
    let mut entry = first.load(Acquire);

    loop {
        if entry != this {
            return false;
        }

        // SAFETY: `entry` is the caller-provided live node.
        let next = unsafe { next_atomic(entry) }.load(Relaxed);

        match first.cmpxchg(entry, next, Full) {
            Ok(_) => return true,
            Err(actual) => entry = actual,
        }
    }
}

/// Reverses a detached chain and returns its new first node.
///
/// # Safety
///
/// All nodes in the chain must be valid and must no longer be reachable by
/// lock-less list producers.
unsafe fn reverse_order(mut head: *mut Node) -> *mut Node {
    let mut new_head = core::ptr::null_mut();

    while !head.is_null() {
        // SAFETY: The detached chain contains only live nodes.
        let next = unsafe { next_atomic(head) }.load(Relaxed);

        // SAFETY: The detached node is writable.
        unsafe { next_atomic(head) }.store(new_head, Relaxed);
        new_head = head;
        head = next;
    }

    new_head
}

/// Deletes and returns the first lock-less list node.
///
/// # Safety
///
/// `head` must remain valid. The caller must follow the lock-less list
/// consumer serialization rules.
#[export]
unsafe extern "C" fn llist_del_first(head: *mut Head) -> *mut Node {
    // SAFETY: The caller provides the public `llist_del_first()` contract.
    unsafe { del_first(head) }
}

/// Deletes a given node when it is the first lock-less list node.
///
/// # Safety
///
/// `head` and `this` must remain valid. The caller must follow the lock-less
/// list consumer contract for `this`.
#[export]
unsafe extern "C" fn llist_del_first_this(head: *mut Head, this: *mut Node) -> bool {
    // SAFETY: The caller provides the public `llist_del_first_this()` contract.
    unsafe { del_first_this(head, this) }
}

/// Reverses a detached lock-less list chain.
///
/// # Safety
///
/// Every node in `head` must be valid and detached from concurrent producers.
#[export]
unsafe extern "C" fn llist_reverse_order(head: *mut Node) -> *mut Node {
    // SAFETY: The caller provides the public `llist_reverse_order()` contract.
    unsafe { reverse_order(head) }
}
