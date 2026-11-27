// SPDX-License-Identifier: GPL-2.0

//! Native generic skb lifecycle.
//!
//! Rust defines allocation, clone, reference-release, list-drop, NAPI
//! consumption, and final shell-release decisions. C retains the shared
//! layouts, slab and page-fragment allocation, NAPI caches, DMA and page-pool
//! recycling, tracepoint delivery, socket destructors, and driver callbacks.

use crate::bindings;
use core::ffi::{c_int, c_uint, c_void};
use macros::export;

type Gfp = bindings::gfp_t;
type KmemCache = bindings::kmem_cache;
type Skb = bindings::sk_buff;
type Sock = bindings::sock;

const GFP_ATOMIC: Gfp = bindings::GFP_ATOMIC;
const NUMA_NO_NODE: c_int = bindings::NUMA_NO_NODE;
const SKB_ALLOC_FCLONE: c_int = bindings::SKB_ALLOC_FCLONE as c_int;
const SKB_ALLOC_RX: c_int = bindings::SKB_ALLOC_RX as c_int;
const SKB_ALLOC_NAPI: c_int = bindings::SKB_ALLOC_NAPI as c_int;
const SKB_FCLONE_UNAVAILABLE: c_uint = bindings::SKB_FCLONE_UNAVAILABLE as c_uint;
const SKB_CONSUMED: c_uint = bindings::skb_drop_reason_SKB_CONSUMED as c_uint;
const SKB_DROP_REASON_NOT_SPECIFIED: c_uint =
    bindings::skb_drop_reason_SKB_DROP_REASON_NOT_SPECIFIED as c_uint;

const KFREE_SKB_BULK_SIZE: usize = 16;

const COUNTER_ALLOC: c_uint = bindings::RUST_SKB_TEST_COUNTER_ALLOC as c_uint;
const COUNTER_FREE: c_uint = bindings::RUST_SKB_TEST_COUNTER_FREE as c_uint;
const COUNTER_CLONE: c_uint = bindings::RUST_SKB_TEST_COUNTER_CLONE as c_uint;

#[cfg(CONFIG_SKB_LIFECYCLE_KUNIT_TEST)]
fn record(counter: c_uint) {
    // SAFETY: The helper updates the test-only observation counters.
    unsafe { bindings::rust_helper_skb_test_record(counter) };
}

#[cfg(not(CONFIG_SKB_LIFECYCLE_KUNIT_TEST))]
#[inline]
fn record(_counter: c_uint) {}

#[cfg(CONFIG_SKB_LIFECYCLE_KUNIT_TEST)]
fn injected_shell_failure() -> bool {
    let stage = bindings::RUST_SKB_TEST_FAIL_SHELL as c_uint;
    // SAFETY: The helper reads the test-only one-shot failure state.
    unsafe { bindings::rust_helper_skb_test_fail(stage) }
}

#[cfg(not(CONFIG_SKB_LIFECYCLE_KUNIT_TEST))]
#[inline]
fn injected_shell_failure() -> bool {
    false
}

/// Selects the generic or fclone shell cache.
///
/// # Safety
///
/// The caller must ensure that the network hot-data caches are initialized.
unsafe fn cache_for(fclone: bool) -> *mut KmemCache {
    // SAFETY: The helper only selects the corresponding global cache.
    unsafe { bindings::rust_helper_skb_cache(fclone) }
}

/// Allocates a shell from a selected skb cache.
///
/// # Safety
///
/// `cache` must be a live skb cache and the caller must own the returned shell.
unsafe fn cache_alloc(cache: *mut KmemCache, gfp_mask: Gfp, node: c_int) -> *mut Skb {
    // SAFETY: The caller supplies the allocation mask and node.
    unsafe { bindings::rust_helper_skb_cache_alloc(cache, gfp_mask, node) }
}

/// Returns an skb shell to its original cache.
///
/// # Safety
///
/// `cache` must be the cache that supplied `skb`. The caller must transfer the
/// shell and must not access it after this call.
unsafe fn cache_free(cache: *mut KmemCache, skb: *mut Skb) {
    // SAFETY: The caller transfers the shell allocated from `cache`.
    unsafe { bindings::rust_helper_skb_cache_free(cache, skb) };
}

/// Releases every object attached to an skb.
///
/// # Safety
///
/// `skb` must hold a live final reference and must not be accessed as a live
/// skb after this call.
unsafe fn release_all(skb: *mut Skb, reason: u32) {
    // SAFETY: The caller supplies a live skb reference.
    unsafe { bindings::rust_helper_skb_release_all(skb, reason) };
}

/// Releases a fully detached skb shell.
///
/// # Safety
///
/// `skb` must be a released shell and must not be accessed after this call.
unsafe fn release_shell(skb: *mut Skb) {
    record(COUNTER_FREE);
    // SAFETY: The caller supplies a fully released shell.
    unsafe { bindings::rust_helper_skb_free_shell(skb) };
}

/// Emits the selected consume or drop tracepoint.
///
/// # Safety
///
/// `skb` must hold the final reference through tracepoint delivery. `sk` must
/// be null or valid for the same interval.
unsafe fn trace_release(sk: *const Sock, skb: *mut Skb, reason: u32) {
    // SAFETY: The caller owns the final skb reference and supplies any live
    // receiving socket.
    if unsafe { bindings::rust_helper_skb_reason_is_consumed(reason) } {
        // SAFETY: The final reference remains live for tracepoint delivery.
        unsafe { bindings::rust_helper_skb_trace_consume(skb) };
    } else {
        // SAFETY: The final reference remains live for tracepoint delivery.
        unsafe { bindings::rust_helper_skb_trace_drop(sk, skb, reason) };
    }
}

/// Copies clone state into an exclusively owned destination shell.
///
/// # Safety
///
/// `new` must be an uninitialized or released shell that the caller owns.
/// `old` must remain live through this call.
unsafe fn clone_inner(new: *mut Skb, old: *mut Skb) {
    // SAFETY: The caller supplies exclusive destination storage and a live
    // source skb.
    unsafe {
        bindings::rust_helper_skb_clone_reset_links(new);
        bindings::rust_helper_skb_copy_header(new, old);
        bindings::rust_helper_skb_clone_copy_fields(new, old);
        bindings::rust_helper_skb_clone_share_data(new, old);
    }
}

/// Allocates an skb and its head storage.
///
/// # Safety
///
/// `gfp_mask`, `flags`, and `node` must satisfy the original `__alloc_skb()`
/// contract. The caller receives one skb reference on success.
#[export]
unsafe extern "C" fn __alloc_skb(
    size: c_uint,
    mut gfp_mask: Gfp,
    flags: c_int,
    node: c_int,
) -> *mut Skb {
    if injected_shell_failure() {
        return core::ptr::null_mut();
    }

    let fclone = flags & SKB_ALLOC_FCLONE != 0;

    // SAFETY: The helper only reads the global memalloc-socket state.
    if unsafe { bindings::rust_helper_skb_memalloc_socks() } && flags & SKB_ALLOC_RX != 0 {
        // SAFETY: The helper only adds the retained allocation flag.
        gfp_mask = unsafe { bindings::rust_helper_skb_gfp_memalloc(gfp_mask) };
    }

    // SAFETY: The helper only selects the global cache.
    let cache = unsafe { cache_for(fclone) };
    let mut skb = core::ptr::null_mut();

    // SAFETY: The helper only reads the current allocation context and node.
    if !fclone && !unsafe { bindings::rust_helper_skb_node_requires_fallback(node) } {
        if flags & SKB_ALLOC_NAPI != 0 {
            // SAFETY: The caller requested the interrupt-safe NAPI cache path.
            skb = unsafe { bindings::rust_helper_skb_napi_cache_get(true) };
            if skb.is_null() {
                return core::ptr::null_mut();
            }
        // SAFETY: The helper only reads the current interrupt context.
        } else if unsafe { bindings::rust_helper_skb_can_use_napi_cache() } {
            // SAFETY: The helper enters the required local-BH region.
            skb = unsafe { bindings::rust_helper_skb_napi_cache_get_bh() };
        }
    }

    if skb.is_null() {
        // SAFETY: The helper removes only the historical GFP_DMA cache flag.
        let cache_gfp = unsafe { bindings::rust_helper_skb_gfp_for_cache(gfp_mask) };
        // SAFETY: The selected cache and node belong to this allocation path.
        skb = unsafe { cache_alloc(cache, cache_gfp, node) };
        if skb.is_null() {
            return core::ptr::null_mut();
        }
    }

    // SAFETY: This function exclusively owns the freshly allocated shell.
    unsafe { bindings::rust_helper_skb_clear(skb) };

    let mut actual_size = size;
    // SAFETY: The shell is exclusively owned and the helper retains the
    // original allocator and pfmemalloc policy.
    let data =
        unsafe { bindings::rust_helper_skb_alloc_head(&mut actual_size, gfp_mask, node, skb) };
    if data.is_null() {
        record(COUNTER_FREE);
        // SAFETY: Head allocation failed, so the uninitialized shell still
        // belongs to `cache`.
        unsafe { cache_free(cache, skb) };
        return core::ptr::null_mut();
    }

    // SAFETY: The shell has no attached state and owns the new head.
    unsafe { bindings::rust_helper_skb_finalize_around(skb, data, actual_size) };
    if fclone {
        // SAFETY: The fclone cache supplied an uninitialized fclone group.
        unsafe { bindings::rust_helper_skb_init_fclone(skb) };
    }

    record(COUNTER_ALLOC);
    skb
}

/// Releases an skb with the unspecified drop reason.
///
/// # Safety
///
/// `skb` must hold its final reference and must not be accessed afterward.
#[export]
unsafe extern "C" fn __kfree_skb(skb: *mut Skb) {
    // SAFETY: The caller owns the final reference.
    unsafe { release_all(skb, SKB_DROP_REASON_NOT_SPECIFIED) };
    // SAFETY: All state has been released and the shell is no longer live.
    unsafe { release_shell(skb) };
}

/// Drops one skb reference with a drop reason.
///
/// # Safety
///
/// `skb` must hold a live reference. `sk` may be null or must remain live
/// through tracepoint delivery.
#[export]
unsafe extern "C" fn sk_skb_reason_drop(sk: *const Sock, skb: *mut Skb, reason: c_uint) {
    // SAFETY: The helper retains the original reference transition.
    if !unsafe { bindings::rust_helper_skb_unref(skb) } {
        return;
    }

    // SAFETY: The helper retains the original diagnostic assertion.
    unsafe { bindings::rust_helper_skb_check_drop_reason(reason) };
    // SAFETY: The final reference is live for tracepoint delivery.
    unsafe { trace_release(sk, skb, reason) };
    // SAFETY: Tracepoint delivery completed and this is the final reference.
    unsafe { __kfree_skb(skb) };
}

/// Releases a linked list of skbs with one reason.
///
/// # Safety
///
/// `segs` must be a valid singly linked skb chain and every list link must
/// hold a live skb reference.
#[export]
unsafe extern "C" fn kfree_skb_list_reason(mut segs: *mut Skb, reason: c_uint) {
    let mut batch = [core::ptr::null_mut::<c_void>(); KFREE_SKB_BULK_SIZE];
    let mut count = 0;

    while !segs.is_null() {
        // SAFETY: The current list node is live.
        let next = unsafe { bindings::rust_helper_skb_next(segs) };

        // SAFETY: The list link owns one live skb reference.
        if unsafe { bindings::rust_helper_skb_unref(segs) } {
            // SAFETY: The final reference remains live for tracepoint
            // delivery.
            unsafe { trace_release(core::ptr::null(), segs, reason) };
            // SAFETY: The skb has reached its final reference.
            unsafe { bindings::rust_helper_skb_poison_list(segs) };

            // SAFETY: The helper only reads the fclone state.
            if unsafe { bindings::rust_helper_skb_fclone_state(segs) } != SKB_FCLONE_UNAVAILABLE {
                // SAFETY: Fclone groups retain their dedicated shell release.
                unsafe { __kfree_skb(segs) };
            } else {
                // SAFETY: The shell is not part of an fclone group.
                unsafe { release_all(segs, reason) };
                batch[count] = segs.cast();
                count += 1;
                if count == KFREE_SKB_BULK_SIZE {
                    for skb in &batch {
                        record(COUNTER_FREE);
                        let _ = skb;
                    }
                    // SAFETY: Every pointer in the batch is a released shell
                    // from the generic skb cache.
                    unsafe {
                        bindings::rust_helper_skb_free_shell_bulk(batch.as_mut_ptr(), count as u32)
                    };
                    count = 0;
                }
            }
        }

        segs = next;
    }

    if count != 0 {
        for skb in &batch[..count] {
            record(COUNTER_FREE);
            let _ = skb;
        }
        // SAFETY: Every pointer in the final batch is a released generic skb
        // shell.
        unsafe { bindings::rust_helper_skb_free_shell_bulk(batch.as_mut_ptr(), count as u32) };
    }
}

/// Consumes one skb reference.
///
/// # Safety
///
/// `skb` must hold a live reference.
#[export]
unsafe extern "C" fn consume_skb(skb: *mut Skb) {
    // SAFETY: The caller supplies a live skb reference.
    if !unsafe { bindings::rust_helper_skb_unref(skb) } {
        return;
    }

    // SAFETY: This is the final reference.
    unsafe { bindings::rust_helper_skb_trace_consume(skb) };
    // SAFETY: Tracepoint delivery completed and this is the final reference.
    unsafe { __kfree_skb(skb) };
}

/// Consumes an skb whose head state has already been released.
///
/// # Safety
///
/// `skb` must hold the final reference and its head state must be empty.
#[export]
unsafe extern "C" fn __consume_stateless_skb(skb: *mut Skb) {
    // SAFETY: This is the final reference and tracepoint delivery precedes
    // data release.
    unsafe { bindings::rust_helper_skb_trace_consume(skb) };
    // SAFETY: The caller guarantees that head state is already empty.
    unsafe { bindings::rust_helper_skb_release_data(skb, SKB_CONSUMED) };
    // SAFETY: Data release completed and the shell is no longer live.
    unsafe { release_shell(skb) };
}

/// Consumes an skb in NAPI context.
///
/// # Safety
///
/// `skb` must hold a live reference. A non-zero `budget` requires the
/// original NAPI polling contract.
#[export]
unsafe extern "C" fn napi_consume_skb(skb: *mut Skb, budget: c_int) {
    if budget == 0 || skb.is_null() {
        // SAFETY: The helper retains the any-context consumption path.
        unsafe { bindings::rust_helper_skb_dev_consume_any(skb) };
        return;
    }

    // SAFETY: The helper retains the softirq diagnostic.
    unsafe { bindings::rust_helper_skb_check_napi_context() };

    // SAFETY: The helper only reads the deferred-free policy.
    let defer_enabled = !unsafe { bindings::rust_helper_skb_defer_disabled() };
    // SAFETY: The helper only reads the skb allocation CPU.
    let alloc_cpu_matches = unsafe { bindings::rust_helper_skb_alloc_cpu_matches(skb) };
    // SAFETY: The helper only reads the skb shared-data state.
    let skb_shared = unsafe { bindings::rust_helper_skb_shared(skb) };

    if defer_enabled && !alloc_cpu_matches && !skb_shared {
        // SAFETY: The deferred path owns the head state and final shell.
        unsafe {
            bindings::rust_helper_skb_release_head_state(skb);
            bindings::rust_helper_skb_attempt_defer_free(skb);
        }
        return;
    }

    // SAFETY: The caller supplies a live skb reference.
    if !unsafe { bindings::rust_helper_skb_unref(skb) } {
        return;
    }

    // SAFETY: This is the final reference.
    unsafe { bindings::rust_helper_skb_trace_consume(skb) };
    // SAFETY: The helper only reads the fclone state.
    if unsafe { bindings::rust_helper_skb_fclone_state(skb) } != SKB_FCLONE_UNAVAILABLE {
        // SAFETY: Fclone groups require their dedicated shell release.
        unsafe { __kfree_skb(skb) };
        return;
    }

    // SAFETY: The generic shell is no longer shared and this is its final
    // reference.
    unsafe { release_all(skb, SKB_CONSUMED) };
    // SAFETY: The released generic shell can enter the NAPI cache.
    unsafe { bindings::rust_helper_skb_napi_cache_put(skb) };
}

/// Allocates an skb that wraps a message frag list.
///
/// # Safety
///
/// `first` must remain live until the returned wrapper releases its frag list.
/// The caller receives one wrapper reference on success.
#[export]
unsafe extern "C" fn alloc_skb_for_msg(first: *mut Skb) -> *mut Skb {
    // SAFETY: The original zero-length atomic allocation contract is used.
    let new = unsafe { __alloc_skb(0, GFP_ATOMIC, 0, NUMA_NO_NODE) };
    if new.is_null() {
        return core::ptr::null_mut();
    }

    // SAFETY: Both skbs are live and the wrapper is exclusively owned.
    unsafe {
        (*new).len = (*first).len;
        (*new).data_len = (*first).len;
        (*new).truesize = (*first).truesize;
        bindings::rust_helper_skb_set_message_frag_list(new, first);
        bindings::rust_helper_skb_copy_header(new, first);
        bindings::rust_helper_skb_clear_destructor(new);
    }
    new
}

/// Morphs one skb into a clone of another.
///
/// # Safety
///
/// `dst` and `src` must be distinct live skbs. `dst` loses its previous
/// state and the returned pointer aliases `dst`.
#[export]
unsafe extern "C" fn skb_morph(dst: *mut Skb, src: *mut Skb) -> *mut Skb {
    // SAFETY: The destination is live and exclusively owned by this call.
    unsafe { release_all(dst, SKB_CONSUMED) };
    // SAFETY: The released destination storage can receive clone state.
    unsafe { clone_inner(dst, src) };
    dst
}

/// Clones an skb.
///
/// # Safety
///
/// `skb` must be live and must permit fragment orphaning under `gfp_mask`.
/// The caller receives one clone reference on success.
#[export]
unsafe extern "C" fn skb_clone(skb: *mut Skb, mut gfp_mask: Gfp) -> *mut Skb {
    // SAFETY: The helper retains the original fragment-orphaning callback
    // sequence.
    if unsafe { bindings::rust_helper_skb_orphan_frags(skb, gfp_mask) } != 0 {
        return core::ptr::null_mut();
    }

    // SAFETY: The helper only checks the fclone state and reference count.
    let clone = if unsafe { bindings::rust_helper_skb_fclone_ready(skb) } {
        // SAFETY: The ready fclone group supplies the clone shell.
        unsafe { bindings::rust_helper_skb_fclone_take(skb) }
    } else {
        // SAFETY: The helper only reads the pfmemalloc state.
        if unsafe { bindings::rust_helper_skb_pfmemalloc(skb) } {
            // SAFETY: The helper only adds the retained allocation flag.
            gfp_mask = unsafe { bindings::rust_helper_skb_gfp_memalloc(gfp_mask) };
        }
        // SAFETY: The cache allocation uses the caller-supplied mask.
        let clone = unsafe { bindings::rust_helper_skb_clone_cache_alloc(gfp_mask) };
        if clone.is_null() {
            return core::ptr::null_mut();
        }
        // SAFETY: The generic cache supplied an uninitialized shell.
        unsafe { bindings::rust_helper_skb_set_fclone_unavailable(clone) };
        clone
    };

    // SAFETY: The source is live and the clone shell is exclusively owned.
    unsafe { clone_inner(clone, skb) };
    record(COUNTER_CLONE);
    clone
}
