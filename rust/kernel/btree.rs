// SPDX-License-Identifier: GPL-2.0

//! Native B+Tree traversal and leaf updates.
//!
//! Rust owns key storage access, key comparison, key copying, predecessor
//! underflow, parent descent, predecessor retries, terminal entry selection,
//! and leaf value publication. C retains the structure layouts, allocation,
//! and public ABI adapters.

use crate::bindings;
use crate::ffi::{c_int, c_ulong};
use core::ffi::c_void;
use macros::export;

type Btree = bindings::btree_head;
type Geo = bindings::btree_geo;
type Key = [c_ulong; MAX_KEYLEN];

const MAX_KEYLEN: usize = 128 / (core::mem::size_of::<c_ulong>() * 8);
const MAX_TREE_HEIGHT: usize = core::mem::size_of::<usize>() * 8;

#[derive(Clone, Copy)]
struct PathEntry {
    node: *mut c_ulong,
    pos: usize,
}

/// Returns the number of machine words in one key.
#[inline(always)]
fn keylen(geo: &Geo) -> usize {
    let len = geo.keylen;
    debug_assert!(len > 0 && len as usize <= MAX_KEYLEN);
    len as usize
}

/// Returns the number of key-value pairs in one node.
#[inline(always)]
fn no_pairs(geo: &Geo) -> usize {
    let pairs = geo.no_pairs;
    debug_assert!(pairs > 0);
    pairs as usize
}

/// Returns the first value index in one node.
#[inline(always)]
fn no_longs(geo: &Geo) -> usize {
    let index = geo.no_longs;
    debug_assert!(index > 0);
    index as usize
}

/// Returns the key at `pos` in `node`.
///
/// # Safety
///
/// `geo` must be valid, and `pos` must identify an in-range node pair.
#[inline(always)]
unsafe fn node_key(geo: &Geo, node: *const c_ulong, pos: usize) -> *const c_ulong {
    let stride = keylen(geo);
    // SAFETY: The caller guarantees that the key is in the node.
    unsafe { node.add(pos * stride) }
}

/// Returns the value at `pos` in `node`.
///
/// # Safety
///
/// `geo` must be valid, and `pos` must identify an in-range node pair.
#[inline(always)]
unsafe fn node_value(geo: &Geo, node: *const c_ulong, pos: usize) -> *mut c_void {
    let index = no_longs(geo) + pos;
    // SAFETY: The caller guarantees that the value slot is in the node.
    unsafe { (*node.add(index)) as *mut c_void }
}

/// Replaces the value at `pos` in `node`.
///
/// # Safety
///
/// `geo` must be valid, `node` must be writable, `pos` must identify an
/// in-range node pair, and the caller must serialize writers.
#[inline(always)]
unsafe fn set_node_value(geo: &Geo, node: *mut c_ulong, pos: usize, val: *mut c_void) {
    let index = no_longs(geo) + pos;
    // SAFETY: The caller guarantees that the value slot is writable.
    unsafe { node.add(index).write(val as c_ulong) }
}

/// Compares a node key with a search key.
///
/// # Safety
///
/// `geo` and `key` must be valid, and `pos` must identify an in-range node
/// pair.
#[inline(always)]
unsafe fn key_compare(geo: &Geo, node: *const c_ulong, pos: usize, key: *const c_ulong) -> c_int {
    let len = keylen(geo);
    // SAFETY: Guaranteed by this function's contract.
    let node_key = unsafe { node_key(geo, node, pos) };
    for i in 0..len {
        // SAFETY: Guaranteed by this function's contract.
        let node_word = unsafe { node_key.add(i).read() };
        // SAFETY: Guaranteed by this function's contract.
        let key_word = unsafe { key.add(i).read() };
        if node_word < key_word {
            return -1;
        }
        if node_word > key_word {
            return 1;
        }
    }
    0
}

/// Copies `count` key words.
///
/// # Safety
///
/// Both key buffers must have room for `count` words. They must not overlap.
#[inline(always)]
unsafe fn copy_key(dst: *mut c_ulong, src: *const c_ulong, count: usize) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { core::ptr::copy_nonoverlapping(src, dst, count) }
}

/// Reports whether every word in a key is zero.
///
/// # Safety
///
/// `geo` and `key` must be valid.
#[inline(always)]
unsafe fn key_is_zero(geo: &Geo, key: *const c_ulong) -> bool {
    for i in 0..keylen(geo) {
        // SAFETY: Guaranteed by this function's contract.
        if unsafe { key.add(i).read() } != 0 {
            return false;
        }
    }
    true
}

/// Decrements a multiword key with unsigned underflow propagation.
///
/// # Safety
///
/// `geo` and `key` must be valid, and `key` must be writable.
#[inline(always)]
unsafe fn decrement_key(geo: &Geo, key: *mut c_ulong) {
    for i in (0..keylen(geo)).rev() {
        // SAFETY: Guaranteed by this function's contract.
        let val = unsafe { key.add(i).read() };
        // SAFETY: Guaranteed by this function's contract.
        unsafe { key.add(i).write(val.wrapping_sub(1)) };
        if val != 0 {
            break;
        }
    }
}

/// Finds the leaf node that can contain `key`.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid. Readers must exclude concurrent
/// tree mutation.
unsafe fn lookup_node(head: *mut Btree, geo: &Geo, key: *const c_ulong) -> *mut c_ulong {
    // SAFETY: Guaranteed by this function's contract.
    let mut height = unsafe { (*head).height };
    if height == 0 {
        return core::ptr::null_mut();
    }

    // SAFETY: A non-empty tree has a valid root node.
    let mut node = unsafe { (*head).node };
    let pairs = no_pairs(geo);

    while height > 1 {
        let mut pos = 0;
        while pos < pairs {
            // SAFETY: `pos` is in range, and all inputs are valid.
            if unsafe { key_compare(geo, node, pos, key) } <= 0 {
                break;
            }
            pos += 1;
        }
        if pos == pairs {
            return core::ptr::null_mut();
        }

        // SAFETY: `pos` identifies an in-range child pointer.
        node = unsafe { node_value(geo, node, pos) }.cast();
        if node.is_null() {
            return core::ptr::null_mut();
        }
        height -= 1;
    }

    node
}

/// Looks up a key through the native B+Tree traversal.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid. Readers must exclude concurrent
/// tree mutation.
unsafe fn lookup(head: *mut Btree, geo: &Geo, key: *const c_ulong) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let leaf = unsafe { lookup_node(head, geo, key) };
    if leaf.is_null() {
        return core::ptr::null_mut();
    }

    let pairs = no_pairs(geo);
    for pos in 0..pairs {
        // SAFETY: `pos` is in range, and all inputs are valid.
        let cmp = unsafe { key_compare(geo, leaf, pos, key) };
        if cmp == 0 {
            // SAFETY: `pos` identifies an in-range value.
            return unsafe { node_value(geo, leaf, pos) };
        }
    }

    core::ptr::null_mut()
}

/// Updates the value for an existing key.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid. The caller must serialize writers.
unsafe fn update(head: *mut Btree, geo: &Geo, key: *const c_ulong, val: *mut c_void) -> c_int {
    // SAFETY: Guaranteed by this function's contract.
    let leaf = unsafe { lookup_node(head, geo, key) };
    if leaf.is_null() {
        return -(bindings::ENOENT as c_int);
    }

    let pairs = no_pairs(geo);
    for pos in 0..pairs {
        // SAFETY: `pos` is in range, and all inputs are valid.
        let cmp = unsafe { key_compare(geo, leaf, pos, key) };
        if cmp == 0 {
            // SAFETY: The caller serializes writers.
            unsafe { set_node_value(geo, leaf, pos, val) };
            return 0;
        }
    }

    -(bindings::ENOENT as c_int)
}

/// Returns the greatest entry in the tree.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid and writable. Readers must exclude
/// concurrent tree mutation.
unsafe fn last(head: *mut Btree, geo: &Geo, key: *mut c_ulong) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    let mut height = unsafe { (*head).height };
    if height == 0 {
        return core::ptr::null_mut();
    }

    // SAFETY: A non-empty tree has a valid root node.
    let mut node = unsafe { (*head).node };
    while height > 1 {
        // SAFETY: The first pair contains the maximum child pointer.
        node = unsafe { node_value(geo, node, 0) }.cast();
        height -= 1;
    }

    let len = keylen(geo);
    // SAFETY: `node` is a valid leaf, and caller key has `len` words.
    unsafe { copy_key(key, node_key(geo, node, 0), len) };
    // SAFETY: The first pair identifies the greatest leaf entry.
    unsafe { node_value(geo, node, 0) }
}

/// Returns the greatest entry in a non-empty subtree.
///
/// # Safety
///
/// `node` must be a valid subtree root at `height`, and `key` must have room
/// for the returned key. Readers must exclude concurrent tree mutation.
unsafe fn last_in_subtree(
    geo: &Geo,
    mut node: *mut c_ulong,
    mut height: c_int,
    key: *mut c_ulong,
) -> *mut c_void {
    while height > 1 {
        // SAFETY: The first pair contains the subtree's greatest child.
        node = unsafe { node_value(geo, node, 0) }.cast();
        if node.is_null() {
            return core::ptr::null_mut();
        }
        height -= 1;
    }

    let len = keylen(geo);
    // SAFETY: The first pair contains the subtree's greatest entry.
    unsafe { copy_key(key, node_key(geo, node, 0), len) };
    // SAFETY: The first pair contains the subtree's greatest entry.
    unsafe { node_value(geo, node, 0) }
}

/// Returns the entry immediately before a search key.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid and writable. Readers must exclude
/// concurrent tree mutation.
unsafe fn get_prev(head: *mut Btree, geo: &Geo, key: *mut c_ulong) -> *mut c_void {
    // SAFETY: Guaranteed by this function's contract.
    if unsafe { key_is_zero(geo, key) } {
        return core::ptr::null_mut();
    }
    // SAFETY: Guaranteed by this function's contract.
    let tree_height = unsafe { (*head).height };
    if tree_height == 0 {
        return core::ptr::null_mut();
    }

    let len = keylen(geo);
    let pairs = no_pairs(geo);
    let mut local: Key = [0; MAX_KEYLEN];
    // SAFETY: `local` has room for every supported key.
    unsafe { copy_key(local.as_mut_ptr(), key, len) };
    // SAFETY: The local key is writable and has a valid geometry.
    unsafe { decrement_key(geo, local.as_mut_ptr()) };

    let mut path = [PathEntry {
        node: core::ptr::null_mut(),
        pos: 0,
    }; MAX_TREE_HEIGHT];
    let mut depth = 0;
    // SAFETY: A non-empty tree has a valid root node.
    let mut node = unsafe { (*head).node };
    let mut height = tree_height;

    while height > 1 {
        let mut pos = 0;
        while pos < pairs {
            // SAFETY: `pos` is in range, and all inputs are valid.
            if unsafe { key_compare(geo, node, pos, local.as_ptr()) } <= 0 {
                break;
            }
            pos += 1;
        }
        if pos == pairs {
            break;
        }

        if depth == MAX_TREE_HEIGHT {
            break;
        }
        path[depth] = PathEntry { node, pos };
        depth += 1;

        // SAFETY: `pos` identifies an in-range child pointer.
        node = unsafe { node_value(geo, node, pos) }.cast();
        if node.is_null() {
            break;
        }
        height -= 1;
    }

    if height == 1 && !node.is_null() {
        for pos in 0..pairs {
            // SAFETY: `pos` is in range, and all inputs are valid.
            if unsafe { key_compare(geo, node, pos, local.as_ptr()) } <= 0 {
                // SAFETY: `pos` identifies an in-range leaf value.
                let val = unsafe { node_value(geo, node, pos) };
                if !val.is_null() {
                    // SAFETY: The caller key has room for the result.
                    unsafe { copy_key(key, node_key(geo, node, pos), len) };
                    return val;
                }
                break;
            }
        }
    }

    while depth > 0 {
        depth -= 1;
        let parent = path[depth];
        let child_height = tree_height - depth as c_int - 1;
        for pos in parent.pos + 1..pairs {
            // SAFETY: `pos` identifies an in-range sibling pointer.
            let sibling = unsafe { node_value(geo, parent.node, pos) }.cast::<c_ulong>();
            if !sibling.is_null() {
                // SAFETY: The sibling is a valid subtree at `child_height`.
                return unsafe { last_in_subtree(geo, sibling, child_height, key) };
            }
        }
    }

    core::ptr::null_mut()
}

/// Implements `btree_lookup()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_lookup(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
) -> *mut c_void {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { lookup(head, geo, key) }
}

/// Implements `btree_update()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_update(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
    val: *mut c_void,
) -> c_int {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { update(head, geo, key, val) }
}

/// Implements `btree_last()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_last(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
) -> *mut c_void {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { last(head, geo, key) }
}

/// Implements `btree_get_prev()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_get_prev(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
) -> *mut c_void {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { get_prev(head, geo, key) }
}
