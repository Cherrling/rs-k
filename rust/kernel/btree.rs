// SPDX-License-Identifier: GPL-2.0

//! Native B+Tree operations.
//!
//! Rust owns key storage access, key comparison, key copying, predecessor
//! underflow, parent descent, predecessor retries, terminal entry selection,
//! structural insertion, removal, merging, and leaf value publication. C
//! retains the structure layouts, node allocation primitives, and public ABI
//! adapters.

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

#[derive(Clone, Copy)]
struct MergeSpec {
    level: c_int,
    left: *mut c_ulong,
    left_fill: usize,
    right: *mut c_ulong,
    right_fill: usize,
    parent: *mut c_ulong,
    parent_pos: usize,
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

/// Returns the writable key at `pos` in `node`.
///
/// # Safety
///
/// `geo` must be valid, `node` must be writable, and `pos` must identify an
/// in-range node pair.
#[inline(always)]
unsafe fn node_key_mut(geo: &Geo, node: *mut c_ulong, pos: usize) -> *mut c_ulong {
    let stride = keylen(geo);
    // SAFETY: The caller guarantees that the key is in the node.
    unsafe { node.add(pos * stride) }
}

/// Replaces the key at `pos` in `node`.
///
/// # Safety
///
/// `geo` and `key` must be valid, `node` must be writable, `pos` must
/// identify an in-range node pair, and the caller must serialize writers.
#[inline(always)]
unsafe fn set_node_key(geo: &Geo, node: *mut c_ulong, pos: usize, key: *const c_ulong) {
    let len = keylen(geo);
    // SAFETY: The caller guarantees that both key buffers are valid.
    unsafe { core::ptr::copy(key, node_key_mut(geo, node, pos), len) }
}

/// Clears the key and value at `pos` in `node`.
///
/// # Safety
///
/// `geo` must be valid, `node` must be writable, `pos` must identify an
/// in-range node pair, and the caller must serialize writers.
#[inline(always)]
unsafe fn clear_pair(geo: &Geo, node: *mut c_ulong, pos: usize) {
    // SAFETY: The caller guarantees that the key and value are writable.
    unsafe {
        core::ptr::write_bytes(node_key_mut(geo, node, pos), 0, keylen(geo));
        set_node_value(geo, node, pos, core::ptr::null_mut());
    }
}

/// Copies one pair within or between nodes.
///
/// # Safety
///
/// `geo` must be valid, both pair indices must be in range, both nodes must be
/// writable, and the caller must serialize writers.
#[inline(always)]
unsafe fn copy_pair(
    geo: &Geo,
    dst: *mut c_ulong,
    dst_pos: usize,
    src: *mut c_ulong,
    src_pos: usize,
) {
    // SAFETY: The caller guarantees valid, writable nodes.
    unsafe {
        core::ptr::copy(
            node_key(geo, src, src_pos),
            node_key_mut(geo, dst, dst_pos),
            keylen(geo),
        );
        set_node_value(geo, dst, dst_pos, node_value(geo, src, src_pos));
    }
}

/// Returns the first empty pair at or after `start`.
///
/// # Safety
///
/// `geo` must be valid, `node` must be valid, and `start` must not exceed the
/// number of pair slots.
#[inline(always)]
unsafe fn first_empty(geo: &Geo, node: *mut c_ulong, start: usize) -> usize {
    let pairs = no_pairs(geo);
    for pos in start..pairs {
        // SAFETY: `pos` is in range, and `node` is valid.
        if unsafe { node_value(geo, node, pos) }.is_null() {
            return pos;
        }
    }
    pairs
}

/// Returns the first key less than or equal to `key`.
///
/// # Safety
///
/// `geo`, `node`, and `key` must be valid.
#[inline(always)]
unsafe fn find_pos(geo: &Geo, node: *mut c_ulong, key: *const c_ulong) -> usize {
    let pairs = no_pairs(geo);
    for pos in 0..pairs {
        // SAFETY: `pos` is in range, and all inputs are valid.
        if unsafe { key_compare(geo, node, pos, key) } <= 0 {
            return pos;
        }
    }
    pairs
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

/// Finds a node at `level` and repairs a missing rightmost ancestor key.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid, `level` must be in range, and the
/// caller must serialize writers.
unsafe fn find_level(
    head: *mut Btree,
    geo: &Geo,
    key: *const c_ulong,
    level: c_int,
) -> *mut c_ulong {
    // SAFETY: The public B+Tree contract guarantees a non-empty tree.
    let mut node = unsafe { (*head).node };
    // SAFETY: The public B+Tree contract guarantees a valid height.
    let mut height = unsafe { (*head).height };
    let pairs = no_pairs(geo);

    while height > level {
        // SAFETY: The inputs are valid and the caller serializes writers.
        let mut pos = unsafe { find_pos(geo, node, key) };
        let child = if pos < pairs {
            // SAFETY: The branch guarantees that `pos` is in range.
            unsafe { node_value(geo, node, pos) }
        } else {
            core::ptr::null_mut()
        };

        if pos == pairs || child.is_null() {
            if pos == 0 {
                panic!("invalid empty B+Tree traversal node");
            }
            pos -= 1;
            // SAFETY: The caller serializes writers and the pair is in range.
            unsafe { set_node_key(geo, node, pos, key) };
        }

        // SAFETY: The invariant above guarantees an in-range child.
        node = unsafe { node_value(geo, node, pos) }.cast();
        if node.is_null() {
            panic!("missing B+Tree child");
        }
        height -= 1;
    }

    node
}

/// Allocates and installs a new root.
///
/// # Safety
///
/// `head` and `geo` must be valid, and the caller must serialize writers.
unsafe fn grow(head: *mut Btree, geo: &Geo, gfp: bindings::gfp_t) -> c_int {
    // SAFETY: The C allocator helper returns a zeroed node or NULL.
    let node = unsafe { bindings::btree_rust_node_alloc(head, gfp) };
    if node.is_null() {
        return -(bindings::ENOMEM as c_int);
    }

    // SAFETY: The caller supplies a valid head.
    if !unsafe { (*head).node }.is_null() {
        // SAFETY: The caller supplies a valid head.
        let old_root = unsafe { (*head).node };
        // SAFETY: A non-empty root has at least one pair.
        let fill = unsafe { first_empty(geo, old_root, 0) };
        if fill == 0 {
            // SAFETY: The new root has not been linked into the tree.
            unsafe { bindings::btree_rust_node_free(head, node) };
            panic!("empty non-leaf B+Tree root");
        }
        // SAFETY: The old root and new root are distinct allocations.
        unsafe {
            set_node_key(geo, node, 0, node_key(geo, old_root, fill - 1));
            set_node_value(geo, node, 0, old_root.cast());
        }
    }

    // SAFETY: The caller serializes writers.
    unsafe {
        (*head).node = node;
        (*head).height += 1;
    }
    0
}

/// Removes a one-child root.
///
/// # Safety
///
/// `head` and `geo` must be valid, and the caller must serialize writers.
unsafe fn shrink(head: *mut Btree, geo: &Geo) {
    // SAFETY: The caller guarantees a valid head.
    if unsafe { (*head).height } <= 1 {
        return;
    }

    // SAFETY: The caller serializes writers.
    let node = unsafe { (*head).node };
    // SAFETY: `node` is a valid root.
    let fill = unsafe { first_empty(geo, node, 0) };
    if fill > 1 {
        panic!("B+Tree root has more than one child");
    }
    // SAFETY: The root has an in-range first child.
    let new_root = unsafe { node_value(geo, node, 0) }.cast();
    // SAFETY: The caller serializes writers.
    unsafe {
        (*head).node = new_root;
        (*head).height -= 1;
        bindings::btree_rust_node_free(head, node);
    }
}

/// Inserts `key` and `val` at `level`.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid, `val` must not be NULL, and the
/// caller must serialize writers.
unsafe fn insert_level(
    head: *mut Btree,
    geo: &Geo,
    key: *const c_ulong,
    val: *mut c_void,
    level: c_int,
    gfp: bindings::gfp_t,
) -> c_int {
    if val.is_null() {
        panic!("B+Tree values must not be NULL");
    }

    // SAFETY: The caller guarantees a valid head.
    if unsafe { (*head).height } < level {
        // SAFETY: The caller serializes writers.
        let ret = unsafe { grow(head, geo, gfp) };
        if ret != 0 {
            return ret;
        }
    }

    let pairs = no_pairs(geo);
    loop {
        // SAFETY: The caller serializes writers.
        let node = unsafe { find_level(head, geo, key, level) };
        // SAFETY: The caller serializes writers.
        let pos = unsafe { find_pos(geo, node, key) };
        // SAFETY: The caller serializes writers.
        let fill = unsafe { first_empty(geo, node, pos) };
        if pos < fill
            // SAFETY: The caller serializes writers.
            && unsafe { key_compare(geo, node, pos, key) } == 0
        {
            panic!("duplicate B+Tree key");
        }

        if fill == pairs {
            // SAFETY: The C allocator helper returns a zeroed node or NULL.
            let new = unsafe { bindings::btree_rust_node_alloc(head, gfp) };
            if new.is_null() {
                return -(bindings::ENOMEM as c_int);
            }

            let split = fill / 2;
            // SAFETY: A full node has an in-range split boundary.
            let boundary = unsafe { node_key(geo, node, split - 1) };
            // SAFETY: The caller serializes writers and the inputs are valid.
            let ret = unsafe { insert_level(head, geo, boundary, new.cast(), level + 1, gfp) };
            if ret != 0 {
                // SAFETY: The new node has not been linked into the tree.
                unsafe { bindings::btree_rust_node_free(head, new) };
                return ret;
            }

            for pos in 0..split {
                // SAFETY: The source and destination pairs are in range.
                unsafe {
                    copy_pair(geo, new, pos, node, pos);
                    copy_pair(geo, node, pos, node, pos + split);
                    clear_pair(geo, node, pos + split);
                }
            }
            if fill & 1 != 0 {
                // SAFETY: The final pair and the vacated split pair are in range.
                unsafe {
                    copy_pair(geo, node, split, node, fill - 1);
                    clear_pair(geo, node, fill - 1);
                }
            }
            continue;
        }

        if fill >= pairs {
            panic!("invalid B+Tree node fill");
        }

        for pos in (pos + 1..=fill).rev() {
            // SAFETY: Both adjacent pair indices are in range.
            unsafe { copy_pair(geo, node, pos, node, pos - 1) };
        }
        // SAFETY: The caller serializes writers and the pair is in range.
        unsafe {
            set_node_key(geo, node, pos, key);
            set_node_value(geo, node, pos, val);
        }
        return 0;
    }
}

/// Inserts an entry into a B+Tree.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract, and the caller must
/// serialize writers.
unsafe fn insert(
    head: *mut Btree,
    geo: &Geo,
    key: *const c_ulong,
    val: *mut c_void,
    gfp: bindings::gfp_t,
) -> c_int {
    // SAFETY: The function contract guarantees valid arguments.
    unsafe { insert_level(head, geo, key, val, 1, gfp) }
}

/// Merges two sibling nodes and removes the obsolete parent pair.
///
/// # Safety
///
/// All nodes and indices must be valid, and the caller must serialize writers.
unsafe fn merge_nodes(head: *mut Btree, geo: &Geo, spec: MergeSpec) {
    let MergeSpec {
        level,
        left,
        left_fill,
        right,
        right_fill,
        parent,
        parent_pos,
    } = spec;

    for pos in 0..right_fill {
        // SAFETY: The destination and source pairs are in range.
        unsafe { copy_pair(geo, left, left_fill + pos, right, pos) };
    }

    // SAFETY: Both parent pairs are in range.
    unsafe {
        set_node_value(geo, parent, parent_pos, right.cast());
        set_node_value(geo, parent, parent_pos + 1, left.cast());
    }

    let mut removed_key: Key = [0; MAX_KEYLEN];
    // SAFETY: `removed_key` has room for every supported key.
    unsafe {
        copy_key(
            removed_key.as_mut_ptr(),
            node_key(geo, parent, parent_pos),
            keylen(geo),
        );
        remove_level(head, geo, removed_key.as_mut_ptr(), level + 1);
        bindings::btree_rust_node_free(head, right);
    }
}

/// Restores the merge invariant after an underfull removal.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid, `child` must be a live node at
/// `level`, and the caller must serialize writers.
unsafe fn rebalance(
    head: *mut Btree,
    geo: &Geo,
    key: *const c_ulong,
    level: c_int,
    child: *mut c_ulong,
    fill: usize,
) {
    if fill == 0 {
        // SAFETY: The caller serializes writers and all inputs are valid.
        unsafe {
            remove_level(head, geo, key, level + 1);
            bindings::btree_rust_node_free(head, child);
        }
        return;
    }

    // SAFETY: The caller serializes writers.
    let parent = unsafe { find_level(head, geo, key, level + 1) };
    // SAFETY: The caller serializes writers.
    let pos = unsafe { find_pos(geo, parent, key) };
    // SAFETY: The caller guarantees a valid parent.
    if pos >= no_pairs(geo) || unsafe { node_value(geo, parent, pos) }.cast() != child {
        panic!("B+Tree parent does not identify the underfull child");
    }

    if pos > 0 {
        // SAFETY: `pos` is in range and identifies the underfull child.
        let left = unsafe { node_value(geo, parent, pos - 1) }.cast();
        // SAFETY: `left` is a valid sibling.
        let left_fill = unsafe { first_empty(geo, left, 0) };
        if fill + left_fill <= no_pairs(geo) {
            // SAFETY: The nodes and parent pair are valid.
            unsafe {
                merge_nodes(
                    head,
                    geo,
                    MergeSpec {
                        level,
                        left,
                        left_fill,
                        right: child,
                        right_fill: fill,
                        parent,
                        parent_pos: pos - 1,
                    },
                )
            };
            return;
        }
    }

    // SAFETY: The parent is valid and `pos` is in range.
    let parent_fill = unsafe { first_empty(geo, parent, pos) };
    if pos + 1 < parent_fill {
        // SAFETY: `pos + 1` identifies the right sibling.
        let right = unsafe { node_value(geo, parent, pos + 1) }.cast();
        // SAFETY: `right` is a valid sibling.
        let right_fill = unsafe { first_empty(geo, right, 0) };
        if fill + right_fill <= no_pairs(geo) {
            // SAFETY: The nodes and parent pair are valid.
            unsafe {
                merge_nodes(
                    head,
                    geo,
                    MergeSpec {
                        level,
                        left: child,
                        left_fill: fill,
                        right,
                        right_fill,
                        parent,
                        parent_pos: pos,
                    },
                )
            };
        }
    }
}

/// Removes `key` at `level`.
///
/// # Safety
///
/// `head`, `geo`, and `key` must be valid, `level` must be non-negative, and
/// the caller must serialize writers.
unsafe fn remove_level(
    head: *mut Btree,
    geo: &Geo,
    key: *const c_ulong,
    level: c_int,
) -> *mut c_void {
    // SAFETY: The caller guarantees a valid head.
    if level > unsafe { (*head).height } {
        // SAFETY: The caller serializes writers.
        unsafe {
            (*head).height = 0;
            (*head).node = core::ptr::null_mut();
        }
        return core::ptr::null_mut();
    }

    // SAFETY: The caller serializes writers.
    let node = unsafe { find_level(head, geo, key, level) };
    // SAFETY: The caller serializes writers.
    let pos = unsafe { find_pos(geo, node, key) };
    // SAFETY: The caller serializes writers.
    let fill = unsafe { first_empty(geo, node, pos) };
    if pos >= fill {
        return core::ptr::null_mut();
    }
    if level == 1
        // SAFETY: The caller serializes writers.
        && unsafe { key_compare(geo, node, pos, key) } != 0
    {
        return core::ptr::null_mut();
    }

    // SAFETY: `pos` identifies an in-range value.
    let ret = unsafe { node_value(geo, node, pos) };
    let new_fill = fill - 1;
    for dst in pos..new_fill {
        // SAFETY: Both adjacent pairs are in range.
        unsafe { copy_pair(geo, node, dst, node, dst + 1) };
    }
    // SAFETY: The final occupied pair is in range.
    unsafe { clear_pair(geo, node, new_fill) };

    if new_fill < no_pairs(geo) / 2 {
        // SAFETY: The caller serializes writers.
        if level < unsafe { (*head).height } {
            // SAFETY: The arguments identify the underfull child.
            unsafe { rebalance(head, geo, key, level, node, new_fill) };
        } else if new_fill == 1 {
            // SAFETY: The caller serializes writers.
            unsafe { shrink(head, geo) };
        }
    }

    ret
}

/// Removes an entry from a B+Tree.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract, and the caller must
/// serialize writers.
unsafe fn remove(head: *mut Btree, geo: &Geo, key: *const c_ulong) -> *mut c_void {
    // SAFETY: The caller guarantees a valid head.
    if unsafe { (*head).height } == 0 {
        return core::ptr::null_mut();
    }
    // SAFETY: The caller serializes writers.
    unsafe { remove_level(head, geo, key, 1) }
}

/// Moves every entry from `victim` into `target`.
///
/// # Safety
///
/// Both heads and `geo` must be valid, the trees must have disjoint keys, both
/// trees must use compatible mempools, and the caller must serialize writers.
unsafe fn merge_trees(
    target: *mut Btree,
    victim: *mut Btree,
    geo: &Geo,
    gfp: bindings::gfp_t,
) -> c_int {
    if target == victim {
        panic!("cannot merge a B+Tree into itself");
    }

    // SAFETY: The caller supplies valid heads.
    if unsafe { (*target).node }.is_null() {
        // SAFETY: The caller serializes writers.
        unsafe {
            (*target).node = (*victim).node;
            (*target).height = (*victim).height;
            (*victim).node = core::ptr::null_mut();
            (*victim).height = 0;
        }
        return 0;
    }

    let mut key: Key = [0; MAX_KEYLEN];
    loop {
        // SAFETY: The caller supplies a valid victim and key buffer.
        let val = unsafe { last(victim, geo, key.as_mut_ptr()) };
        if val.is_null() {
            break;
        }

        // SAFETY: The caller serializes writers.
        let ret = unsafe { insert(target, geo, key.as_ptr(), val, gfp) };
        if ret != 0 {
            return ret;
        }

        let mut duplicate: Key = [0; MAX_KEYLEN];
        // SAFETY: `duplicate` has room for every supported key.
        unsafe {
            copy_key(duplicate.as_mut_ptr(), key.as_ptr(), keylen(geo));
            remove(victim, geo, duplicate.as_ptr());
        }
    }
    0
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

/// Implements `btree_insert()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_insert(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
    val: *mut c_void,
    gfp: bindings::gfp_t,
) -> c_int {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { insert(head, geo, key, val, gfp) }
}

/// Implements `btree_remove()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_remove(
    head: *mut Btree,
    geo: *mut Geo,
    key: *mut c_ulong,
) -> *mut c_void {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { remove(head, geo, key) }
}

/// Implements `btree_merge()`.
///
/// # Safety
///
/// The arguments must satisfy the public B+Tree contract.
#[export]
unsafe extern "C" fn rust_btree_merge(
    target: *mut Btree,
    victim: *mut Btree,
    geo: *mut Geo,
    gfp: bindings::gfp_t,
) -> c_int {
    // SAFETY: The public B+Tree contract guarantees a valid geometry.
    let geo = unsafe { &*geo };
    // SAFETY: The public B+Tree contract guarantees valid arguments.
    unsafe { merge_trees(target, victim, geo, gfp) }
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
