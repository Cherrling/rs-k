// SPDX-License-Identifier: GPL-2.0

//! Native address-space construction and task attachment.
//!
//! Rust owns the `mm_struct` initialization order, fallible resource cleanup,
//! address-space duplication sequence, `CLONE_VM` decision, and task pointer
//! publication. C helpers provide private constructors, architecture hooks,
//! locking primitives, VMA duplication, and subsystem entry points.

use crate::bindings;
use crate::mm_core;
use core::ffi::c_int;
use core::ptr;
use macros::export;

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Task = bindings::task_struct;
type Mm = bindings::mm_struct;

const CLONE_VM: u64 = bindings::CLONE_VM as u64;

const FAIL_PGD: u32 = 1;
const FAIL_ID: u32 = 2;
const FAIL_CONTEXT: u32 = 3;
const FAIL_CID: u32 = 4;
const FAIL_SCHED: u32 = 5;
const FAIL_RSS: u32 = 6;

const FAIL_DUP_MMAP: u32 = 1;
const FAIL_DUP_BINFMT: u32 = 2;

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
static NEXT_INIT_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
static INIT_FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
static NEXT_DUP_FAILURE: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
static DUP_FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
enum InitCleanup {
    None,
    Pgd,
    Id,
    Context,
    Cid,
    Sched,
}

fn errno(error: u32) -> c_int {
    error as c_int
}

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_INIT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_COPY_MM_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
fn record_init_failure() {
    INIT_FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_COPY_MM_KUNIT_TEST))]
fn record_init_failure() {}

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
fn requested_dup_failure(stage: u32) -> bool {
    NEXT_DUP_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_COPY_MM_KUNIT_TEST))]
fn requested_dup_failure(_stage: u32) -> bool {
    false
}

#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
fn record_dup_failure() {
    DUP_FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_COPY_MM_KUNIT_TEST))]
fn record_dup_failure() {}

/// Returns the number of injected constructor failures observed.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
extern "C" fn rust_copy_mm_test_init_failure_count() -> u32 {
    INIT_FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one constructor failure at the selected stage.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
extern "C" fn rust_copy_mm_test_fail_mm_init(stage: u32) {
    NEXT_INIT_FAILURE.store(stage, Ordering::Release);
}

/// Returns the number of injected duplication failures observed.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
extern "C" fn rust_copy_mm_test_dup_failure_count() -> u32 {
    DUP_FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one duplication failure at the selected stage.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
extern "C" fn rust_copy_mm_test_fail_dup(stage: u32) {
    NEXT_DUP_FAILURE.store(stage, Ordering::Release);
}

/// Releases resources acquired during address-space construction.
///
/// # Safety
///
/// `mm` must be an unpublished address space. `stage` must identify the last
/// successfully initialized resource.
unsafe fn cleanup_init(mm: *mut Mm, stage: InitCleanup) {
    if stage >= InitCleanup::Sched {
        // SAFETY: The scheduler state was initialized for this address space.
        unsafe { bindings::rust_helper_copy_mm_destroy_sched(mm) };
    }
    if stage >= InitCleanup::Cid {
        // SAFETY: The CPU-ID state was initialized for this address space.
        unsafe { bindings::rust_helper_copy_mm_destroy_cid(mm) };
    }
    if stage >= InitCleanup::Context {
        // SAFETY: The architecture context was initialized.
        unsafe { bindings::rust_helper_copy_mm_destroy_context(mm) };
    }
    if stage >= InitCleanup::Id {
        // SAFETY: The address-space ID was allocated.
        unsafe { bindings::rust_helper_copy_mm_free_id(mm) };
    }
    if stage >= InitCleanup::Pgd {
        // SAFETY: The page tables were allocated.
        unsafe { bindings::rust_helper_copy_mm_free_pgd(mm) };
    }
    // SAFETY: The unpublished address space is no longer owned by a caller.
    unsafe { bindings::rust_helper_copy_mm_free(mm) };
}

/// Initializes non-fallible address-space state in the required order.
///
/// # Safety
///
/// `mm` must point to an allocated but uninitialized `mm_struct`. `task` must
/// remain live for the duration of initialization.
unsafe fn init_mm_fields(mm: *mut Mm, task: *mut Task) {
    // SAFETY: The caller supplies freshly allocated task and mm structures.
    // Each helper initializes one primitive or tightly coupled C object.
    unsafe {
        bindings::rust_helper_copy_mm_mt_init(mm);
        bindings::rust_helper_copy_mm_mt_set_external_lock(mm);
        bindings::rust_helper_copy_mm_users_init(mm);
        bindings::rust_helper_copy_mm_count_init(mm);
        bindings::rust_helper_copy_mm_write_protect_seq_init(mm);
        bindings::rust_helper_copy_mm_mmap_lock_init(mm);
        bindings::rust_helper_copy_mm_mmlist_init(mm);
        bindings::rust_helper_copy_mm_pgtables_bytes_init(mm);
        bindings::rust_helper_copy_mm_map_count_init(mm);
        bindings::rust_helper_copy_mm_locked_vm_init(mm);
        bindings::rust_helper_copy_mm_pinned_vm_init(mm);
        bindings::rust_helper_copy_mm_rss_stat_init(mm);
        bindings::rust_helper_copy_mm_page_table_lock_init(mm);
        bindings::rust_helper_copy_mm_arg_lock_init(mm);
        bindings::rust_helper_copy_mm_cpumask_init(mm);
        bindings::rust_helper_copy_mm_aio_init(mm);
        bindings::rust_helper_copy_mm_owner_init(mm, task);
        bindings::rust_helper_copy_mm_pasid_init(mm);
        bindings::rust_helper_copy_mm_exe_file_init(mm);
        bindings::rust_helper_copy_mm_mmu_notifier_init(mm);
        bindings::rust_helper_copy_mm_tlb_flush_pending_init(mm);
        bindings::rust_helper_copy_mm_pmd_huge_pte_init(mm);
        bindings::rust_helper_copy_mm_uprobes_state_init(mm);
        bindings::rust_helper_copy_mm_hugetlb_count_init(mm);
        bindings::rust_helper_copy_mm_futex_init(mm);
        bindings::rust_helper_copy_mm_flags_clear_all(mm);
    }
}

/// Initializes an allocated address space.
///
/// # Safety
///
/// `mm` must point to an allocated but uninitialized `mm_struct`. `task` must
/// remain live for the duration of initialization.
unsafe fn init_mm_struct(mm: *mut Mm, task: *mut Task) -> bool {
    // SAFETY: The caller supplies freshly allocated task and mm structures.
    unsafe { init_mm_fields(mm, task) };

    // SAFETY: Any live process address space is valid as the inheritance
    // source. A null pointer selects the coredump-filter defaults.
    let source = unsafe { bindings::rust_helper_copy_mm_current_mm() };
    let flags = if source.is_null() {
        // SAFETY: The helper returns the immutable boot-time coredump mask.
        unsafe { bindings::rust_helper_copy_mm_coredump_filter() }
    } else {
        // SAFETY: The source is a live address space.
        let source_flags = unsafe { bindings::rust_helper_copy_mm_flags_word(source) };
        // SAFETY: This converts inherited legacy bits without side effects.
        unsafe { bindings::rust_helper_copy_mm_init_legacy_flags(source_flags) }
    };
    // SAFETY: The new address space is unpublished and exclusively owned.
    unsafe {
        bindings::rust_helper_copy_mm_set_flags(mm, flags);
        bindings::rust_helper_copy_mm_set_def_flags(
            mm,
            bindings::rust_helper_copy_mm_def_flags(source),
        );
    }

    let mut stage = InitCleanup::None;

    if requested_failure(FAIL_PGD) {
        record_init_failure();
        // SAFETY: Common fields are initialized and only the allocation must
        // be released.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The unpublished address space needs page tables.
    if unsafe { bindings::rust_helper_copy_mm_alloc_pgd(mm) } != 0 {
        record_init_failure();
        // SAFETY: No fallible resource has been acquired.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    stage = InitCleanup::Pgd;

    if requested_failure(FAIL_ID) {
        record_init_failure();
        // SAFETY: Page tables are the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The address space needs a unique architecture ID.
    if unsafe { bindings::rust_helper_copy_mm_alloc_id(mm) } != 0 {
        record_init_failure();
        // SAFETY: Page tables are the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    stage = InitCleanup::Id;

    if requested_failure(FAIL_CONTEXT) {
        record_init_failure();
        // SAFETY: The address-space ID is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The unpublished task owns the new address space.
    if unsafe { bindings::rust_helper_copy_mm_init_context(task, mm) } != 0 {
        record_init_failure();
        // SAFETY: The address-space ID is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    stage = InitCleanup::Context;

    if requested_failure(FAIL_CID) {
        record_init_failure();
        // SAFETY: The architecture context is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The new task needs per-mm CPU-ID state.
    if unsafe { bindings::rust_helper_copy_mm_alloc_cid(mm, task) } != 0 {
        record_init_failure();
        // SAFETY: The architecture context is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    stage = InitCleanup::Cid;

    if requested_failure(FAIL_SCHED) {
        record_init_failure();
        // SAFETY: CPU-ID state is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The address space needs scheduler accounting state.
    if unsafe { bindings::rust_helper_copy_mm_alloc_sched(mm) } != 0 {
        record_init_failure();
        // SAFETY: CPU-ID state is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    stage = InitCleanup::Sched;

    if requested_failure(FAIL_RSS) {
        record_init_failure();
        // SAFETY: Scheduler state is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }
    // SAFETY: The address space needs per-CPU RSS counters.
    if unsafe { bindings::rust_helper_copy_mm_init_rss(mm) } != 0 {
        record_init_failure();
        // SAFETY: Scheduler state is the last acquired resource.
        unsafe { cleanup_init(mm, stage) };
        return false;
    }

    // SAFETY: All address-space resources are initialized.
    unsafe { bindings::rust_helper_copy_mm_lru_gen_init(mm) };
    true
}

/// Releases a partially duplicated address space.
///
/// # Safety
///
/// `mm` must be initialized and exclusively owned by the caller.
unsafe fn cleanup_duplicate(mm: *mut Mm) {
    // SAFETY: The module reference is acquired only after VMA duplication.
    unsafe {
        bindings::rust_helper_copy_mm_clear_binfmt(mm);
        bindings::rust_helper_copy_mm_clear_owner(mm);
    }
    // SAFETY: The caller owns the final user reference.
    unsafe { mm_core::put_sync(mm) };
}

/// Duplicates an address space.
///
/// # Safety
///
/// `task`, `oldmm`, and `current->mm` must remain live during duplication.
unsafe fn duplicate_mm(task: *mut Task, oldmm: *mut Mm) -> *mut Mm {
    // SAFETY: The allocation helper returns either null or a private slot.
    let mm = unsafe { bindings::rust_helper_copy_mm_allocate() };
    if mm.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The destination is newly allocated and the source is initialized.
    unsafe { ptr::copy_nonoverlapping(oldmm, mm, 1) };
    // SAFETY: The copy is unpublished and exclusively owned.
    if !unsafe { init_mm_struct(mm, task) } {
        return ptr::null_mut();
    }

    // SAFETY: Registered uprobes must remain stable while VMAs are copied.
    unsafe { bindings::rust_helper_copy_mm_uprobe_start() };
    let error = if requested_dup_failure(FAIL_DUP_MMAP) {
        record_dup_failure();
        -errno(bindings::ENOMEM)
    } else {
        // SAFETY: Both address spaces are live and the task is under
        // construction.
        unsafe { bindings::rust_helper_copy_mm_dup_mmap(mm, oldmm) }
    };
    if error != 0 {
        // SAFETY: Initialization completed and no address space was published.
        unsafe { cleanup_duplicate(mm) };
        // SAFETY: This balances the successful uprobe start.
        unsafe { bindings::rust_helper_copy_mm_uprobe_end() };
        return ptr::null_mut();
    }
    // SAFETY: VMA duplication completed.
    unsafe { bindings::rust_helper_copy_mm_uprobe_end() };

    // SAFETY: The duplicated VMA tree is complete.
    unsafe { bindings::rust_helper_copy_mm_set_hiwater(mm) };
    let binfmt_acquired = if requested_dup_failure(FAIL_DUP_BINFMT) {
        record_dup_failure();
        false
    } else {
        // SAFETY: The binary-format reference is acquired lazily.
        (unsafe { bindings::rust_helper_copy_mm_get_binfmt(mm) }) != 0
    };
    if !binfmt_acquired {
        // SAFETY: The duplicate has not been published.
        unsafe { cleanup_duplicate(mm) };
        return ptr::null_mut();
    }

    mm
}

/// Initializes an allocated address space through the C ABI.
///
/// # Safety
///
/// The requirements of [`init_mm_struct`] apply.
#[export]
unsafe extern "C" fn rust_mm_init(mm: *mut Mm, task: *mut Task) -> *mut Mm {
    // SAFETY: The caller upholds the function requirements.
    if unsafe { init_mm_struct(mm, task) } {
        mm
    } else {
        ptr::null_mut()
    }
}

/// Copies address-space state into a new task.
///
/// # Safety
///
/// `task` must be an unpublished task under construction and `current` must
/// remain in task-creation context.
unsafe fn copy_mm_state(clone_flags: u64, task: *mut Task, oldmm: *mut Mm) -> c_int {
    // SAFETY: The unpublished task belongs exclusively to task creation.
    unsafe {
        (*task).min_flt = 0;
        (*task).maj_flt = 0;
        (*task).nvcsw = 0;
        (*task).nivcsw = 0;
    }
    #[cfg(CONFIG_DETECT_HUNG_TASK)]
    // SAFETY: The task is unpublished and exclusively owned.
    unsafe {
        (*task).last_switch_count = 0;
        (*task).last_switch_time = 0;
    }
    // SAFETY: The task has no published address space yet.
    unsafe {
        (*task).mm = ptr::null_mut();
        (*task).active_mm = ptr::null_mut();
    }

    if oldmm.is_null() {
        return 0;
    }

    let mm = if clone_flags & CLONE_VM != 0 {
        // SAFETY: The source address space is live in task-creation context.
        unsafe { mm_core::get(oldmm) };
        oldmm
    } else {
        // SAFETY: Task creation serializes publication of the new task.
        let mm = unsafe { duplicate_mm(task, oldmm) };
        if mm.is_null() {
            return -errno(bindings::ENOMEM);
        }
        mm
    };

    // SAFETY: The task is unpublished and now owns one user reference.
    unsafe {
        (*task).mm = mm;
        (*task).active_mm = mm;
        bindings::rust_helper_copy_mm_sched_cache_fork(task);
    }
    0
}

/// Copies address-space state to a new task through the C ABI.
///
/// # Safety
///
/// The requirements of [`copy_mm_state`] apply. The source address space is
/// `current->mm`.
#[export]
unsafe extern "C" fn rust_copy_mm(clone_flags: u64, task: *mut Task) -> c_int {
    // SAFETY: The caller remains in task-creation context.
    let oldmm = unsafe { bindings::rust_helper_copy_mm_current_mm() };
    // SAFETY: The caller upholds the function requirements.
    unsafe { copy_mm_state(clone_flags, task, oldmm) }
}

/// Duplicates an address space for KUnit coverage.
///
/// # Safety
///
/// `task` must be live and `oldmm` must have a live user reference. The caller
/// owns the returned address space on success.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_copy_mm_test_dup(task: *mut Task, oldmm: *mut Mm) -> *mut Mm {
    // SAFETY: The caller upholds the function requirements.
    unsafe { duplicate_mm(task, oldmm) }
}

/// Exercises task address-space attachment without a source address space.
///
/// # Safety
///
/// `task` must point to writable task storage that remains live for this call.
#[cfg(CONFIG_COPY_MM_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_copy_mm_test_kernel_thread(task: *mut Task) -> c_int {
    // SAFETY: The caller supplies writable task storage and no source mm.
    unsafe { copy_mm_state(0, task, ptr::null_mut()) }
}
