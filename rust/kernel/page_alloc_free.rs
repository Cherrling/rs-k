// SPDX-License-Identifier: GPL-2.0-only

//! Native page allocation and free policy.
//!
//! Rust owns public allocation and free entry policy, NUMA fallback,
//! refcount attachment and rollback, compound-folio conversion, final-free
//! selection, and non-compound tail-page release order. C retains the buddy
//! allocator, page and folio layouts, reference and allocation-tag
//! primitives, reclaim, watermarks, per-CPU lists, and address translation.

use crate::bindings;
use core::ffi::{c_int, c_uint};
use macros::export;

type Folio = bindings::folio;
type Page = bindings::page;

const ALLOC_NOLOCK: c_uint = bindings::RUST_PAGE_ALLOC_FREE_ALLOC_NOLOCK;
const NUMA_NO_NODE: c_int = bindings::NUMA_NO_NODE;

#[cfg(CONFIG_PAGE_ALLOC_FREE_KUNIT_TEST)]
fn injected_failure(stage: c_uint) -> bool {
    // SAFETY: The helper reads and consumes the focused test-only state.
    unsafe { bindings::rust_helper_page_alloc_free_test_fail(stage) }
}

#[cfg(not(CONFIG_PAGE_ALLOC_FREE_KUNIT_TEST))]
#[inline]
fn injected_failure(_stage: c_uint) -> bool {
    false
}

#[inline]
/// Allocates a frozen page tree from the buddy allocator.
///
/// # Safety
///
/// `nodemask` must be null or point to a live mask for this call.
unsafe fn alloc_frozen(
    gfp: bindings::gfp_t,
    order: c_uint,
    nid: c_int,
    nodemask: *mut bindings::nodemask_t,
    alloc_flags: c_uint,
) -> *mut Page {
    // SAFETY: The caller satisfies the allocator and optional-nodemask contract.
    unsafe { bindings::rust_helper_page_alloc_free_frozen(gfp, order, nid, nodemask, alloc_flags) }
}

#[inline]
/// Adds the compound-page allocation flag.
///
/// # Safety
///
/// The helper preserves all caller-supplied GFP policy bits.
unsafe fn compound_flags(gfp: bindings::gfp_t) -> bindings::gfp_t {
    // SAFETY: The helper only adds the compound-page policy bit.
    unsafe { bindings::rust_helper_page_alloc_free_compound_flags(gfp) }
}

#[inline]
/// Gives a frozen allocation its first page reference.
///
/// # Safety
///
/// `page` must be a live frozen page-allocation head with a zero refcount.
unsafe fn set_page_refcounted(page: *mut Page) {
    // SAFETY: The allocator returned a frozen head with a zero refcount.
    unsafe { bindings::rust_helper_page_alloc_free_set_refcounted(page) };
}

#[inline]
fn numa_mem_id() -> c_int {
    // SAFETY: The helper reads topology state only.
    unsafe { bindings::rust_helper_page_alloc_free_numa_mem_id() }
}

#[inline]
fn numa_node_id() -> c_int {
    // SAFETY: The helper reads the current node only.
    unsafe { bindings::rust_helper_page_alloc_free_numa_node_id() }
}

#[inline]
/// Warns for an explicit automatic allocation from an offline node.
///
/// # Safety
///
/// The helper follows the normal page-allocation diagnostics contract.
unsafe fn warn_if_node_offline(nid: c_int, gfp: bindings::gfp_t) {
    // SAFETY: This call preserves the original allocation diagnostics.
    unsafe { bindings::rust_helper_page_alloc_free_warn_if_node_offline(nid, gfp) };
}

#[inline]
/// Marks a large folio as rmappable.
///
/// # Safety
///
/// `page` must be null or a live compound allocation.
unsafe fn rmappable_folio(page: *mut Page) -> *mut Folio {
    // SAFETY: The caller supplies the result of a compound allocation.
    unsafe { bindings::rust_helper_page_alloc_free_rmappable_folio(page) }
}

#[inline]
/// Tests whether a page is the head of its allocation.
///
/// # Safety
///
/// `page` must point to a live allocated page.
unsafe fn page_head(page: *mut Page) -> bool {
    // SAFETY: The caller supplies a live allocated page.
    unsafe { bindings::rust_helper_page_alloc_free_page_head(page) }
}

#[inline]
/// Gets the allocation-profiling tag used by the page.
///
/// # Safety
///
/// `page` must point to a live allocated page.
unsafe fn tag_get(page: *mut Page) -> usize {
    // SAFETY: The caller supplies a live allocated page.
    unsafe { bindings::rust_helper_page_alloc_free_tag_get(page) }
}

#[inline]
/// Drops one page reference and reports the zero transition.
///
/// # Safety
///
/// The caller must own a reference to `page`.
unsafe fn put_page_testzero(page: *mut Page) -> bool {
    // SAFETY: The caller owns the transferred page reference.
    unsafe { bindings::rust_helper_page_alloc_free_put_page_testzero(page) }
}

#[inline]
/// Accounts for tail pages after a non-final free of the head page.
///
/// # Safety
///
/// `tag` must be the saved tag for `page`.
unsafe fn tag_sub_pages(tag: usize, nr_pages: c_uint) {
    // SAFETY: The head-page free path owns the remaining allocation references.
    unsafe { bindings::rust_helper_page_alloc_free_tag_sub_pages(tag, nr_pages) };
}

#[inline]
/// Clears the allocation tag from a non-compound tail page.
///
/// # Safety
///
/// `page` must be an unallocated tail page from a non-compound allocation.
unsafe fn clear_tail_tag(page: *mut Page) {
    // SAFETY: The allocation owns the tail pages until this release.
    unsafe { bindings::rust_helper_page_alloc_free_clear_tail_tag(page) };
}

#[inline]
/// Returns a frozen page tree to the allocator.
///
/// # Safety
///
/// `page` must be a frozen allocation head, or an in-bounds tail-tree root.
unsafe fn free_frozen_page(page: *mut Page, order: c_uint, nolock: bool) {
    if nolock {
        // SAFETY: The caller supplies a frozen allocation for the no-lock path.
        unsafe { bindings::rust_helper_page_alloc_free_frozen_page_nolock(page, order) };
    } else {
        // SAFETY: The caller supplies a frozen allocation for the normal path.
        unsafe { bindings::rust_helper_page_alloc_free_frozen_page(page, order) };
    }
}

#[inline]
/// Validates and converts a page-allocation virtual address to its page.
///
/// # Safety
///
/// `addr` must satisfy the architecture virtual-address contract.
unsafe fn virt_to_page(addr: usize) -> *mut Page {
    // SAFETY: The helper retains the original VM_BUG_ON validation.
    unsafe { bindings::rust_helper_page_alloc_free_virt_to_page(addr) }
}

/// Allocates a frozen page tree and attaches its first reference.
///
/// # Safety
///
/// `nodemask` must be null or point to a live mask for this call.
unsafe fn allocate_with_refcount(
    gfp: bindings::gfp_t,
    order: c_uint,
    nid: c_int,
    nodemask: *mut bindings::nodemask_t,
    alloc_flags: c_uint,
) -> *mut Page {
    if injected_failure(bindings::RUST_PAGE_ALLOC_FREE_TEST_FAIL_ALLOC) {
        return core::ptr::null_mut();
    }

    // SAFETY: The caller satisfies the allocator and optional-nodemask contract.
    let page = unsafe { alloc_frozen(gfp, order, nid, nodemask, alloc_flags) };
    if page.is_null() {
        return page;
    }

    if injected_failure(bindings::RUST_PAGE_ALLOC_FREE_TEST_FAIL_REFCOUNT) {
        // SAFETY: The newly allocated tree is still frozen and unreferenced.
        unsafe { free_frozen_page(page, order, alloc_flags & ALLOC_NOLOCK != 0) };
        return core::ptr::null_mut();
    }

    // SAFETY: The allocator returned a frozen head with a zero refcount.
    unsafe { set_page_refcounted(page) };
    page
}

/// Allocates a page tree and attaches its first reference.
///
/// # Safety
///
/// `nodemask` must be null or point to a live node mask. The returned page
/// must be released with the matching page-free API.
#[export]
unsafe extern "C" fn __alloc_pages_noprof(
    gfp: bindings::gfp_t,
    order: c_uint,
    preferred_nid: c_int,
    nodemask: *mut bindings::nodemask_t,
    alloc_flags: c_uint,
) -> *mut Page {
    // SAFETY: Forward the allocator and optional-nodemask contract.
    unsafe { allocate_with_refcount(gfp, order, preferred_nid, nodemask, alloc_flags) }
}

/// Allocates pages from a preferred node.
///
/// # Safety
///
/// The returned page must be released with the matching page-free API.
#[export]
unsafe extern "C" fn alloc_pages_node_noprof(
    mut nid: c_int,
    gfp: bindings::gfp_t,
    order: c_uint,
) -> *mut Page {
    if nid == NUMA_NO_NODE {
        nid = numa_mem_id();
    }

    // SAFETY: The selected node and GFP flags follow the original diagnostics.
    unsafe { warn_if_node_offline(nid, gfp) };
    // SAFETY: The selected node and flags satisfy the page allocator contract.
    unsafe {
        allocate_with_refcount(
            gfp,
            order,
            nid,
            core::ptr::null_mut(),
            bindings::RUST_PAGE_ALLOC_FREE_ALLOC_DEFAULT,
        )
    }
}

/// Allocates a compound page tree and returns an rmappable folio.
///
/// # Safety
///
/// `nodemask` must be null or point to a live node mask. The returned folio
/// must be released with `folio_put()`.
#[export]
unsafe extern "C" fn __folio_alloc_noprof(
    gfp: bindings::gfp_t,
    order: c_uint,
    preferred_nid: c_int,
    nodemask: *mut bindings::nodemask_t,
) -> *mut Folio {
    // SAFETY: The caller supplies the compound-allocation contract.
    let page = unsafe {
        allocate_with_refcount(
            compound_flags(gfp),
            order,
            preferred_nid,
            nodemask,
            bindings::RUST_PAGE_ALLOC_FREE_ALLOC_DEFAULT,
        )
    };
    // SAFETY: The allocator result is null or a live compound page tree.
    unsafe { rmappable_folio(page) }
}

/// Releases one reference according to page-tree free policy.
///
/// # Safety
///
/// `page` must point to a live allocation of `order`, and the caller must
/// transfer one reference. `nolock` must match the allocation contract.
unsafe fn free_page_tree(page: *mut Page, order: c_uint, nolock: bool) {
    // SAFETY: The caller supplies a live allocated page.
    let head = unsafe { page_head(page) };
    // SAFETY: Save the allocation tag before the reference can reach zero.
    let tag = unsafe { tag_get(page) };

    // SAFETY: The caller transfers one reference.
    if unsafe { put_page_testzero(page) } {
        // SAFETY: The final reference returns the complete frozen allocation.
        unsafe { free_frozen_page(page, order, nolock) };
        return;
    }

    if head {
        return;
    }

    // SAFETY: The successful decrement left at least one reference. The
    // non-compound allocation owns the remaining tail-page references.
    unsafe { tag_sub_pages(tag, (1u32 << order) - 1) };

    let mut tail_order = order;
    while tail_order > 0 {
        tail_order -= 1;
        // SAFETY: Each descending tail subtree is in bounds and unallocated.
        let tail = unsafe { page.add(1usize << tail_order) };
        // SAFETY: A non-compound tail has no allocation tag of its own.
        unsafe {
            clear_tail_tag(tail);
            free_frozen_page(tail, tail_order, nolock);
        }
    }
}

/// Frees a page allocation.
///
/// # Safety
///
/// `page` must point to a live allocation of `order`, and the caller owns one
/// reference to its head.
#[export]
unsafe extern "C" fn __free_pages(page: *mut Page, order: c_uint) {
    // SAFETY: The caller transfers one reference to a live allocation.
    unsafe { free_page_tree(page, order, false) };
}

/// Frees a page allocation without blocking allocator locks.
///
/// # Safety
///
/// `page` must point to a live allocation of `order`, and the caller owns one
/// reference to its head. The caller must permit the no-lock allocator path.
#[export]
unsafe extern "C" fn free_pages_nolock(page: *mut Page, order: c_uint) {
    // SAFETY: The caller transfers one reference and permits no-lock release.
    unsafe { free_page_tree(page, order, true) };
}

/// Frees a page allocation from its kernel virtual address.
///
/// # Safety
///
/// A nonzero `addr` must be a live allocation address of `order`.
#[export]
unsafe extern "C" fn free_pages(addr: usize, order: c_uint) {
    if addr == 0 {
        return;
    }

    // SAFETY: The helper preserves the original virtual-address validation.
    let page = unsafe { virt_to_page(addr) };
    // SAFETY: The caller transferred one reference through the matching address.
    unsafe { free_page_tree(page, order, false) };
}

/// Allocates pages for a constrained context without blocking allocator locks.
///
/// # Safety
///
/// The allocation flags must satisfy the no-lock allocator contract. The
/// returned page must be released with `free_pages_nolock()`.
#[export]
unsafe extern "C" fn alloc_pages_nolock_noprof(
    gfp: bindings::gfp_t,
    mut nid: c_int,
    order: c_uint,
) -> *mut Page {
    if nid == NUMA_NO_NODE {
        nid = numa_node_id();
    }

    // SAFETY: The selected node and flags follow the no-lock allocator contract.
    unsafe {
        allocate_with_refcount(
            gfp,
            order,
            nid,
            core::ptr::null_mut(),
            bindings::RUST_PAGE_ALLOC_FREE_ALLOC_NOLOCK,
        )
    }
}
