// SPDX-License-Identifier: GPL-2.0

//! Native credential lifecycle.
//!
//! Rust owns credential allocation initialization, reference acquisition and
//! rollback, exec and fork preparation, commit ordering, RCU deferred release,
//! and final resource release. C helpers retain the slab allocator, atomic
//! operations, task pointer publication, LSM callbacks, key and namespace
//! primitives, and RCU queueing.

use crate::bindings;
use core::{ffi::c_int, mem::offset_of, ptr};
use macros::export;

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Cred = bindings::cred;
type Inode = bindings::inode;
type Task = bindings::task_struct;

const RLIMIT_NPROC: bindings::rlimit_type = bindings::rlimit_type_UCOUNT_RLIMIT_NPROC;
const INVALID_ID: u32 = u32::MAX;

const FAIL_CRED_ALLOC: u32 = 1;
const FAIL_BLANK_ALLOC: u32 = 2;
const FAIL_SECURITY_BLANK: u32 = 3;
const FAIL_UCOUNTS_GET: u32 = 4;
const FAIL_SECURITY_PREPARE: u32 = 5;
const FAIL_UCOUNTS_ALLOC: u32 = 6;
const FAIL_USER_NS: u32 = 7;

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static ENTRY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static PREPARE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static FORK_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static COMMIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static EXIT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static RELEASE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_entry() {
    ENTRY_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_entry() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_prepare() {
    PREPARE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_prepare() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_fork() {
    FORK_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_fork() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_commit() {
    COMMIT_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_commit() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_exit() {
    EXIT_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_exit() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_release() {
    RELEASE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_release() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn record_failure() {}

#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_CRED_LIFECYCLE_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

fn injected_failure(stage: u32) -> bool {
    if requested_failure(stage) {
        record_failure();
        true
    } else {
        false
    }
}

#[inline]
fn uid_eq(left: bindings::kuid_t, right: bindings::kuid_t) -> bool {
    left.val == right.val
}

#[inline]
fn gid_eq(left: bindings::kgid_t, right: bindings::kgid_t) -> bool {
    left.val == right.val
}

/// Tests whether changing from `old` to `new` can change task dumpability.
///
/// # Safety
///
/// Both pointers must refer to live credential records.
unsafe fn changes_dumpability(old: *const Cred, new: *const Cred) -> bool {
    // SAFETY: The caller provides live credential records.
    let (old, new) = unsafe { (&*old, &*new) };

    !uid_eq(old.euid, new.euid)
        || !gid_eq(old.egid, new.egid)
        || !uid_eq(old.fsuid, new.fsuid)
        || !gid_eq(old.fsgid, new.fsgid)
        // SAFETY: Both credentials are live for this decision.
        || !unsafe { cap_issubset(old, new) }
}

/// Tests whether a credential change requires a UID connector event.
///
/// # Safety
///
/// Both pointers must refer to live credential records.
unsafe fn changes_uid(old: *const Cred, new: *const Cred) -> bool {
    // SAFETY: The caller provides live credential records.
    let (old, new) = unsafe { (&*old, &*new) };

    !uid_eq(old.uid, new.uid)
        || !uid_eq(old.euid, new.euid)
        || !uid_eq(old.suid, new.suid)
        || !uid_eq(old.fsuid, new.fsuid)
}

/// Tests whether a credential change requires a GID connector event.
///
/// # Safety
///
/// Both pointers must refer to live credential records.
unsafe fn changes_gid(old: *const Cred, new: *const Cred) -> bool {
    // SAFETY: The caller provides live credential records.
    let (old, new) = unsafe { (&*old, &*new) };

    !gid_eq(old.gid, new.gid)
        || !gid_eq(old.egid, new.egid)
        || !gid_eq(old.sgid, new.sgid)
        || !gid_eq(old.fsgid, new.fsgid)
}

/// Tests whether `subset` has capabilities contained in `set`.
///
/// # Safety
///
/// Both pointers must refer to live credential records.
unsafe fn cap_issubset(set: *const Cred, subset: *const Cred) -> bool {
    // SAFETY: The caller provides live credential records.
    let (set, subset) = unsafe { (&*set, &*subset) };
    let set_ns = set.user_ns;
    let mut subset_ns = subset.user_ns;

    if set_ns == subset_ns {
        return subset.cap_permitted.val & !set.cap_permitted.val == 0;
    }

    let init_user_ns = ptr::addr_of_mut!(bindings::init_user_ns);
    while subset_ns != init_user_ns {
        if subset_ns.is_null() {
            return false;
        }

        // SAFETY: A user namespace remains live while its descendant
        // credential is live.
        if set_ns == unsafe { (*subset_ns).parent }
            // SAFETY: The parent pointer above established a live namespace.
            && uid_eq(unsafe { (*subset_ns).owner }, set.euid)
        {
            return true;
        }

        // SAFETY: A live non-initial user namespace has a live parent until
        // the initial namespace, where the loop terminates.
        subset_ns = unsafe { (*subset_ns).parent };
    }

    false
}

/// Returns the number of production entry calls observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_entry_count() -> u32 {
    ENTRY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of credential preparations observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_prepare_count() -> u32 {
    PREPARE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of fork credential operations observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_fork_count() -> u32 {
    FORK_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of credential commits observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_commit_count() -> u32 {
    COMMIT_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of exiting-task credential operations observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_exit_count() -> u32 {
    EXIT_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of final credential releases observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_release_count() -> u32 {
    RELEASE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of injected failures observed.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one failure at the selected credential operation stage.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_cred_test_fail_next(stage: u32) {
    NEXT_FAILURE.store(stage, Ordering::Release);
}

/// Returns whether the commit path must update task dumpability.
///
/// # Safety
///
/// `old` and `new` must point to live credential records.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_cred_test_changes_dumpability(
    old: *const Cred,
    new: *const Cred,
) -> bool {
    // SAFETY: The test supplies credential records with valid identity fields.
    unsafe { changes_dumpability(old, new) }
}

/// Returns whether the commit path must send a UID connector event.
///
/// # Safety
///
/// `old` and `new` must point to live credential records.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_cred_test_changes_uid(old: *const Cred, new: *const Cred) -> bool {
    // SAFETY: The test supplies credential records with valid identity fields.
    unsafe { changes_uid(old, new) }
}

/// Returns whether the commit path must send a GID connector event.
///
/// # Safety
///
/// `old` and `new` must point to live credential records.
#[cfg(CONFIG_CRED_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_cred_test_changes_gid(old: *const Cred, new: *const Cred) -> bool {
    // SAFETY: The test supplies credential records with valid identity fields.
    unsafe { changes_gid(old, new) }
}

/// Acquires one credential reference unless the count is already zero.
///
/// # Safety
///
/// `cred` must remain addressable while the caller holds the RCU read lock.
unsafe fn get_rcu(cred: *const Cred) -> bool {
    if cred.is_null() {
        return false;
    }

    // SAFETY: The caller guarantees that the RCU-protected storage is live.
    unsafe { bindings::rust_helper_cred_get_rcu(cred) }
}

/// Acquires credential references using the committed-reference contract.
///
/// # Safety
///
/// A non-NULL `cred` must have a positive reference count.
unsafe fn get_many(cred: *const Cred, nr: isize) -> *const Cred {
    if !cred.is_null() {
        // SAFETY: The caller guarantees that the credential is live.
        unsafe { bindings::rust_helper_cred_get_many(cred, nr) };
    }
    cred
}

/// Releases references and dispatches the final release.
///
/// # Safety
///
/// A non-NULL `cred` must have at least `nr` references owned by the caller.
unsafe fn put_many(cred: *const Cred, nr: isize) {
    if cred.is_null() {
        return;
    }

    let cred = cred.cast_mut();
    // SAFETY: The caller owns the references being released.
    if unsafe { bindings::rust_helper_cred_sub_and_test(cred, nr) } {
        // SAFETY: The count reached zero and the caller transferred final
        // release responsibility to this path.
        unsafe { __put_cred(cred) };
    }
}

/// Acquires the references copied from an existing credential.
///
/// # Safety
///
/// `new` must contain valid copied credential dependency pointers.
unsafe fn acquire_common_refs(new: *mut Cred) {
    // SAFETY: The caller provides a valid credential copy.
    unsafe {
        (*new).group_info = bindings::rust_helper_cred_group_get((*new).group_info);
        (*new).user = bindings::rust_helper_cred_user_get((*new).user);
        (*new).user_ns = bindings::rust_helper_cred_user_ns_get((*new).user_ns);
    }
}

#[cfg(CONFIG_KEYS)]
/// Acquires every keyring reference copied into `new`.
///
/// # Safety
///
/// `new` must contain valid copied keyring pointers.
unsafe fn acquire_key_refs(new: *mut Cred) {
    // SAFETY: The caller provides valid copied keyring pointers.
    unsafe {
        (*new).session_keyring = bindings::rust_helper_cred_key_get((*new).session_keyring);
        (*new).process_keyring = bindings::rust_helper_cred_key_get((*new).process_keyring);
        (*new).thread_keyring = bindings::rust_helper_cred_key_get((*new).thread_keyring);
        (*new).request_key_auth = bindings::rust_helper_cred_key_get((*new).request_key_auth);
    }
}

#[cfg(not(CONFIG_KEYS))]
unsafe fn acquire_key_refs(_new: *mut Cred) {}

#[cfg(CONFIG_KEYS)]
/// Releases all keyring references owned by a final credential.
///
/// # Safety
///
/// `cred` must own the keyring references and remain live until all releases
/// complete.
unsafe fn release_key_refs(cred: *mut Cred) {
    // SAFETY: The caller owns every keyring reference.
    unsafe {
        bindings::rust_helper_cred_key_put((*cred).session_keyring);
        bindings::rust_helper_cred_key_put((*cred).process_keyring);
        bindings::rust_helper_cred_key_put((*cred).thread_keyring);
        bindings::rust_helper_cred_key_put((*cred).request_key_auth);
    }
}

#[cfg(not(CONFIG_KEYS))]
unsafe fn release_key_refs(_cred: *mut Cred) {}

#[cfg(CONFIG_KEYS)]
/// Releases thread and process keyring references while preparing exec.
///
/// # Safety
///
/// `cred` must be an uncommitted credential that owns these references.
unsafe fn prepare_exec_keyrings(cred: *mut Cred) {
    // SAFETY: The caller owns both keyring references.
    unsafe {
        bindings::rust_helper_cred_key_put((*cred).thread_keyring);
        (*cred).thread_keyring = ptr::null_mut();
        bindings::rust_helper_cred_key_put((*cred).process_keyring);
        (*cred).process_keyring = ptr::null_mut();
    }
}

#[cfg(not(CONFIG_KEYS))]
unsafe fn prepare_exec_keyrings(_cred: *mut Cred) {}

#[cfg(CONFIG_KEYS)]
/// Clears inherited keyrings for a kernel credential.
///
/// # Safety
///
/// `cred` must be an uncommitted copy whose inherited keyring pointers were
/// not referenced for the copy.
unsafe fn clear_kernel_keyrings(cred: *mut Cred) {
    // SAFETY: The caller owns the complete uncommitted credential.
    unsafe {
        (*cred).session_keyring = ptr::null_mut();
        (*cred).process_keyring = ptr::null_mut();
        (*cred).thread_keyring = ptr::null_mut();
        (*cred).request_key_auth = ptr::null_mut();
        (*cred).jit_keyring = bindings::rust_helper_cred_default_jit_keyring();
    }
}

#[cfg(not(CONFIG_KEYS))]
unsafe fn clear_kernel_keyrings(_cred: *mut Cred) {}

#[cfg(CONFIG_KEYS)]
fn has_thread_keyring(cred: *const Cred) -> bool {
    // SAFETY: The caller guarantees a live credential pointer.
    unsafe { !(*cred).thread_keyring.is_null() }
}

#[cfg(not(CONFIG_KEYS))]
fn has_thread_keyring(_cred: *const Cred) -> bool {
    false
}

#[cfg(CONFIG_KEYS)]
/// Applies fork keyring inheritance rules to uncommitted credentials.
///
/// # Safety
///
/// `cred` must own the keyring references copied by `prepare_creds()`.
unsafe fn prepare_fork_keyrings(cred: *mut Cred, clone_thread: bool) {
    // SAFETY: The caller owns the uncommitted credential references.
    unsafe {
        if !(*cred).thread_keyring.is_null() {
            bindings::rust_helper_cred_key_put((*cred).thread_keyring);
            (*cred).thread_keyring = ptr::null_mut();
            if clone_thread {
                bindings::rust_helper_cred_install_thread_keyring(cred);
            }
        }

        if !clone_thread {
            bindings::rust_helper_cred_key_put((*cred).process_keyring);
            (*cred).process_keyring = ptr::null_mut();
        }
    }
}

#[cfg(not(CONFIG_KEYS))]
unsafe fn prepare_fork_keyrings(_cred: *mut Cred, _clone_thread: bool) {}

#[cfg(CONFIG_SECURITY)]
/// Clears the copied LSM pointer before preparing new security state.
///
/// # Safety
///
/// `cred` must be an uncommitted credential copy.
unsafe fn clear_security(cred: *mut Cred) {
    // SAFETY: The caller owns the complete uncommitted credential.
    unsafe { (*cred).security = ptr::null_mut() };
}

#[cfg(not(CONFIG_SECURITY))]
unsafe fn clear_security(_cred: *mut Cred) {}

/// Releases every resource owned by a final credential.
///
/// # Safety
///
/// `cred` must have zero references and must no longer be published by a task.
unsafe fn put_cred_rcu(cred: *mut Cred) {
    // SAFETY: The caller guarantees the final-release contract.
    let usage = unsafe { bindings::rust_helper_cred_usage_read(cred) };
    if usage != 0 {
        // SAFETY: This preserves the original fatal invariant.
        unsafe { bindings::rust_helper_cred_invariant_failed() };
    }

    // SAFETY: The final reference is gone and the credential remains live.
    unsafe { bindings::rust_helper_cred_security_free(cred) };
    // SAFETY: The final credential owns all keyring references.
    unsafe { release_key_refs(cred) };
    // SAFETY: The final credential owns its group and user references.
    unsafe {
        bindings::rust_helper_cred_group_put((*cred).group_info);
        bindings::rust_helper_cred_user_put((*cred).user);
        bindings::rust_helper_cred_ucounts_put((*cred).ucounts);
        bindings::rust_helper_cred_user_ns_put((*cred).user_ns);
        bindings::rust_helper_cred_free(cred);
    }
    record_release();
}

/// RCU callback for a credential queued by `__put_cred()`.
///
/// # Safety
///
/// `head` must be the RCU head embedded in a credential with zero references.
unsafe extern "C" fn put_cred_rcu_callback(head: *mut bindings::callback_head) {
    // SAFETY: The RCU head is embedded at the credential's union offset.
    let cred = unsafe {
        head.cast::<u8>()
            .sub(offset_of!(Cred, __bindgen_anon_1))
            .cast::<Cred>()
    };
    // SAFETY: The RCU callback transfers final-release ownership.
    unsafe { put_cred_rcu(cred) };
}

/// Destroys a credential set with no remaining references.
///
/// # Safety
///
/// The caller must own the final reference and the credential must not be the
/// current subjective or objective credential.
#[export]
unsafe extern "C" fn __put_cred(cred: *mut Cred) {
    record_entry();

    if cred.is_null() {
        // SAFETY: This preserves the original fatal invariant.
        unsafe { bindings::rust_helper_cred_invariant_failed() };
    }

    // SAFETY: The caller guarantees a live final credential.
    let current = unsafe { bindings::rust_helper_cred_current() };
    // SAFETY: The caller guarantees a live final credential and current task.
    let in_use = unsafe {
        if (*current).cred.cast_mut() == cred || (*current).real_cred.cast_mut() == cred {
            true
        } else {
            bindings::rust_helper_cred_usage_read(cred) != 0
        }
    };
    if in_use {
        // SAFETY: This preserves the original fatal invariants.
        unsafe { bindings::rust_helper_cred_invariant_failed() };
    }

    // SAFETY: The non-RCU flag selects direct final release.
    if unsafe { bindings::rust_helper_cred_non_rcu(cred) } {
        // SAFETY: The caller guarantees the final-release contract.
        unsafe { put_cred_rcu(cred) };
        return;
    }

    // SAFETY: The embedded RCU head is unused and final release is deferred.
    unsafe {
        bindings::call_rcu(
            ptr::addr_of_mut!((*cred).__bindgen_anon_1.rcu),
            Some(put_cred_rcu_callback),
        )
    };
}

/// Releases the credentials held by an exiting task.
///
/// # Safety
///
/// `task` must be exiting and must not race with another exit-credential call.
#[export]
unsafe extern "C" fn exit_creds(task: *mut Task) {
    record_exit();

    // SAFETY: The caller guarantees a live exiting task.
    let (real_cred, cred) = unsafe {
        let real_cred = (*task).real_cred;
        let cred = (*task).cred;
        (*task).real_cred = ptr::null();
        (*task).cred = ptr::null();
        (real_cred, cred)
    };

    if real_cred == cred {
        // SAFETY: The two task pointers owned two references to this credential.
        unsafe { put_many(cred, 2) };
    } else {
        // SAFETY: The task owned one reference through each pointer.
        unsafe {
            put_many(real_cred, 1);
            put_many(cred, 1);
        }
    }

    // SAFETY: The task is exiting and owns the cached requested key.
    unsafe { bindings::rust_helper_cred_exit_keys(task) };
}

/// Gets another task's objective credentials.
///
/// # Safety
///
/// The task must remain live through a task reference or tasklist lock.
unsafe fn get_task_cred_inner(task: *mut Task) -> *const Cred {
    // SAFETY: The caller guarantees the task lifetime.
    unsafe { bindings::rust_helper_cred_rcu_read_lock() };

    loop {
        // SAFETY: The task lifetime contract makes the RCU pointer stable
        // across a successful reference acquisition.
        let cred = unsafe { bindings::rust_helper_cred_task_objective(task) };
        if cred.is_null() {
            // SAFETY: The original implementation treats this as fatal.
            unsafe { bindings::rust_helper_cred_invariant_failed() };
        }

        // SAFETY: The RCU read lock protects the credential storage.
        if unsafe { get_rcu(cred) } {
            // SAFETY: This balances the RCU read lock above.
            unsafe { bindings::rust_helper_cred_rcu_read_unlock() };
            return cred;
        }
    }
}

/// Gets another task's objective credentials.
///
/// # Safety
///
/// The task must remain live through a task reference or tasklist lock.
#[export]
unsafe extern "C" fn get_task_cred(task: *mut Task) -> *const Cred {
    // SAFETY: The caller upholds the task lifetime contract.
    unsafe { get_task_cred_inner(task) }
}

/// Allocates blank credentials for later completion.
///
/// # Safety
///
/// This function has the original `cred_alloc_blank()` context requirements.
#[export]
unsafe extern "C" fn cred_alloc_blank() -> *mut Cred {
    record_entry();

    let new = if injected_failure(FAIL_BLANK_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The allocation helper uses the initialized credential cache.
        unsafe { bindings::rust_helper_cred_alloc_zero(bindings::GFP_KERNEL) }
    };
    if new.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The allocation produced writable zeroed credential storage.
    unsafe { bindings::rust_helper_cred_usage_set(new, 1) };
    let security_ret = if injected_failure(FAIL_SECURITY_BLANK) {
        -(bindings::ENOMEM as c_int)
    } else {
        // SAFETY: A non-NULL allocation remains live for LSM initialization.
        unsafe { bindings::rust_helper_cred_security_alloc_blank(new) }
    };
    if security_ret < 0 {
        // SAFETY: The new credential owns its sole reference.
        unsafe { abort_creds(new) };
        return ptr::null_mut();
    }

    new
}

/// Prepares a copy of the current task's subjective credentials.
///
/// # Safety
///
/// This function has the original `prepare_creds()` context requirements.
unsafe fn prepare_creds_inner() -> *mut Cred {
    record_prepare();

    let new = if injected_failure(FAIL_CRED_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The allocation helper uses the initialized credential cache.
        unsafe { bindings::rust_helper_cred_alloc(bindings::GFP_KERNEL) }
    };
    if new.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: Current always has a live subjective credential.
    let task = unsafe { bindings::rust_helper_cred_current() };
    // SAFETY: Current owns a live subjective credential.
    let old = unsafe { (*task).cred };
    // SAFETY: Both credential records are valid and do not overlap.
    unsafe { ptr::copy_nonoverlapping(old, new, 1) };

    // SAFETY: The new credential is uncommitted and writable.
    unsafe {
        bindings::rust_helper_cred_clear_non_rcu(new);
        bindings::rust_helper_cred_usage_set(new, 1);
        acquire_common_refs(new);
        acquire_key_refs(new);
        clear_security(new);
    }

    let ucounts = if injected_failure(FAIL_UCOUNTS_GET) {
        ptr::null_mut()
    } else {
        // SAFETY: The copied ucounts pointer remains live while `old` is live.
        unsafe { bindings::rust_helper_cred_ucounts_get((*new).ucounts) }
    };
    // SAFETY: The new credential remains live for this assignment.
    unsafe { (*new).ucounts = ucounts };
    if ucounts.is_null() {
        // SAFETY: The new credential owns its sole reference.
        unsafe { abort_creds(new) };
        return ptr::null_mut();
    }

    let security_ret = if injected_failure(FAIL_SECURITY_PREPARE) {
        -(bindings::ENOMEM as c_int)
    } else {
        // SAFETY: `old` remains live and `new` owns all dependency references
        // it needs for the LSM callback.
        unsafe { bindings::rust_helper_cred_security_prepare(new, old) }
    };
    if security_ret < 0 {
        // SAFETY: The new credential owns its sole reference.
        unsafe { abort_creds(new) };
        return ptr::null_mut();
    }

    new
}

/// Prepares a copy of the current task's subjective credentials.
///
/// # Safety
///
/// This function has the original `prepare_creds()` context requirements.
#[export]
unsafe extern "C" fn prepare_creds() -> *mut Cred {
    // SAFETY: The caller upholds the original context requirements.
    unsafe { prepare_creds_inner() }
}

/// Prepares credentials for an exec transition.
///
/// # Safety
///
/// The caller must hold `cred_guard_mutex`.
#[export]
unsafe extern "C" fn prepare_exec_creds() -> *mut Cred {
    // SAFETY: The caller upholds the exec credential requirements.
    let new = unsafe { prepare_creds_inner() };
    if new.is_null() {
        return new;
    }

    // SAFETY: The new credential is uncommitted and owns its key references.
    unsafe {
        prepare_exec_keyrings(new);
        (*new).suid = (*new).euid;
        (*new).fsuid = (*new).euid;
        (*new).sgid = (*new).egid;
        (*new).fsgid = (*new).egid;
    }

    new
}

/// Copies credentials for a task created by fork.
///
/// # Safety
///
/// `task` must be a new task whose credential pointers are initialized and not
/// yet visible to other tasks. The caller must satisfy the original
/// `copy_creds()` context requirements.
#[export]
unsafe extern "C" fn copy_creds(task: *mut Task, clone_flags: u64) -> c_int {
    record_fork();

    let clone_thread = clone_flags & u64::from(bindings::CLONE_THREAD) != 0;

    #[cfg(CONFIG_KEYS_REQUEST_CACHE)]
    // SAFETY: The new task owns its credential cache field.
    unsafe {
        (*task).cached_requested_key = ptr::null_mut();
    }

    // SAFETY: The new task has an initialized subjective credential.
    let inherited = unsafe { (*task).cred };
    if !has_thread_keyring(inherited) && clone_thread {
        // SAFETY: The inherited credential remains live through the parent.
        let shared = unsafe { get_many(inherited, 2) };
        // SAFETY: The new task is not yet visible to other tasks.
        unsafe {
            (*task).real_cred = shared;
            bindings::rust_helper_cred_inc_task_nproc(task);
            bindings::rust_helper_cred_get_namespaces(task);
        }
        return 0;
    }

    // SAFETY: The current parent satisfies the prepare requirements.
    let new = unsafe { prepare_creds_inner() };
    if new.is_null() {
        return -(bindings::ENOMEM as c_int);
    }

    if clone_flags & u64::from(bindings::CLONE_NEWUSER) != 0 {
        let ret = if injected_failure(FAIL_USER_NS) {
            -(bindings::ENOMEM as c_int)
        } else {
            // SAFETY: The new credential owns its sole reference and is
            // writable.
            unsafe { bindings::rust_helper_cred_create_user_ns(new) }
        };
        if ret < 0 {
            // SAFETY: The new credential owns its sole reference.
            unsafe { put_many(new, 1) };
            return ret;
        }

        // SAFETY: The new credential is uncommitted and writable.
        let ret = unsafe { set_cred_ucounts(new) };
        if ret < 0 {
            // SAFETY: The new credential owns its sole reference.
            unsafe { put_many(new, 1) };
            return ret;
        }
    }

    // SAFETY: The new credential owns the keyring references copied earlier.
    unsafe { prepare_fork_keyrings(new, clone_thread) };
    // SAFETY: The new credential acquires the task reference before publication.
    let shared = unsafe { get_many(new, 1) };
    // SAFETY: The task is not yet visible to other tasks.
    unsafe {
        bindings::rust_helper_cred_set_task_creds(task, shared);
        bindings::rust_helper_cred_inc_task_nproc(task);
        bindings::rust_helper_cred_get_namespaces(task);
    }

    0
}

/// Installs new credentials on the current task.
///
/// # Safety
///
/// The current task's subjective and objective credentials must be identical,
/// and the caller must own one reference to `new`.
#[export]
unsafe extern "C" fn commit_creds(new: *mut Cred) -> c_int {
    record_commit();

    // SAFETY: The caller guarantees the current credential state.
    let task = unsafe { bindings::rust_helper_cred_current() };
    // SAFETY: Current owns a live objective credential.
    let old = unsafe { (*task).real_cred };
    let old_mut = old.cast_mut();

    // SAFETY: The caller guarantees live current and new credentials.
    let invalid = unsafe {
        (*task).cred != old
            || bindings::rust_helper_cred_usage_read(new) < 1
            || old.is_null()
            || new.is_null()
    };
    if invalid {
        // SAFETY: This preserves the original fatal commit invariants.
        unsafe { bindings::rust_helper_cred_invariant_failed() };
    }

    // SAFETY: The caller owns one reference and commit adds the subjective
    // reference required for publication.
    unsafe { bindings::rust_helper_cred_get_many(new, 1) };

    // SAFETY: The caller guarantees live credentials and current task.
    if unsafe { changes_dumpability(old, new) } {
        // SAFETY: The current task remains live through credential commit.
        unsafe {
            bindings::rust_helper_cred_apply_dumpability(task);
            (*task).pdeath_signal = 0;
            bindings::rust_helper_cred_apply_write_barrier();
        }
    }

    // SAFETY: Both credentials remain live and writable through the commit.
    unsafe {
        let (old, new) = (&*old, &*new);
        if !uid_eq(new.fsuid, old.fsuid) {
            bindings::rust_helper_cred_key_fsuid_changed(ptr::from_ref(new).cast_mut());
        }
        if !gid_eq(new.fsgid, old.fsgid) {
            bindings::rust_helper_cred_key_fsgid_changed(ptr::from_ref(new).cast_mut());
        }

        if new.user != old.user || new.user_ns != old.user_ns {
            bindings::inc_rlimit_ucounts(new.ucounts, RLIMIT_NPROC, 1);
        }

        bindings::rust_helper_cred_publish_current(task, ptr::from_ref(new).cast_mut());

        if new.user != old.user || new.user_ns != old.user_ns {
            bindings::dec_rlimit_ucounts(old.ucounts, RLIMIT_NPROC, 1);
        }
        if new.user_ns != old.user_ns {
            bindings::rust_helper_cred_switch_namespaces(old, new);
        }

        if changes_uid(old, new) {
            bindings::rust_helper_cred_proc_uid_changed(task);
        }
        if changes_gid(old, new) {
            bindings::rust_helper_cred_proc_gid_changed(task);
        }
    }

    // SAFETY: The current task owned the objective and subjective references.
    unsafe { put_many(old_mut, 2) };
    0
}

/// Discards credentials that were being prepared.
///
/// # Safety
///
/// The caller must own a positive reference to `cred`.
#[export]
unsafe extern "C" fn abort_creds(cred: *mut Cred) {
    // SAFETY: The caller guarantees a live credential with a positive count.
    if unsafe { bindings::rust_helper_cred_usage_read(cred) } < 1 {
        // SAFETY: This preserves the original fatal abort invariant.
        unsafe { bindings::rust_helper_cred_invariant_failed() };
    }

    // SAFETY: The caller transfers its preparation reference.
    unsafe { put_many(cred, 1) };
}

/// Compares credentials by filesystem identity and supplementary groups.
///
/// # Safety
///
/// Both pointers must refer to live credential records.
#[export]
unsafe extern "C" fn cred_fscmp(left: *const Cred, right: *const Cred) -> c_int {
    if left == right {
        return 0;
    }

    // SAFETY: The caller provides live credential records.
    let (left, right) = unsafe { (&*left, &*right) };
    if left.fsuid.val < right.fsuid.val {
        return -1;
    }
    if left.fsuid.val > right.fsuid.val {
        return 1;
    }
    if left.fsgid.val < right.fsgid.val {
        return -1;
    }
    if left.fsgid.val > right.fsgid.val {
        return 1;
    }

    if left.group_info == right.group_info {
        return 0;
    }
    if left.group_info.is_null() {
        return -1;
    }
    if right.group_info.is_null() {
        return 1;
    }

    // SAFETY: Both group records are live through their credentials.
    let left_groups = unsafe { &*left.group_info };
    // SAFETY: Both group records are live through their credentials.
    let right_groups = unsafe { &*right.group_info };
    if left_groups.ngroups < right_groups.ngroups {
        return -1;
    }
    if left_groups.ngroups > right_groups.ngroups {
        return 1;
    }

    let mut index = 0;
    while index < left_groups.ngroups {
        // SAFETY: Both group records have the same live group count.
        let left_gid = unsafe { bindings::rust_helper_cred_group_gid(left.group_info, index) };
        // SAFETY: Both group records have the same live group count.
        let right_gid = unsafe { bindings::rust_helper_cred_group_gid(right.group_info, index) };
        if left_gid < right_gid {
            return -1;
        }
        if left_gid > right_gid {
            return 1;
        }
        index += 1;
    }

    0
}

/// Replaces a credential's ucounts subscription with one for its user and
/// user namespace.
///
/// # Safety
///
/// `cred` must be an uncommitted credential that owns its current ucounts
/// reference.
#[export]
unsafe extern "C" fn set_cred_ucounts(cred: *mut Cred) -> c_int {
    // SAFETY: The caller owns a live uncommitted credential.
    let old = unsafe { (*cred).ucounts };
    // SAFETY: The old ucounts reference remains live for comparison.
    let unchanged = unsafe {
        if old.is_null() {
            false
        } else {
            (*old).ns == (*cred).user_ns && uid_eq((*old).uid, (*cred).uid)
        }
    };
    if unchanged {
        return 0;
    }

    let new = if injected_failure(FAIL_UCOUNTS_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The credential identifies the target namespace and UID.
        unsafe { bindings::alloc_ucounts((*cred).user_ns, (*cred).uid) }
    };
    if new.is_null() {
        return -(bindings::EAGAIN as c_int);
    }

    // SAFETY: The new ucounts reference replaces the old one.
    unsafe {
        (*cred).ucounts = new;
        bindings::rust_helper_cred_ucounts_put(old);
    }
    0
}

/// Prepares a set of credentials for a kernel service.
///
/// # Safety
///
/// `daemon` must be a live task. The caller must satisfy the original
/// `prepare_kernel_cred()` context requirements.
#[export]
unsafe extern "C" fn prepare_kernel_cred(daemon: *mut Task) -> *mut Cred {
    record_entry();

    if daemon.is_null() {
        // SAFETY: This preserves the original warning.
        unsafe { bindings::rust_helper_cred_warn_on_once(true) };
        return ptr::null_mut();
    }

    let new = if injected_failure(FAIL_CRED_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The allocation helper uses the initialized credential cache.
        unsafe { bindings::rust_helper_cred_alloc(bindings::GFP_KERNEL) }
    };
    if new.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees a live daemon task.
    let old = unsafe { get_task_cred_inner(daemon) };
    if old.is_null() {
        // SAFETY: This consumes the allocation without a published reference.
        unsafe { bindings::rust_helper_cred_free(new) };
        return ptr::null_mut();
    }

    // SAFETY: Both credential records are valid and do not overlap.
    unsafe { ptr::copy_nonoverlapping(old, new, 1) };
    // SAFETY: The new credential is uncommitted and writable.
    unsafe {
        bindings::rust_helper_cred_clear_non_rcu(new);
        bindings::rust_helper_cred_usage_set(new, 1);
        acquire_common_refs(new);
        clear_kernel_keyrings(new);
        clear_security(new);
    }

    let ucounts = if injected_failure(FAIL_UCOUNTS_GET) {
        ptr::null_mut()
    } else {
        // SAFETY: The copied ucounts pointer remains live while `old` is live.
        unsafe { bindings::rust_helper_cred_ucounts_get((*new).ucounts) }
    };
    // SAFETY: The new credential remains live for this assignment.
    unsafe { (*new).ucounts = ucounts };
    if ucounts.is_null() {
        // SAFETY: The new credential owns its sole reference.
        unsafe {
            put_many(new, 1);
            put_many(old, 1);
        }
        return ptr::null_mut();
    }

    let security_ret = if injected_failure(FAIL_SECURITY_PREPARE) {
        -(bindings::ENOMEM as c_int)
    } else {
        // SAFETY: `old` remains live and `new` owns its dependency references.
        unsafe { bindings::rust_helper_cred_security_prepare(new, old) }
    };
    if security_ret < 0 {
        // SAFETY: The new credential and source each own one reference.
        unsafe {
            put_many(new, 1);
            put_many(old, 1);
        }
        return ptr::null_mut();
    }

    // SAFETY: The source reference is no longer needed.
    unsafe { put_many(old, 1) };
    new
}

/// Sets the LSM security ID in uncommitted credentials.
///
/// # Safety
///
/// `new` must be a live, writable credential prepared for modification.
#[export]
unsafe extern "C" fn set_security_override(new: *mut Cred, secid: u32) -> c_int {
    // SAFETY: The caller provides writable credential storage.
    unsafe { bindings::rust_helper_cred_security_act_as(new, secid) }
}

/// Takes the LSM file-creation identity from an inode.
///
/// # Safety
///
/// `new` must be writable and `inode` must remain live through the callback.
#[export]
unsafe extern "C" fn set_create_files_as(new: *mut Cred, inode: *mut Inode) -> c_int {
    // SAFETY: The caller provides a live inode.
    let (uid, gid) = unsafe { ((*inode).i_uid, (*inode).i_gid) };
    if uid.val == INVALID_ID || gid.val == INVALID_ID {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller provides a live writable credential and inode.
    unsafe {
        (*new).fsuid = uid;
        (*new).fsgid = gid;
        bindings::rust_helper_cred_security_create_files_as(new, inode)
    }
}
