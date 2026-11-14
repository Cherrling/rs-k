// SPDX-License-Identifier: GPL-2.0

//! Native VMA-tree duplication.
//!
//! Rust owns the lock sequence, VMA iteration, per-VMA duplication decisions,
//! failure cleanup depth, rollback, and final success handling. C helpers
//! expose individual maple-tree, VMA, architecture, rmap, userfaultfd, and
//! page-table operations.

use crate::bindings;
use core::ffi::c_int;
use core::mem::MaybeUninit;
use core::ptr;
use macros::export;

#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
use core::sync::atomic::{AtomicPtr, AtomicU32, Ordering};

type Mm = bindings::mm_struct;
type State = bindings::rust_dup_mmap_state;
type Vma = bindings::vm_area_struct;

const FAIL_TREE: u32 = 1;
const FAIL_SOURCE_WRITE: u32 = 2;
const FAIL_DONTCOPY: u32 = 3;
const FAIL_ACCOUNT: u32 = 4;
const FAIL_VMA: u32 = 5;
const FAIL_POLICY: u32 = 6;
const FAIL_USERFAULTFD: u32 = 7;
const FAIL_ANON_VMA: u32 = 8;
const FAIL_PAGE_RANGE: u32 = 9;
const FAIL_ARCH: u32 = 10;

#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
static NEXT_FAILURE_MM: AtomicPtr<Mm> = AtomicPtr::new(ptr::null_mut());

fn errno(error: u32) -> c_int {
    error as c_int
}

#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
fn requested_failure(stage: u32, oldmm: *mut Mm) -> bool {
    if NEXT_FAILURE_MM.load(Ordering::Acquire) != oldmm {
        return false;
    }

    let requested = NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok();
    if requested {
        NEXT_FAILURE_MM.store(ptr::null_mut(), Ordering::Release);
    }
    requested
}

#[cfg(not(CONFIG_DUP_MMAP_KUNIT_TEST))]
fn requested_failure(_stage: u32, _oldmm: *mut Mm) -> bool {
    false
}

#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_DUP_MMAP_KUNIT_TEST))]
fn record_failure() {}

/// Returns the number of injected failure stages observed.
#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_mmap_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one failure at the selected stage.
#[cfg(CONFIG_DUP_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_dup_mmap_test_fail(mm: *mut Mm, stage: u32) {
    NEXT_FAILURE_MM.store(mm, Ordering::Release);
    NEXT_FAILURE.store(stage, Ordering::Release);
}

/// Releases state for a duplicate that failed before publication.
///
/// # Safety
///
/// `duplicate` must be privately owned. `release_policy` must be true only
/// after policy duplication transferred a policy reference.
unsafe fn cleanup_vma_failure(state: *mut State, duplicate: *mut Vma, release_policy: bool) {
    if release_policy {
        // SAFETY: Policy duplication transferred one reference.
        unsafe { bindings::rust_helper_dup_mmap_release_policy(duplicate) };
    }
    // SAFETY: The duplicate is detached and privately owned.
    unsafe { bindings::rust_helper_dup_mmap_free_vma(duplicate) };
    // SAFETY: The caller acquired one memory-accounting charge.
    unsafe { bindings::rust_helper_dup_mmap_unaccount_charge(state) };
}

/// Completes the locked portion of a duplication attempt.
///
/// # Safety
///
/// Both address spaces must have remained live. Their mmap write locks must be
/// held in source-then-destination order.
unsafe fn finish_locked(state: *mut State, mm: *mut Mm, oldmm: *mut Mm, retval: c_int) {
    // SAFETY: Both locks are held in the required order.
    unsafe {
        bindings::rust_helper_dup_mmap_unlock(mm);
        bindings::rust_helper_dup_mmap_flush_tlb(oldmm);
        bindings::rust_helper_dup_mmap_unlock(oldmm);
    }

    if retval == 0 {
        // SAFETY: VMA duplication completed and userfaultfd events can commit.
        unsafe { bindings::rust_helper_dup_mmap_userfault_complete(state) };
    } else {
        // SAFETY: The failed attempt must release accumulated userfaultfd work.
        unsafe { bindings::rust_helper_dup_mmap_userfault_fail(state) };
    }
}

/// Rolls back a partially populated destination VMA tree.
///
/// # Safety
///
/// The destination tree must contain only private duplicate VMAs and original
/// source entries that have not yet been replaced. No failure may be published.
unsafe fn rollback(state: *mut State, mm: *mut Mm, failure: *mut Vma) {
    // SAFETY: The destination mm remains live and its mmap write lock is held.
    let map_count = unsafe { bindings::rust_helper_dup_mmap_map_count(mm) };
    let end = if map_count == 0 {
        0
    } else if failure.is_null() {
        usize::MAX
    } else {
        // SAFETY: `failure` identifies the VMA that stopped the loop.
        unsafe { bindings::rust_helper_dup_mmap_vma_start(failure) }
    };

    // SAFETY: The address space is private and cannot be reaped normally.
    unsafe {
        bindings::rust_helper_dup_mmap_oom_skip(mm);
        bindings::rust_helper_dup_mmap_cleanup_partial(state, mm, end);
        bindings::rust_helper_dup_mmap_destroy_tree(mm);
        bindings::rust_helper_dup_mmap_set_unstable(mm);
    }
}

/// Duplicates all copyable VMAs from one address space into another.
///
/// # Safety
///
/// `mm` must be initialized and unpublished. `oldmm` must remain live until
/// this call returns. Both pointers must remain valid for the entire call.
pub(crate) unsafe fn duplicate(mm: *mut Mm, oldmm: *mut Mm) -> c_int {
    let mut storage = MaybeUninit::<State>::uninit();
    let state = storage.as_mut_ptr();
    // SAFETY: The helper initializes the complete state object.
    unsafe { bindings::rust_helper_dup_mmap_state_init(state, mm) };

    // SAFETY: The source address space is live.
    let retval = unsafe { bindings::rust_helper_dup_mmap_lock_source(oldmm) };
    if retval != 0 {
        return retval;
    }

    // SAFETY: The source write lock is held.
    unsafe {
        bindings::rust_helper_dup_mmap_flush_cache(oldmm);
        bindings::rust_helper_dup_mmap_uprobe(oldmm, mm);
        bindings::rust_helper_dup_mmap_lock_destination(mm);
        bindings::rust_helper_dup_mmap_exe_file(mm, oldmm);
        bindings::rust_helper_dup_mmap_copy_accounting(mm, oldmm);
    }

    let retval = if requested_failure(FAIL_TREE, oldmm) {
        record_failure();
        -errno(bindings::ENOMEM)
    } else {
        // SAFETY: Both mmap write locks are held for initialized trees.
        unsafe { bindings::rust_helper_dup_mmap_copy_tree(mm, oldmm) }
    };
    if retval != 0 {
        // SAFETY: The destination tree was not populated by this attempt.
        unsafe { finish_locked(state, mm, oldmm, retval) };
        return retval;
    }

    // SAFETY: The duplicated tree is private and both mmap locks are held.
    unsafe { bindings::rust_helper_dup_mmap_clear_tree_rcu(state) };

    let mut failure = ptr::null_mut();
    let mut retval = 0;

    loop {
        // SAFETY: The iterator walks the duplicated private tree.
        let source = unsafe { bindings::rust_helper_dup_mmap_next(state) };
        if source.is_null() {
            break;
        }

        retval = if requested_failure(FAIL_SOURCE_WRITE, oldmm) {
            record_failure();
            -errno(bindings::EINTR)
        } else {
            // SAFETY: The source VMA belongs to the locked source mm.
            unsafe { bindings::rust_helper_dup_mmap_source_start_write(source) }
        };
        if retval < 0 {
            failure = source;
            break;
        }

        // SAFETY: The source VMA remains protected by the source mmap lock.
        if unsafe { bindings::rust_helper_dup_mmap_dontcopy(source) } {
            retval = if requested_failure(FAIL_DONTCOPY, oldmm) {
                record_failure();
                -errno(bindings::ENOMEM)
            } else {
                // SAFETY: Clearing the source slot cannot free the old VMA.
                unsafe { bindings::rust_helper_dup_mmap_clear_dontcopy(state, source) }
            };
            if retval != 0 {
                failure = source;
                break;
            }
            // SAFETY: Removing this source entry affects destination accounting.
            unsafe { bindings::rust_helper_dup_mmap_unaccount_source(mm, source) };
            continue;
        }

        // SAFETY: No charge has been acquired for this VMA yet.
        unsafe { bindings::rust_helper_dup_mmap_set_charge(state, 0) };

        // SAFETY: Accounting decisions use the inherited source flags.
        if unsafe { bindings::rust_helper_dup_mmap_is_account(source) } {
            // SAFETY: The source VMA length is stable under the mmap lock.
            let pages = unsafe { bindings::rust_helper_dup_mmap_nr_pages(source) };
            let charged = if requested_failure(FAIL_ACCOUNT, oldmm) {
                record_failure();
                false
            } else {
                // SAFETY: The original code intentionally charges the source mm.
                unsafe { bindings::rust_helper_dup_mmap_account_memory(oldmm, pages) }
            };
            if !charged {
                retval = -errno(bindings::ENOMEM);
                failure = source;
                break;
            }
            // SAFETY: One accounting charge now belongs to this attempt.
            unsafe { bindings::rust_helper_dup_mmap_set_charge(state, pages) };
        }

        let duplicate = if requested_failure(FAIL_VMA, oldmm) {
            record_failure();
            ptr::null_mut()
        } else {
            // SAFETY: The source VMA remains live under the mmap lock.
            unsafe { bindings::rust_helper_dup_mmap_clone(source) }
        };
        if duplicate.is_null() {
            // SAFETY: Only a memory-accounting charge may be live.
            unsafe { bindings::rust_helper_dup_mmap_unaccount_charge(state) };
            retval = -errno(bindings::ENOMEM);
            failure = source;
            break;
        }

        retval = if requested_failure(FAIL_POLICY, oldmm) {
            record_failure();
            -errno(bindings::ENOMEM)
        } else {
            // SAFETY: Both VMAs are live and the duplicate is private.
            unsafe { bindings::rust_helper_dup_mmap_dup_policy(source, duplicate) }
        };
        if retval != 0 {
            // SAFETY: Policy duplication did not transfer a reference.
            unsafe {
                bindings::rust_helper_dup_mmap_free_vma(duplicate);
                bindings::rust_helper_dup_mmap_unaccount_charge(state);
            }
            retval = -errno(bindings::ENOMEM);
            failure = source;
            break;
        }

        // SAFETY: The policy reference is now owned by the duplicate.
        unsafe { bindings::rust_helper_dup_mmap_set_mm(duplicate, mm) };

        retval = if requested_failure(FAIL_USERFAULTFD, oldmm) {
            record_failure();
            -errno(bindings::ENOMEM)
        } else {
            // SAFETY: The duplicate is private and the context list is live.
            unsafe { bindings::rust_helper_dup_mmap_dup_userfaultfd(duplicate, state) }
        };
        if retval != 0 {
            // SAFETY: The duplicate owns its policy but is not in any tree.
            unsafe { cleanup_vma_failure(state, duplicate, true) };
            retval = -errno(bindings::ENOMEM);
            failure = source;
            break;
        }

        // SAFETY: Both VMAs are live under the mmap locks.
        let wipe_on_fork = unsafe { bindings::rust_helper_dup_mmap_wipeonfork(duplicate) };
        if wipe_on_fork {
            // SAFETY: A wipe-on-fork VMA starts without an anonymous VMA.
            unsafe { bindings::rust_helper_dup_mmap_clear_anon(duplicate) };
        } else {
            retval = if requested_failure(FAIL_ANON_VMA, oldmm) {
                record_failure();
                -errno(bindings::ENOMEM)
            } else {
                // SAFETY: The source remains locked and the duplicate is private.
                unsafe { bindings::rust_helper_dup_mmap_anon_fork(duplicate, source) }
            };
            if retval != 0 {
                // SAFETY: The duplicate owns its policy but is not in any tree.
                unsafe { cleanup_vma_failure(state, duplicate, true) };
                retval = -errno(bindings::ENOMEM);
                failure = source;
                break;
            }
        }

        // SAFETY: The duplicate is private and about to enter the new tree.
        unsafe {
            bindings::rust_helper_dup_mmap_dest_start_write(duplicate);
            bindings::rust_helper_dup_mmap_clear_locked(duplicate);
        }

        // SAFETY: Hugetlb duplication depends only on inherited VMA flags.
        if unsafe { bindings::rust_helper_dup_mmap_is_hugetlb(duplicate) } {
            // SAFETY: The private duplicate has an inherited hugetlb VMA.
            unsafe { bindings::rust_helper_dup_mmap_dup_hugetlb(duplicate) };
        }

        // SAFETY: Bulk storage replaces a source entry in the copied tree.
        unsafe {
            bindings::rust_helper_dup_mmap_store(state, duplicate);
            bindings::rust_helper_dup_mmap_inc_map_count(mm);
        }

        // SAFETY: The inserted duplicate owns a live vm_ops reference.
        if unsafe { bindings::rust_helper_dup_mmap_has_open(duplicate) } {
            // SAFETY: Invoke the inherited VMA open callback.
            unsafe { bindings::rust_helper_dup_mmap_call_open(duplicate) };
        }

        // SAFETY: The duplicate remains private and both mmap locks are held.
        let file = unsafe { bindings::rust_helper_dup_mmap_file(duplicate) };
        if !file.is_null() {
            // SAFETY: File-backed insertion follows the original rmap protocol.
            unsafe {
                bindings::rust_helper_dup_mmap_get_file(file);
                let mapping = bindings::rust_helper_dup_mmap_mapping(file);
                bindings::rust_helper_dup_mmap_rmap_lock(mapping);
                if bindings::rust_helper_dup_mmap_shared_maywrite(duplicate) {
                    bindings::rust_helper_dup_mmap_allow_writable(mapping);
                }
                bindings::rust_helper_dup_mmap_rmap_tree_lock(mapping);
                bindings::rust_helper_dup_mmap_rmap_insert_after(duplicate, source, mapping);
                bindings::rust_helper_dup_mmap_rmap_tree_unlock(mapping);
                bindings::rust_helper_dup_mmap_rmap_unlock(mapping);
            }
        }

        if !wipe_on_fork {
            retval = if requested_failure(FAIL_PAGE_RANGE, oldmm) {
                record_failure();
                -errno(bindings::ENOMEM)
            } else {
                // SAFETY: Both VMAs are live and the duplicate is inserted.
                unsafe { bindings::rust_helper_dup_mmap_copy_pages(duplicate, source) }
            };
        } else {
            retval = 0;
        }

        if retval != 0 {
            // The current duplicate is included in rollback. Advance to the
            // next source entry so its start address is the cleanup barrier.
            // SAFETY: The iterator remains valid after the inserted duplicate.
            failure = unsafe { bindings::rust_helper_dup_mmap_next(state) };
            break;
        }
    }

    // SAFETY: All iterator allocations are released before rollback or commit.
    unsafe { bindings::rust_helper_dup_mmap_free_iterator(state) };

    if retval == 0 {
        retval = if requested_failure(FAIL_ARCH, oldmm) {
            record_failure();
            -errno(bindings::ENOMEM)
        } else {
            // SAFETY: The completed duplicate has both mmap write locks held.
            unsafe { bindings::rust_helper_dup_mmap_arch(oldmm, mm) }
        };
    }

    if retval == 0 {
        // SAFETY: The duplicate is complete and can enter RCU readers.
        unsafe {
            bindings::rust_helper_dup_mmap_set_tree_rcu(state);
            bindings::rust_helper_dup_mmap_ksm(mm, oldmm);
            bindings::rust_helper_dup_mmap_khugepaged(mm, oldmm);
        }
    } else {
        // SAFETY: The destination is private and must roll back partial work.
        unsafe { rollback(state, mm, failure) };
    }

    // SAFETY: Both address spaces remain live and locked.
    unsafe { finish_locked(state, mm, oldmm, retval) };
    retval
}

/// Duplicates a VMA tree through the C ABI.
///
/// # Safety
///
/// The requirements of [`duplicate`] apply.
#[export]
unsafe extern "C" fn rust_dup_mmap(mm: *mut Mm, oldmm: *mut Mm) -> c_int {
    // SAFETY: The caller upholds the function requirements.
    unsafe { duplicate(mm, oldmm) }
}
