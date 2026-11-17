// SPDX-License-Identifier: GPL-2.0

//! Native priority-sorted intrusive list operations.
//!
//! Rust owns priority placement, same-priority FIFO ordering, bucket-list
//! maintenance, removal promotion, and requeue decisions. C retains the
//! intrusive structure layouts, raw list primitives, public ABI adapters, and
//! debug validation.

use crate::{bindings, container_of};
use core::ptr;
use macros::export;

type List = bindings::list_head;
type PList = bindings::plist_head;
type Node = bindings::plist_node;

/// Returns whether a linked-list ring is empty.
///
/// # Safety
///
/// `list` must point to an initialized list member.
#[inline]
unsafe fn list_empty(list: *const List) -> bool {
    // SAFETY: The caller guarantees an initialized list member.
    unsafe { bindings::rust_plist_list_empty(list) }
}

/// Inserts `entry` immediately after `head`.
///
/// # Safety
///
/// `entry` must be isolated and `head` must identify a valid insertion point.
#[inline]
unsafe fn list_add(entry: *mut List, head: *mut List) {
    // SAFETY: The caller guarantees valid intrusive list members.
    unsafe { bindings::rust_plist_list_add(entry, head) }
}

/// Inserts `entry` immediately before `head`.
///
/// # Safety
///
/// `entry` must be isolated and `head` must identify a valid insertion point.
#[inline]
unsafe fn list_add_tail(entry: *mut List, head: *mut List) {
    // SAFETY: The caller guarantees valid intrusive list members.
    unsafe { bindings::rust_plist_list_add_tail(entry, head) }
}

/// Removes `entry` and restores it to the isolated state.
///
/// # Safety
///
/// `entry` must be linked in a valid list.
#[inline]
unsafe fn list_del_init(entry: *mut List) {
    // SAFETY: The caller guarantees that `entry` is linked.
    unsafe { bindings::rust_plist_list_del_init(entry) }
}

/// Returns the serialized node-list member of `node`.
///
/// # Safety
///
/// `node` must point to a valid priority-list node.
#[inline]
unsafe fn node_list(node: *mut Node) -> *mut List {
    // SAFETY: The caller guarantees a valid node.
    unsafe { ptr::addr_of_mut!((*node).node_list) }
}

/// Returns the priority-bucket member of `node`.
///
/// # Safety
///
/// `node` must point to a valid priority-list node.
#[inline]
unsafe fn prio_list(node: *mut Node) -> *mut List {
    // SAFETY: The caller guarantees a valid node.
    unsafe { ptr::addr_of_mut!((*node).prio_list) }
}

/// Returns the node that contains a serialized node-list member.
///
/// # Safety
///
/// `list` must point to the `node_list` member of a priority-list node.
#[inline]
unsafe fn node_from_node_list(list: *mut List) -> *mut Node {
    // SAFETY: The caller guarantees that `list` is embedded in a node.
    unsafe { container_of!(list, Node, node_list) }
}

/// Returns the node that contains a priority-bucket member.
///
/// # Safety
///
/// `list` must point to the `prio_list` member of a priority-list node.
#[inline]
unsafe fn node_from_prio_list(list: *mut List) -> *mut Node {
    // SAFETY: The caller guarantees that `list` is embedded in a node.
    unsafe { container_of!(list, Node, prio_list) }
}

/// Adds `node` at its sorted position.
///
/// # Safety
///
/// `head` must be initialized, `node` must be isolated, and the caller must
/// serialize all access to both objects.
unsafe fn add(node: *mut Node, head: *mut PList) {
    // SAFETY: The caller guarantees valid objects.
    let head_nodes = unsafe { ptr::addr_of_mut!((*head).node_list) };
    // SAFETY: The caller guarantees a valid node.
    let nodes = unsafe { node_list(node) };
    // SAFETY: The caller guarantees a valid node.
    let prios = unsafe { prio_list(node) };

    // SAFETY: The head is initialized.
    if unsafe { list_empty(head_nodes) } {
        // SAFETY: An empty list accepts an isolated node.
        unsafe { list_add_tail(nodes, head_nodes) };
        return;
    }

    // SAFETY: A non-empty serialized list starts with a valid node.
    let first = unsafe { node_from_node_list((*head_nodes).next) };
    let mut bucket = first;
    // SAFETY: The first serialized node is also the first priority bucket.
    let mut next_bucket = unsafe { node_from_prio_list((*prio_list(bucket)).next) };

    // SAFETY: The caller guarantees valid bucket and node objects.
    while unsafe { (*bucket).prio < (*node).prio } && next_bucket != first {
        bucket = next_bucket;
        // SAFETY: Every priority bucket has a valid next ring member.
        next_bucket = unsafe { node_from_prio_list((*prio_list(bucket)).next) };
    }

    // SAFETY: The bucket ring identifies the neighboring priorities.
    let bucket_prio = unsafe { (*bucket).prio };
    // SAFETY: The caller guarantees a valid node.
    let node_prio = unsafe { (*node).prio };

    if bucket_prio == node_prio {
        // The last bucket wraps to the first bucket in a headless ring, but
        // its serialized entries still end at the list sentinel.
        let insertion = if next_bucket == first {
            head_nodes
        } else {
            // SAFETY: A different next bucket is a valid node.
            unsafe { node_list(next_bucket) }
        };
        // SAFETY: Insert after every node in the matching priority bucket.
        unsafe { list_add_tail(nodes, insertion) };
    } else if bucket_prio < node_prio {
        // All buckets have a higher priority. The node forms the new last
        // bucket, and its serialized entry belongs at the list tail.
        // SAFETY: `first` is a valid bucket and `node` is isolated.
        unsafe {
            list_add_tail(prios, prio_list(first));
            list_add_tail(nodes, head_nodes);
        }
    } else {
        // The node forms a new bucket immediately before `bucket`.
        // SAFETY: `bucket` and `node` identify valid insertion points.
        unsafe {
            list_add_tail(prios, prio_list(bucket));
            list_add_tail(nodes, node_list(bucket));
        }
    }
}

/// Removes `node` and repairs its priority bucket.
///
/// # Safety
///
/// `head` must be initialized, `node` must be linked to it, and the caller
/// must serialize all access to both objects.
unsafe fn remove(node: *mut Node, head: *mut PList) {
    // SAFETY: The caller guarantees valid objects.
    let head_nodes = unsafe { ptr::addr_of_mut!((*head).node_list) };
    // SAFETY: The caller guarantees a valid node.
    let nodes = unsafe { node_list(node) };
    // SAFETY: The caller guarantees a valid node.
    let prios = unsafe { prio_list(node) };

    // SAFETY: Every embedded list member is initialized.
    if unsafe { !list_empty(prios) } {
        // SAFETY: A linked node has a valid next serialized member.
        if unsafe { (*nodes).next != head_nodes } {
            // SAFETY: The next serialized member belongs to a valid node.
            let next = unsafe { node_from_node_list((*nodes).next) };
            // Promote the next node when it has the same priority.
            // SAFETY: Its priority list is initialized.
            if unsafe { list_empty(prio_list(next)) } {
                // SAFETY: `next` is isolated in the bucket ring, and the
                // current node is the preceding bucket head.
                unsafe { list_add(prio_list(next), prios) };
            }
        }
        // SAFETY: The caller guarantees that the node is linked.
        unsafe { list_del_init(prios) };
    }

    // SAFETY: The caller guarantees that the node is linked.
    unsafe { list_del_init(nodes) };
}

/// Moves `node` to the end of its same-priority entries.
///
/// # Safety
///
/// `head` must be initialized and non-empty, `node` must be linked to it, and
/// the caller must serialize all access to both objects.
unsafe fn requeue(node: *mut Node, head: *mut PList) {
    // SAFETY: The caller guarantees valid objects.
    let head_nodes = unsafe { ptr::addr_of_mut!((*head).node_list) };
    // SAFETY: The caller guarantees a valid node.
    let nodes = unsafe { node_list(node) };

    // SAFETY: The linked node has a valid next member.
    if unsafe { (*nodes).next == head_nodes } {
        return;
    }

    // SAFETY: The next member belongs to a valid node.
    let next = unsafe { node_from_node_list((*nodes).next) };
    // SAFETY: Both nodes are valid.
    if unsafe { (*next).prio != (*node).prio } {
        return;
    }

    // Removing and reinserting preserves the bucket representative and moves
    // the node behind every existing entry with the same priority.
    // SAFETY: The caller guarantees valid linked and isolated states.
    unsafe {
        remove(node, head);
        add(node, head);
    }
}

/// Implements `plist_add()`.
///
/// # Safety
///
/// The arguments must satisfy the public priority-list contract.
#[export]
unsafe extern "C" fn rust_plist_add(node: *mut Node, head: *mut PList) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { add(node, head) };
}

/// Implements `plist_del()`.
///
/// # Safety
///
/// The arguments must satisfy the public priority-list contract.
#[export]
unsafe extern "C" fn rust_plist_del(node: *mut Node, head: *mut PList) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { remove(node, head) };
}

/// Implements `plist_requeue()`.
///
/// # Safety
///
/// The arguments must satisfy the public priority-list contract.
#[export]
unsafe extern "C" fn rust_plist_requeue(node: *mut Node, head: *mut PList) {
    // SAFETY: Guaranteed by the public contract.
    unsafe { requeue(node, head) };
}
