// SPDX-License-Identifier: GPL-2.0

//! Native implementation of the stable bottom-up merge sort used by `list_sort()`.
//!
//! The C entry point remains as an ABI adapter. When `CONFIG_RUST_LIST_SORT` is enabled, the
//! original C algorithm is excluded from the build.

use crate::bindings;
use core::{
    ffi::{c_int, c_void},
    ptr,
};
use macros::export;

type List = bindings::list_head;
type CompareFn = Option<unsafe extern "C" fn(*mut c_void, *const List, *const List) -> c_int>;

/// Calls the sort comparator.
///
/// # Safety
///
/// `cmp` must be present and callable with `private` and the two valid list nodes.
#[inline]
unsafe fn compare(private: *mut c_void, cmp: CompareFn, a: *mut List, b: *mut List) -> c_int {
    // SAFETY: The caller guarantees that the comparator is present and callable.
    unsafe { cmp.unwrap_unchecked()(private, a, b) }
}

/// Merges two null-terminated singly-linked lists.
///
/// # Safety
///
/// `a` and `b` must be valid, non-empty, disjoint null-terminated lists. Their `prev` pointers
/// are not used. `cmp` and `private` must satisfy the `list_sort()` comparator contract.
unsafe fn merge(
    private: *mut c_void,
    cmp: CompareFn,
    mut a: *mut List,
    mut b: *mut List,
) -> *mut List {
    let mut head = ptr::null_mut();
    let mut tail = ptr::addr_of_mut!(head);

    loop {
        // SAFETY: Both list nodes and the comparator satisfy this function's contract.
        if unsafe { compare(private, cmp, a, b) } <= 0 {
            // SAFETY: `tail` is the location of the last list link.
            unsafe { *tail = a };
            // SAFETY: `a` is a valid list node.
            tail = unsafe { ptr::addr_of_mut!((*a).next) };
            // SAFETY: `a` is a valid list node.
            a = unsafe { (*a).next };
            if a.is_null() {
                // SAFETY: `tail` is the location of the last list link.
                unsafe { *tail = b };
                break;
            }
        } else {
            // SAFETY: `tail` is the location of the last list link.
            unsafe { *tail = b };
            // SAFETY: `b` is a valid list node.
            tail = unsafe { ptr::addr_of_mut!((*b).next) };
            // SAFETY: `b` is a valid list node.
            b = unsafe { (*b).next };
            if b.is_null() {
                // SAFETY: `tail` is the location of the last list link.
                unsafe { *tail = a };
                break;
            }
        }
    }

    head
}

/// Performs the final merge and restores `prev` links and the circular list sentinel.
///
/// # Safety
///
/// `head` must be a valid list sentinel. `a` and `b` must be valid, non-empty, disjoint
/// null-terminated lists. `cmp` and `private` must satisfy the `list_sort()` comparator
/// contract.
unsafe fn merge_final(
    private: *mut c_void,
    cmp: CompareFn,
    head: *mut List,
    mut a: *mut List,
    mut b: *mut List,
) {
    let mut tail = head;

    loop {
        // SAFETY: Both list nodes and the comparator satisfy this function's contract.
        if unsafe { compare(private, cmp, a, b) } <= 0 {
            // SAFETY: `tail`, `a`, and `b` are valid list nodes.
            unsafe {
                (*tail).next = a;
                (*a).prev = tail;
            }
            tail = a;
            // SAFETY: `a` is a valid list node.
            a = unsafe { (*a).next };
            if a.is_null() {
                break;
            }
        } else {
            // SAFETY: `tail`, `a`, and `b` are valid list nodes.
            unsafe {
                (*tail).next = b;
                (*b).prev = tail;
            }
            tail = b;
            // SAFETY: `b` is a valid list node.
            b = unsafe { (*b).next };
            if b.is_null() {
                b = a;
                break;
            }
        }
    }

    // SAFETY: The remaining list starts at `b` and follows valid nodes.
    unsafe { (*tail).next = b };
    loop {
        // SAFETY: `tail` and `b` are valid list nodes.
        unsafe {
            (*b).prev = tail;
        }
        tail = b;
        // SAFETY: `b` is a valid list node.
        b = unsafe { (*b).next };
        if b.is_null() {
            break;
        }
    }

    // SAFETY: `tail` is the final list node and `head` is the list sentinel.
    unsafe {
        (*tail).next = head;
        (*head).prev = tail;
    }
}

/// Sorts an intrusive circular doubly-linked list.
///
/// # Safety
///
/// `head` must be an initialized list sentinel and `cmp` must be non-null. The caller must have
/// exclusive access to every node in the list for the duration of the call. `cmp` must obey the
/// antisymmetry and transitivity requirements documented by `list_sort()`.
unsafe fn sort(private: *mut c_void, head: *mut List, cmp: CompareFn) {
    // SAFETY: The caller guarantees that `head` is a valid list sentinel.
    let mut list = unsafe { (*head).next };
    let mut pending: *mut List = ptr::null_mut();
    let mut count = 0usize;

    // SAFETY: The caller guarantees that the sentinel is valid.
    if list == unsafe { (*head).prev } {
        return;
    }

    // Convert the input to a null-terminated singly-linked list.
    // SAFETY: The sentinel and the last node are valid.
    unsafe { (*(*head).prev).next = ptr::null_mut() };

    loop {
        let mut bits = count;
        let mut tail = ptr::addr_of_mut!(pending);

        while bits & 1 != 0 {
            // SAFETY: The `count` state guarantees that `*tail` is non-null and valid.
            tail = unsafe { ptr::addr_of_mut!((**tail).prev) };
            bits >>= 1;
        }

        if bits != 0 {
            // SAFETY: The pending-list invariant guarantees that `*tail` and its predecessor are
            // valid non-empty sublists.
            let a = unsafe { *tail };
            // SAFETY: The pending-list invariant guarantees that `a` is valid.
            let b = unsafe { (*a).prev };
            // SAFETY: Both sublists and the comparator satisfy the merge contract.
            let merged = unsafe { merge(private, cmp, b, a) };
            // SAFETY: The pending-list invariant guarantees that `merged` and `b` are valid.
            unsafe { (*merged).prev = (*b).prev };
            // SAFETY: `tail` selects the link from the pending list.
            unsafe { *tail = merged };
        }

        // Move one input element into the pending list.
        // SAFETY: `list` and `pending` are valid list nodes or null as required.
        unsafe {
            (*list).prev = pending;
        }
        pending = list;
        // SAFETY: `list` is a valid non-empty input node.
        list = unsafe { (*list).next };
        // SAFETY: The new pending sublist has one element.
        unsafe { (*pending).next = ptr::null_mut() };
        count += 1;

        if list.is_null() {
            break;
        }
    }

    list = pending;
    // SAFETY: At least two elements were sorted, so `pending` has a predecessor.
    pending = unsafe { (*pending).prev };
    loop {
        // SAFETY: `pending` is a valid pending sublist.
        let next = unsafe { (*pending).prev };
        if next.is_null() {
            break;
        }
        // SAFETY: Both pending sublists and the comparator satisfy the merge contract.
        list = unsafe { merge(private, cmp, pending, list) };
        pending = next;
    }

    // SAFETY: The final sublists and comparator satisfy the merge contract.
    unsafe { merge_final(private, cmp, head, pending, list) };
}

/// Sorts an intrusive circular doubly-linked list with a stable bottom-up merge sort.
///
/// # Safety
///
/// The contracts of `list_sort()` apply. `head` must be a valid initialized list sentinel,
/// `cmp` must be non-null, and the caller must have exclusive access to every list node.
#[export]
unsafe extern "C" fn rust_list_sort(private: *mut c_void, head: *mut List, cmp: CompareFn) {
    // SAFETY: Guaranteed by this function's contract.
    unsafe { sort(private, head, cmp) };
}
