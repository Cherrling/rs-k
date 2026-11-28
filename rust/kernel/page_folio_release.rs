// SPDX-License-Identifier: GPL-2.0-only

//! Native page and folio release lifecycle.
//!
//! Rust owns final-reference branching, batch compaction and refill, encoded
//! page parsing, LRU-cache drain decisions, and release ordering. C retains
//! the shared memory-descriptor layout and the allocator, LRU, memory cgroup,
//! hugetlb, zone-device, and deferred-split primitives.

use crate::bindings;
use core::ffi::{c_int, c_uint};
use macros::export;

type EncodedPage = bindings::encoded_page;
type Folio = bindings::folio;
type FolioBatch = bindings::folio_batch;
type Lruvec = bindings::lruvec;
type Page = bindings::page;
type ReleasePagesArg = bindings::release_pages_arg;

const FOLIO_BATCH_SIZE: usize = bindings::FOLIO_BATCH_SIZE as usize;
const ENCODED_PAGE_BIT_NR_PAGES_NEXT: usize = bindings::ENCODED_PAGE_BIT_NR_PAGES_NEXT as usize;

#[cfg(CONFIG_PAGE_FOLIO_RELEASE_KUNIT_TEST)]
fn injected_prepare_failure() -> bool {
    let stage = bindings::RUST_PAGE_FOLIO_RELEASE_TEST_FAIL_PREPARE;
    // SAFETY: The helper reads and consumes the focused test-only state.
    unsafe { bindings::rust_helper_page_folio_release_test_fail(stage) }
}

#[cfg(not(CONFIG_PAGE_FOLIO_RELEASE_KUNIT_TEST))]
#[inline]
fn injected_prepare_failure() -> bool {
    false
}

#[inline]
/// Tests whether a folio belongs to zone-device memory.
///
/// # Safety
///
/// `folio` must point to a live folio.
unsafe fn is_zone_device(folio: *const Folio) -> bool {
    // SAFETY: The caller supplies a live folio.
    unsafe { bindings::rust_helper_page_folio_release_is_zone_device(folio) }
}

#[inline]
/// Tests whether a folio is hugetlb memory.
///
/// # Safety
///
/// `folio` must point to a live folio.
unsafe fn is_hugetlb(folio: *const Folio) -> bool {
    // SAFETY: The caller supplies a live folio.
    unsafe { bindings::rust_helper_page_folio_release_is_hugetlb(folio) }
}

#[inline]
/// Tests whether a folio is the shared huge-zero folio.
///
/// # Safety
///
/// `folio` must point to a live folio.
unsafe fn is_huge_zero(folio: *const Folio) -> bool {
    // SAFETY: The caller supplies a live folio.
    unsafe { bindings::rust_helper_page_folio_release_is_huge_zero(folio) }
}

#[inline]
/// Subtracts folio references and reports the zero transition.
///
/// # Safety
///
/// `folio` must be live and the caller must own `refs` references.
unsafe fn ref_sub_and_test(folio: *mut Folio, refs: c_uint) -> bool {
    // SAFETY: The caller owns or shares the required folio references.
    unsafe { bindings::rust_helper_page_folio_release_folio_ref_sub_and_test(folio, refs) }
}

#[inline]
/// Releases final zone-device memory.
///
/// # Safety
///
/// `folio` must be zone-device memory with its final reference.
unsafe fn free_zone_device(folio: *mut Folio) {
    // SAFETY: The folio reached its final reference and is zone-device memory.
    unsafe { bindings::rust_helper_page_folio_release_zone_device(folio) };
}

#[inline]
/// Releases final hugetlb memory.
///
/// # Safety
///
/// `folio` must be hugetlb memory with its final reference.
unsafe fn free_hugetlb(folio: *mut Folio) {
    // SAFETY: The folio reached its final reference and is hugetlb memory.
    unsafe { bindings::rust_helper_page_folio_release_hugetlb(folio) };
}

#[inline]
/// Removes a final folio from the deferred-split queue.
///
/// # Safety
///
/// `folio` must have no remaining references.
unsafe fn unqueue_deferred_split(folio: *mut Folio) {
    // SAFETY: The folio reached zero references and is not hugetlb memory.
    unsafe { bindings::rust_helper_page_folio_release_deferred(folio) };
}

#[inline]
/// Removes a final folio from page-cache LRU state.
///
/// # Safety
///
/// `folio` must be live and `lruvec` and `flags` must be valid storage.
unsafe fn page_cache_release(folio: *mut Folio, lruvec: &mut *mut Lruvec, flags: &mut usize) {
    // SAFETY: The caller serializes the folio release and provides storage
    // for the optional locked lruvec.
    unsafe { bindings::rust_helper_page_folio_release_cache_locked(folio, lruvec, flags) };
}

#[inline]
/// Releases an accumulated LRU lock.
///
/// # Safety
///
/// `lruvec` and `flags` must come from a matching cache-release call.
unsafe fn unlock_lruvec(lruvec: &mut *mut Lruvec, flags: &mut usize) {
    // SAFETY: The pointer and flags came from the matching cache helper.
    unsafe { bindings::rust_helper_page_folio_release_unlock(lruvec, flags) };
}

#[inline]
/// Removes memory-cgroup accounting for a final folio.
///
/// # Safety
///
/// `folio` must have no remaining references.
unsafe fn uncharge(folio: *const Folio) {
    // SAFETY: The folio reached zero references and is no longer in use.
    unsafe { bindings::rust_helper_page_folio_release_uncharge(folio) };
}

#[inline]
/// Removes memory-cgroup accounting for a final folio batch.
///
/// # Safety
///
/// `folios` must contain only final-reference folios.
unsafe fn uncharge_batch(folios: *const FolioBatch) {
    // SAFETY: The compacted batch contains only final-reference folios.
    unsafe { bindings::rust_helper_page_folio_release_uncharge_batch(folios) };
}

#[inline]
/// Returns a final ordinary folio to the page allocator.
///
/// # Safety
///
/// `folio` must be an ordinary folio with no remaining references.
unsafe fn free_frozen(folio: *mut Folio) {
    // SAFETY: The ordinary folio reached zero references.
    unsafe { bindings::rust_helper_page_folio_release_frozen(folio) };
}

#[inline]
/// Returns a final ordinary folio batch to the page allocator.
///
/// # Safety
///
/// `folios` must contain only ordinary final-reference folios.
unsafe fn free_unref_batch(folios: *const FolioBatch) {
    // SAFETY: The compacted batch contains ordinary final-reference folios.
    unsafe { bindings::rust_helper_page_folio_release_unref_batch(folios) };
}

#[inline]
/// Drains deferred local LRU additions.
///
/// # Safety
///
/// The caller must satisfy the normal batch-release context contract.
unsafe fn lru_add_drain() {
    // SAFETY: The original batch-release contract permits LRU cache draining.
    unsafe { bindings::rust_helper_page_folio_release_lru_drain() };
}

#[inline]
/// Converts a page to its head folio.
///
/// # Safety
///
/// `page` must point to a live page.
unsafe fn page_folio(page: *mut Page) -> *mut Folio {
    // SAFETY: The caller supplies a live page pointer.
    unsafe { bindings::rust_helper_page_folio_release_page_folio(page) }
}

#[inline]
/// Removes encoded pointer flags.
///
/// # Safety
///
/// `page` must be a valid encoded-page entry.
unsafe fn encoded_ptr(page: *mut EncodedPage) -> *mut Page {
    // SAFETY: The caller supplies a valid encoded-page entry.
    unsafe { bindings::rust_helper_page_folio_release_encoded_ptr(page) }
}

#[inline]
/// Returns encoded-page flags.
///
/// # Safety
///
/// `page` must be a valid encoded-page entry.
unsafe fn encoded_flags(page: *mut EncodedPage) -> usize {
    // SAFETY: The caller supplies a valid encoded-page entry.
    unsafe { bindings::rust_helper_page_folio_release_encoded_flags(page) }
}

#[inline]
/// Returns the reference count from an encoded nr-pages entry.
///
/// # Safety
///
/// `page` must be a valid encoded nr-pages entry.
unsafe fn encoded_nr_pages(page: *mut EncodedPage) -> usize {
    // SAFETY: The caller supplies a valid encoded nr-pages entry.
    unsafe { bindings::rust_helper_page_folio_release_encoded_nr_pages(page) }
}

/// Releases a folio that already reached its final reference.
///
/// # Safety
///
/// `folio` must hold exactly the final reference and must not be used after
/// this call.
#[export]
unsafe extern "C" fn __folio_put(folio: *mut Folio) {
    // SAFETY: The function contract requires a live folio.
    if unsafe { is_zone_device(folio) } {
        // SAFETY: The final reference belongs to the caller.
        unsafe { free_zone_device(folio) };
        return;
    }

    // SAFETY: The function contract requires a live folio.
    if unsafe { is_hugetlb(folio) } {
        // SAFETY: The final reference belongs to the caller.
        unsafe { free_hugetlb(folio) };
        return;
    }

    let mut lruvec = core::ptr::null_mut();
    let mut flags = 0;
    // SAFETY: The folio and all delegated state are live until release.
    unsafe {
        page_cache_release(folio, &mut lruvec, &mut flags);
        unlock_lruvec(&mut lruvec, &mut flags);
        unqueue_deferred_split(folio);
        uncharge(folio);
        free_frozen(folio);
    }
}

/// Reduces references for a batch of folios and releases the final refs.
///
/// # Safety
///
/// Every non-null batch entry must be live. `refs`, when non-null, must point
/// to at least as many entries as the batch count.
#[export]
unsafe extern "C" fn folios_put_refs(folios: *mut FolioBatch, refs: *mut c_uint) {
    let mut lruvec = core::ptr::null_mut();
    let mut flags = 0;
    let mut input = 0usize;
    let mut output = 0usize;

    // SAFETY: The caller validates the batch and optional refs array.
    while input < unsafe { (*folios).nr as usize } {
        // SAFETY: `input` is within the initialized batch range.
        let folio = unsafe { (*folios).folios[input] };
        let nr_refs = if refs.is_null() {
            1
        } else {
            // SAFETY: The optional refs array covers the initialized batch range.
            unsafe { *refs.add(input) }
        };
        input += 1;

        if folio.is_null() {
            continue;
        }
        // SAFETY: The entry is a live folio.
        if unsafe { is_huge_zero(folio) } {
            continue;
        }

        // SAFETY: The entry is a live folio with caller-owned references.
        if unsafe { is_zone_device(folio) } {
            // SAFETY: LRU state cannot cross a zone-device release.
            unsafe { unlock_lruvec(&mut lruvec, &mut flags) };
            // SAFETY: The caller transferred `nr_refs` references.
            if unsafe { ref_sub_and_test(folio, nr_refs) } {
                // SAFETY: This subtraction reached the final reference.
                unsafe { free_zone_device(folio) };
            }
            continue;
        }

        // SAFETY: The caller transferred `nr_refs` references.
        if !unsafe { ref_sub_and_test(folio, nr_refs) } {
            continue;
        }

        // SAFETY: The final reference is still live while selecting a branch.
        if unsafe { is_hugetlb(folio) } {
            // SAFETY: LRU state cannot cross a hugetlb release.
            unsafe { unlock_lruvec(&mut lruvec, &mut flags) };
            // SAFETY: This folio reached its final reference.
            unsafe { free_hugetlb(folio) };
            continue;
        }

        // SAFETY: The ordinary folio reached its final reference.
        unsafe { unqueue_deferred_split(folio) };
        // SAFETY: The final reference remains valid during LRU removal.
        unsafe { page_cache_release(folio, &mut lruvec, &mut flags) };

        if output != input - 1 {
            // SAFETY: Both indices are inside the initialized batch range.
            unsafe { (*folios).folios[output] = folio };
        }
        output += 1;
    }

    // SAFETY: Drop any lruvec lock acquired by the last ordinary folio.
    unsafe { unlock_lruvec(&mut lruvec, &mut flags) };

    if output == 0 {
        // SAFETY: The caller owns the batch and can reuse it immediately.
        unsafe {
            (*folios).nr = 0;
            (*folios).i = 0;
        }
        return;
    }

    // SAFETY: The caller owns the compacted batch.
    unsafe { (*folios).nr = output as u8 };
    // SAFETY: The batch now contains exactly the ordinary final-reference refs.
    unsafe {
        uncharge_batch(folios);
        free_unref_batch(folios);
    }
}

/// Releases an array of pages, folios, or encoded pages.
///
/// # Safety
///
/// `arg` must contain `nr` live entries of the selected representation. Each
/// entry transfers the references described by its encoded form.
#[export]
unsafe extern "C" fn release_pages(arg: ReleasePagesArg, nr: c_int) {
    if injected_prepare_failure() {
        return;
    }

    // SAFETY: The transparent union contains the same array pointer in every
    // representation.
    let encoded = unsafe { arg.encoded_pages };
    let mut fbatch = FolioBatch {
        nr: 0,
        i: 0,
        percpu_pvec_drained: false,
        folios: [core::ptr::null_mut(); FOLIO_BATCH_SIZE],
    };
    let mut refs = [0u32; FOLIO_BATCH_SIZE];
    let mut index = 0usize;

    while index < nr as usize {
        // SAFETY: The caller supplies `nr` valid encoded entries.
        let entry = unsafe { *encoded.add(index) };
        // SAFETY: `entry` is a valid encoded page.
        let folio = unsafe { page_folio(encoded_ptr(entry)) };
        let slot = fbatch.nr as usize;
        refs[slot] = 1;

        // SAFETY: `entry` is a valid encoded page with optional flags.
        if unsafe { encoded_flags(entry) } & ENCODED_PAGE_BIT_NR_PAGES_NEXT != 0 {
            index += 1;
            // SAFETY: The flag guarantees a following nr-pages entry.
            let next = unsafe { *encoded.add(index) };
            // SAFETY: `next` is the encoded nr-pages value.
            refs[slot] = unsafe { encoded_nr_pages(next) as u32 };
        }

        fbatch.folios[slot] = folio;
        fbatch.nr += 1;
        index += 1;

        if fbatch.nr as usize == FOLIO_BATCH_SIZE {
            // SAFETY: The local batch and refs array are live and full.
            unsafe { folios_put_refs(&mut fbatch, refs.as_mut_ptr()) };
        }
    }

    if fbatch.nr != 0 {
        // SAFETY: The local batch contains the remaining live entries.
        unsafe { folios_put_refs(&mut fbatch, refs.as_mut_ptr()) };
    }
}

/// Releases a folio batch after draining deferred LRU additions once.
///
/// # Safety
///
/// The batch must contain live folios with caller-owned references.
#[export]
unsafe extern "C" fn __folio_batch_release(fbatch: *mut FolioBatch) {
    // SAFETY: The caller owns the live batch.
    if !unsafe { (*fbatch).percpu_pvec_drained } {
        // SAFETY: Batch release may run in the original process or interrupt
        // context and drain the local LRU cache.
        unsafe { lru_add_drain() };
        // SAFETY: The caller owns the live batch.
        unsafe { (*fbatch).percpu_pvec_drained = true };
    }

    // SAFETY: Every live entry transfers one folio reference.
    unsafe { folios_put_refs(fbatch, core::ptr::null_mut()) };
}
