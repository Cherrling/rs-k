// SPDX-License-Identifier: GPL-2.0

//! Native address-space mapping teardown.
//!
//! Rust owns the mmap lock sequence, notifier ordering, VMA iteration,
//! page-table teardown decisions, OOM state publication, reverse-map removal,
//! accounting accumulation, and the final accounting release. C helpers
//! expose individual TLB, maple-tree, VMA, notifier, and architecture
//! primitives.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
use core::sync::atomic::{AtomicPtr, AtomicU32, AtomicU64, Ordering};

type Mm = bindings::mm_struct;
type State = bindings::rust_exit_mmap_state;
type Vma = bindings::vm_area_struct;

#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
static TEST_MM: AtomicPtr<Mm> = AtomicPtr::new(core::ptr::null_mut());
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
static VMA_RUNS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
static EMPTY_RUNS: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
static VMA_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
static ACCOUNTED_PAGES: AtomicU64 = AtomicU64::new(0);

#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
fn record_test(mm: *mut Mm, vma_count: u32, accounted_pages: u64) {
    if TEST_MM.load(Ordering::Acquire) != mm {
        return;
    }

    if vma_count == 0 {
        EMPTY_RUNS.fetch_add(1, Ordering::Relaxed);
    } else {
        VMA_RUNS.fetch_add(1, Ordering::Relaxed);
    }
    VMA_COUNT.store(vma_count, Ordering::Relaxed);
    ACCOUNTED_PAGES.store(accounted_pages, Ordering::Relaxed);
    TEST_MM.store(core::ptr::null_mut(), Ordering::Release);
}

#[cfg(not(CONFIG_EXIT_MMAP_KUNIT_TEST))]
fn record_test(_mm: *mut Mm, _vma_count: u32, _accounted_pages: u64) {}

/// Returns the number of watched address spaces that exited with VMAs.
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_exit_mmap_test_vma_runs() -> u32 {
    VMA_RUNS.load(Ordering::Relaxed)
}

/// Returns the number of watched address spaces that exited without VMAs.
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_exit_mmap_test_empty_runs() -> u32 {
    EMPTY_RUNS.load(Ordering::Relaxed)
}

/// Returns the number of VMAs removed by the last watched exit.
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_exit_mmap_test_vma_count() -> u32 {
    VMA_COUNT.load(Ordering::Relaxed)
}

/// Returns the last watched exit's accounted page count.
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_exit_mmap_test_accounted_pages() -> u64 {
    ACCOUNTED_PAGES.load(Ordering::Relaxed)
}

/// Watches one address space and resets the test counters.
#[cfg(CONFIG_EXIT_MMAP_KUNIT_TEST)]
#[export]
extern "C" fn rust_exit_mmap_test_watch(mm: *mut Mm) {
    VMA_RUNS.store(0, Ordering::Relaxed);
    EMPTY_RUNS.store(0, Ordering::Relaxed);
    VMA_COUNT.store(0, Ordering::Relaxed);
    ACCOUNTED_PAGES.store(0, Ordering::Relaxed);
    TEST_MM.store(mm, Ordering::Release);
}

/// Removes every VMA below the exit barrier.
///
/// # Safety
///
/// `mm` must have no users, `state` must contain the active VMA iterator, and
/// `vma` must be the first entry in the address space. The destination mmap
/// write lock must be held.
unsafe fn tear_down_vmas(mm: *mut Mm, state: *mut State, mut vma: *mut Vma) -> (usize, u32) {
    let mut nr_accounted = 0usize;
    let mut count = 0 as c_int;

    // SAFETY: The iterator belongs to `mm` and the first VMA is live.
    unsafe { bindings::rust_helper_exit_mmap_teardown_init(state, vma) };

    loop {
        // SAFETY: The VMA remains live under the mmap write lock.
        let pages = unsafe { bindings::rust_helper_exit_mmap_accounted_pages(vma) };
        nr_accounted = nr_accounted.wrapping_add(pages);

        // SAFETY: Every entry is removed exactly once during address-space exit.
        unsafe {
            bindings::rust_helper_exit_mmap_mark_detached(vma);
            bindings::rust_helper_exit_mmap_remove_vma(vma);
        }
        count = count.wrapping_add(1);

        // SAFETY: The original loop permits rescheduling between VMA removals.
        unsafe { bindings::rust_helper_exit_mmap_cond_resched() };
        // SAFETY: The iterator advances after the current entry is removed.
        vma = unsafe { bindings::rust_helper_exit_mmap_next(state) };
        if vma.is_null() {
            break;
        }
    }

    // SAFETY: `count` and the final map count describe the same removal pass.
    unsafe { bindings::rust_helper_exit_mmap_warn_count(mm, count) };
    (nr_accounted, count as u32)
}

/// Tears down all user mappings in an address space.
///
/// # Safety
///
/// `mm` must have no users and must own the final user reference. The call
/// must occur in sleepable context. `state` must be private to this call.
unsafe fn run(mm: *mut Mm, state: *mut State) {
    let mut nr_accounted = 0usize;
    let mut vma_count = 0u32;

    // SAFETY: The final user reference is gone and the mm remains live.
    unsafe { bindings::rust_helper_exit_mmap_notifier_release(mm) };
    // SAFETY: The private state is initialized before any iterator use.
    unsafe { bindings::rust_helper_exit_mmap_state_init(state, mm) };

    // SAFETY: The address space has no remaining users.
    unsafe { bindings::rust_helper_exit_mmap_read_lock(mm) };
    // SAFETY: The architecture exit hook runs under the mmap read lock.
    unsafe { bindings::rust_helper_exit_mmap_arch_exit(mm) };
    // SAFETY: The iterator has been initialized for this address space.
    let vma = unsafe { bindings::rust_helper_exit_mmap_next(state) };

    if vma.is_null() {
        // A failed duplication can publish an empty tree with OOM state.
        // SAFETY: Replacing the read lock with the write lock is required for
        // destroying an empty tree.
        unsafe {
            bindings::rust_helper_exit_mmap_read_unlock(mm);
            bindings::rust_helper_exit_mmap_write_lock(mm);
        }
    } else {
        // SAFETY: `vma` is the first live entry in the initialized tree.
        unsafe { bindings::rust_helper_exit_mmap_unmap_init(state, vma) };
        // SAFETY: The address space has no remaining users.
        unsafe { bindings::rust_helper_exit_mmap_flush_cache(mm) };
        // SAFETY: The full-mm TLB state belongs to this private stack state.
        unsafe { bindings::rust_helper_exit_mmap_tlb_init(state, mm) };
        // SAFETY: The unmap descriptor covers every VMA in the address space.
        unsafe { bindings::rust_helper_exit_mmap_unmap(state) };
        // SAFETY: All pages are unmapped before replacing the read lock.
        unsafe { bindings::rust_helper_exit_mmap_read_unlock(mm) };

        // SAFETY: The memory has been unmapped and hidden from the OOM reaper.
        unsafe { bindings::rust_helper_exit_mmap_oom_skip(mm) };
        // SAFETY: Page-table removal requires the mmap write lock.
        unsafe { bindings::rust_helper_exit_mmap_write_lock(mm) };
        // SAFETY: The unmap descriptor now operates under the write lock.
        unsafe { bindings::rust_helper_exit_mmap_set_write_locked(state) };
        // SAFETY: The private tree is leaving RCU before page-table removal.
        unsafe { bindings::rust_helper_exit_mmap_clear_tree_rcu(mm) };
        // SAFETY: The unmap descriptor is prepared for the page-table range.
        unsafe { bindings::rust_helper_exit_mmap_pgtable_init(state) };
        // SAFETY: All VMAs have been unmapped and the write lock is held.
        unsafe { bindings::rust_helper_exit_mmap_free_pgtables(state) };
        // SAFETY: Page-table removal completed and the TLB state is active.
        unsafe { bindings::rust_helper_exit_mmap_tlb_finish(state) };

        // SAFETY: The write lock is held and `vma` is still the first entry.
        let result = unsafe { tear_down_vmas(mm, state, vma) };
        (nr_accounted, vma_count) = result;
    }

    // SAFETY: Every VMA has been detached and the tree is no longer visible.
    unsafe { bindings::rust_helper_exit_mmap_destroy_tree(mm) };
    // SAFETY: The tree is destroyed and the mm remains live until return.
    unsafe { bindings::rust_helper_exit_mmap_trace(mm) };
    // SAFETY: No further tree or VMA operation occurs after the unlock.
    unsafe { bindings::rust_helper_exit_mmap_write_unlock(mm) };
    // SAFETY: Accounting is released after all mapping locks are dropped.
    unsafe { bindings::rust_helper_exit_mmap_unaccount(nr_accounted) };

    record_test(mm, vma_count, nr_accounted as u64);
}

/// Tears down all mappings through the stable C ABI.
///
/// # Safety
///
/// The same requirements as [`run`] apply. The C adapter allocates `state` on
/// its stack and must provide a private object.
#[export]
unsafe extern "C" fn rust_exit_mmap(mm: *mut Mm, state: *mut State) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { run(mm, state) };
}
