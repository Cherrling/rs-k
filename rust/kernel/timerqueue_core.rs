// SPDX-License-Identifier: GPL-2.0

//! Native timer queue insertion, deletion, and traversal.
//!
//! Rust owns the ordered search, node linking, cached-leftmost maintenance,
//! successor lookup, and linked-tree insertion. Callers retain the locking
//! that serializes each timer queue.

use crate::bindings;
use core::{
    ptr,
    sync::atomic::{AtomicBool, Ordering},
};
use macros::export;

type RbNode = bindings::rb_node;
type RbNodeLinked = bindings::rb_node_linked;
type TimerHead = bindings::timerqueue_head;
type TimerNode = bindings::timerqueue_node;
type TimerLinkedHead = bindings::timerqueue_linked_head;
type TimerLinkedNode = bindings::timerqueue_linked_node;

static ADD_WARNED: AtomicBool = AtomicBool::new(false);
static DEL_WARNED: AtomicBool = AtomicBool::new(false);

/// Reports an invalid node state once at this call site.
#[inline]
fn warn_once(condition: bool, warned: &AtomicBool) {
    if condition && !warned.swap(true, Ordering::Relaxed) {
        let _ = crate::warn_on!(true);
    }
}

/// Returns whether an intrusive red-black node is initialized but not linked.
///
/// # Safety
///
/// `node` must point to a valid `rb_node`.
#[inline]
unsafe fn node_is_empty(node: *const RbNode) -> bool {
    // SAFETY: The caller guarantees that node is valid.
    unsafe { (*node).__rb_parent_color == node as usize }
}

/// Returns the timer queue entry that owns an intrusive node.
///
/// # Safety
///
/// `node` must point to the `node` field of a live timer queue entry.
#[inline]
unsafe fn timer_node_from_rb(node: *mut RbNode) -> *mut TimerNode {
    // SAFETY: The caller guarantees the containing timer queue entry is live.
    unsafe { crate::container_of!(node, TimerNode, node) }
}

/// Returns the linked timer queue entry that owns an intrusive node.
///
/// # Safety
///
/// `node` must point to the inner `node` field of a live linked timer queue
/// entry.
#[inline]
unsafe fn timer_linked_node_from_rb(node: *mut RbNode) -> *mut TimerLinkedNode {
    // SAFETY: The caller guarantees the containing linked timer queue entry is live.
    unsafe { crate::container_of!(node, TimerLinkedNode, node.node) }
}

/// Returns the linked red-black node that owns an intrusive node.
///
/// # Safety
///
/// `node` must point to the inner `node` field of a live linked red-black
/// node.
#[inline]
unsafe fn rb_linked_node_from_rb(node: *mut RbNode) -> *mut RbNodeLinked {
    // SAFETY: The caller guarantees the containing linked node is live.
    unsafe { crate::container_of!(node, RbNodeLinked, node) }
}

/// Adds a timer to a cached timer queue.
///
/// # Safety
///
/// `head` and `node` must point to live objects. The caller must serialize
/// access to the queue and initialize `node` with `timerqueue_init()`.
#[export]
unsafe extern "C" fn rust_timerqueue_add(head: *mut TimerHead, node: *mut TimerNode) -> bool {
    // SAFETY: The caller guarantees that node is live.
    let node_rb = unsafe { ptr::addr_of_mut!((*node).node) };
    // SAFETY: The caller guarantees that node is initialized.
    warn_once(!unsafe { node_is_empty(node_rb) }, &ADD_WARNED);

    // SAFETY: The caller guarantees that head is live.
    let root = unsafe { ptr::addr_of_mut!((*head).rb_root) };
    // SAFETY: `rb_root` is the live root field of the cached tree.
    let mut link: *mut *mut RbNode = unsafe { ptr::addr_of_mut!((*root).rb_root.rb_node) };
    let mut parent = ptr::null_mut();
    let mut leftmost = true;

    // SAFETY: `link` points to the root or a child pointer of a live node.
    while !unsafe { *link }.is_null() {
        // SAFETY: The loop condition established that the link targets a live node.
        parent = unsafe { *link };
        // SAFETY: parent is a linked timer queue entry.
        let parent_entry = unsafe { timer_node_from_rb(parent) };
        // SAFETY: Both timer entries are live.
        if unsafe { (*node).expires < (*parent_entry).expires } {
            // SAFETY: parent is live.
            link = unsafe { ptr::addr_of_mut!((*parent).rb_left) };
        } else {
            leftmost = false;
            // SAFETY: parent is live.
            link = unsafe { ptr::addr_of_mut!((*parent).rb_right) };
        }
    }

    // SAFETY: The search found a valid insertion slot, and node is live.
    unsafe { bindings::rb_link_node(node_rb, parent, link) };
    if leftmost {
        // SAFETY: root and node are live, and node is the new leftmost entry.
        unsafe { (*root).rb_leftmost = node_rb };
    }
    // SAFETY: node is linked into root, and the caller excludes concurrent access.
    unsafe { bindings::rb_insert_color(node_rb, ptr::addr_of_mut!((*root).rb_root)) };

    leftmost
}

/// Removes a timer from a cached timer queue.
///
/// # Safety
///
/// `head` and `node` must point to live objects. The caller must serialize
/// access to the queue and `node` must currently be queued.
#[export]
unsafe extern "C" fn rust_timerqueue_del(head: *mut TimerHead, node: *mut TimerNode) -> bool {
    // SAFETY: The caller guarantees that node is live.
    let node_rb = unsafe { ptr::addr_of_mut!((*node).node) };
    // SAFETY: The caller guarantees that node is live.
    warn_once(unsafe { node_is_empty(node_rb) }, &DEL_WARNED);

    // SAFETY: The caller guarantees that head is live.
    let root = unsafe { ptr::addr_of_mut!((*head).rb_root) };
    // SAFETY: `root` is the live cached root supplied by the caller.
    if unsafe { (*root).rb_leftmost == node_rb } {
        // SAFETY: node is queued, so rb_next is valid and finds its successor.
        unsafe { (*root).rb_leftmost = bindings::rb_next(node_rb) };
    }
    // SAFETY: node is queued in root, and the caller excludes concurrent access.
    unsafe { bindings::rb_erase(node_rb, ptr::addr_of_mut!((*root).rb_root)) };
    // SAFETY: node is live and no longer linked after rb_erase.
    unsafe { (*node_rb).__rb_parent_color = node_rb as usize };

    // SAFETY: root is live and rb_erase completed.
    !unsafe { (*root).rb_root.rb_node }.is_null()
}

/// Returns the timer after `node` in sort order.
///
/// # Safety
///
/// `node` must be null or point to a queued timer queue entry. The caller must
/// serialize access to the queue.
#[export]
unsafe extern "C" fn rust_timerqueue_iterate_next(node: *mut TimerNode) -> *mut TimerNode {
    if node.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees that node is live.
    let node_rb = unsafe { ptr::addr_of_mut!((*node).node) };
    // SAFETY: node is queued, so rb_next can inspect it.
    let next = unsafe { bindings::rb_next(node_rb) };
    if next.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: rb_next returned a live timer queue entry.
    unsafe { timer_node_from_rb(next) }
}

/// Links a new node into a linked red-black tree.
///
/// # Safety
///
/// `new_linked` must be initialized and must not yet be linked. `parent` must
/// be null or live. `link` must point at the pointer used to reach `parent`.
#[inline]
unsafe fn link_linked_node(
    new_linked: *mut RbNodeLinked,
    parent: *mut RbNode,
    link: *mut *mut RbNode,
) {
    if parent.is_null() {
        return;
    }

    // SAFETY: parent is the inner node of a live linked node.
    let parent_linked = unsafe { rb_linked_node_from_rb(parent) };
    // SAFETY: parent is live.
    let parent_left = unsafe { ptr::addr_of_mut!((*parent).rb_left) };
    if link == parent_left {
        // SAFETY: All linked nodes are live and the caller excludes concurrent access.
        unsafe {
            (*new_linked).prev = (*parent_linked).prev;
            (*new_linked).next = parent_linked;
            (*parent_linked).prev = new_linked;
            if !(*new_linked).prev.is_null() {
                (*(*new_linked).prev).next = new_linked;
            }
        }
    } else {
        // SAFETY: All linked nodes are live and the caller excludes concurrent access.
        unsafe {
            (*new_linked).next = (*parent_linked).next;
            (*new_linked).prev = parent_linked;
            (*parent_linked).next = new_linked;
            if !(*new_linked).next.is_null() {
                (*(*new_linked).next).prev = new_linked;
            }
        }
    }
}

/// Adds a timer to a linked timer queue.
///
/// # Safety
///
/// `head` and `node` must point to live objects. The caller must serialize
/// access to the queue and initialize `node` with
/// `timerqueue_linked_init()`.
#[export]
unsafe extern "C" fn rust_timerqueue_linked_add(
    head: *mut TimerLinkedHead,
    node: *mut TimerLinkedNode,
) -> bool {
    // SAFETY: The caller guarantees that node is live.
    let node_linked = unsafe { ptr::addr_of_mut!((*node).node) };
    // SAFETY: The caller guarantees that node is live.
    let node_rb = unsafe { ptr::addr_of_mut!((*node_linked).node) };
    // SAFETY: The caller guarantees that head is live.
    let root = unsafe { ptr::addr_of_mut!((*head).rb_root) };
    // SAFETY: `rb_root` is the live root field of the linked tree.
    let mut link: *mut *mut RbNode = unsafe { ptr::addr_of_mut!((*root).rb_root.rb_node) };
    let mut parent = ptr::null_mut();

    // SAFETY: `link` points to the root or a child pointer of a live node.
    while !unsafe { *link }.is_null() {
        // SAFETY: The loop condition established that the link targets a live node.
        parent = unsafe { *link };
        // SAFETY: parent is the inner node of a linked timer queue entry.
        let parent_entry = unsafe { timer_linked_node_from_rb(parent) };
        // SAFETY: Both timer entries are live.
        if unsafe { (*node).expires < (*parent_entry).expires } {
            // SAFETY: parent is live.
            link = unsafe { ptr::addr_of_mut!((*parent).rb_left) };
        } else {
            // SAFETY: parent is live.
            link = unsafe { ptr::addr_of_mut!((*parent).rb_right) };
        }
    }

    // SAFETY: The search found a valid slot and node is initialized but unlinked.
    unsafe { link_linked_node(node_linked, parent, link) };
    // SAFETY: The search found a valid insertion slot, and node is live.
    unsafe { bindings::rb_link_node(node_rb, parent, link) };
    // SAFETY: node is linked into root, and the caller excludes concurrent access.
    unsafe { bindings::rb_insert_color(node_rb, ptr::addr_of_mut!((*root).rb_root)) };

    // SAFETY: linked chain insertion completed and node is live.
    let leftmost = unsafe { (*node_linked).prev.is_null() };
    if leftmost {
        // SAFETY: node is the new leftmost linked node.
        unsafe { (*root).rb_leftmost = node_linked };
    }
    leftmost
}
