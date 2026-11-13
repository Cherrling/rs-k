// SPDX-License-Identifier: GPL-2.0

//! Native PID lifecycle release.
//!
//! Rust owns the final-reference decision, reverse PID array traversal, PID
//! namespace count transitions, child-reaper wake decisions, release ordering,
//! and the deferred final-reference callback.

use crate::bindings;
use core::mem::offset_of;
use macros::export;

type Pid = bindings::pid;
type PidNamespace = bindings::pid_namespace;
type Upid = bindings::upid;

const COUNT_ACTION_NONE: u32 = bindings::RUST_PID_COUNT_ACTION_NONE;
const COUNT_ACTION_WAKE: u32 = bindings::RUST_PID_COUNT_ACTION_WAKE;
const COUNT_ACTION_WARN: u32 = bindings::RUST_PID_COUNT_ACTION_WARN;

fn count_action(new_count: u32) -> u32 {
    match new_count {
        1 | 2 => COUNT_ACTION_WAKE,
        bindings::PIDNS_ADDING => COUNT_ACTION_WARN,
        _ => COUNT_ACTION_NONE,
    }
}

/// Returns the action selected for a namespace PID count.
#[cfg(CONFIG_PID_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_pid_test_count_action(new_count: u32) -> u32 {
    count_action(new_count)
}

/// Visits each non-NULL PID array entry from the highest type to the lowest.
///
/// # Safety
///
/// `pids` must point to an array of at least `PIDTYPE_MAX` live pointer slots
/// for the duration of this call.
unsafe fn visit_present_reverse<F>(pids: *mut *mut Pid, mut visit: F)
where
    F: FnMut(usize, *mut Pid),
{
    let mut index = bindings::pid_type_PIDTYPE_MAX as usize;

    while index != 0 {
        index -= 1;
        // SAFETY: The caller guarantees that the full PID array is valid.
        let pid = unsafe { *pids.add(index) };
        if !pid.is_null() {
            visit(index, pid);
        }
    }
}

/// Verifies the order used by the production release traversal.
///
/// # Safety
///
/// `pids` must point to a valid array of `PIDTYPE_MAX` pointer slots. `indices`
/// must point to `capacity` writable integers when `capacity` is non-zero.
#[cfg(CONFIG_PID_LIFECYCLE_KUNIT_TEST)]
#[export]
unsafe extern "C" fn rust_pid_test_visit_order(
    pids: *mut *mut Pid,
    indices: *mut u32,
    capacity: u32,
) -> u32 {
    let mut visited = 0;

    // SAFETY: The caller supplies the PID array and output storage described
    // by the function contract.
    unsafe {
        visit_present_reverse(pids, |index, _| {
            if !indices.is_null() && visited < capacity {
                *indices.add(visited as usize) = index as u32;
            }
            visited += 1;
        })
    };

    visited
}

/// Returns the namespace for the most specific PID number.
///
/// # Safety
///
/// `pid` must point to a live PID structure.
unsafe fn active_namespace(pid: *mut Pid) -> *mut PidNamespace {
    // SAFETY: The caller guarantees that the PID is live.
    unsafe { bindings::rust_helper_pid_active_namespace(pid) }
}

/// Returns one PID namespace number entry.
///
/// # Safety
///
/// `pid` must point to a live PID structure and `level` must not exceed its
/// recorded namespace level.
unsafe fn upid(pid: *mut Pid, level: u32) -> *mut Upid {
    // SAFETY: The caller guarantees that the level is valid.
    unsafe { bindings::rust_helper_pid_upid(pid, level) }
}

/// Decrements a namespace PID count and dispatches its transition.
///
/// # Safety
///
/// `ns` must be live and `pidmap_lock` must be held.
unsafe fn decrement_namespace_count(ns: *mut PidNamespace) {
    // SAFETY: The caller holds the lock that serializes this counter.
    let new_count = unsafe { (*ns).pid_allocated.wrapping_sub(1) };
    // SAFETY: The caller holds the lock that serializes this counter.
    unsafe { (*ns).pid_allocated = new_count };

    match count_action(new_count) {
        COUNT_ACTION_WAKE => {
            // SAFETY: `pidmap_lock` serializes the count and reaper pointer.
            unsafe { bindings::rust_helper_pid_wake_child_reaper(ns) };
        }
        COUNT_ACTION_WARN => {
            // SAFETY: This branch is the original PIDNS_ADDING invariant.
            unsafe { bindings::rust_helper_pid_warn_child_reaper(ns) };
        }
        _ => {}
    }
}

/// Releases one live PID after it is removed from its task.
///
/// # Safety
///
/// `pid` must be live and must no longer be associated with a task. The caller
/// must not hold `tasklist_lock`.
unsafe fn release(pid: *mut Pid) {
    // SAFETY: The caller satisfies the retained lock assertion.
    unsafe { bindings::rust_helper_pid_assert_tasklist_not_held() };

    // SAFETY: The PID is live and owns an active namespace reference.
    let active_ns = unsafe { active_namespace(pid) };
    // SAFETY: This releases the active reference owned by the PID.
    unsafe { bindings::rust_helper_pid_ns_active_put(active_ns) };

    // SAFETY: This lock protects the namespace PID counters and IDR entries.
    unsafe { bindings::rust_helper_pidmap_lock() };

    // SAFETY: The PID is live for the complete release operation.
    let level = unsafe { (*pid).level };
    let mut index = 0;

    while index <= level {
        // SAFETY: Every index through the recorded level has a namespace entry.
        let number = unsafe { upid(pid, index) };
        // SAFETY: This entry is live until the matching IDR removal below.
        let ns = unsafe { (*number).ns };

        // SAFETY: `pidmap_lock` serializes the namespace counter.
        unsafe { decrement_namespace_count(ns) };

        // SAFETY: The namespace entry was still registered in its IDR.
        unsafe { bindings::rust_helper_pid_idr_remove(ns, (*number).nr) };
        index += 1;
    }

    // SAFETY: This balances `rust_helper_pidmap_lock()` above.
    unsafe { bindings::rust_helper_pidmap_unlock() };

    // SAFETY: The PID is still live and its pidfs entry is no longer needed.
    unsafe { bindings::rust_helper_pidfs_remove(pid) };

    // SAFETY: The embedded RCU head belongs to this live PID and is unused.
    unsafe { bindings::rust_helper_pid_call_rcu(pid, Some(put_rcu)) };
}

/// Releases one PID reference.
///
/// # Safety
///
/// A non-NULL `pid` must be live and the caller must own one reference.
unsafe fn put(pid: *mut Pid) {
    if pid.is_null() {
        return;
    }

    // SAFETY: The caller owns a live PID reference.
    let ns = unsafe { active_namespace(pid) };
    // SAFETY: The caller owns the reference that this decrement releases.
    let final_reference = unsafe { bindings::refcount_dec_and_test(&mut (*pid).count) };

    if final_reference {
        // SAFETY: The final reference is gone and the PID storage is still live.
        unsafe { bindings::rust_helper_pidfs_free(pid) };
        // SAFETY: This consumes the PID storage after pidfs cleanup.
        unsafe { bindings::rust_helper_pid_cache_free(pid, ns) };
        // SAFETY: The namespace reference was acquired with the PID.
        unsafe { bindings::rust_helper_put_pid_ns(ns) };
    }
}

/// Completes a deferred PID release.
///
/// # Safety
///
/// `head` must point to the RCU head embedded in a live PID.
unsafe extern "C" fn put_rcu(head: *mut bindings::callback_head) {
    // SAFETY: The embedded RCU head is a member of the live PID object.
    let pid = unsafe { head.cast::<u8>().sub(offset_of!(Pid, rcu)).cast::<Pid>() };

    // SAFETY: The embedded callback head belongs to a live PID with one
    // deferred reference.
    unsafe { put(pid) };
}

/// Releases one PID reference through the C ABI.
///
/// # Safety
///
/// The same requirements as [`put`] apply.
#[export]
unsafe extern "C" fn rust_pid_put(pid: *mut Pid) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { put(pid) };
}

/// Releases one detached PID through the C ABI.
///
/// # Safety
///
/// The same requirements as [`release`] apply.
#[export]
unsafe extern "C" fn rust_pid_free(pid: *mut Pid) {
    // SAFETY: The caller upholds the function requirements.
    unsafe { release(pid) };
}

/// Releases each non-NULL PID in a PID type array through the C ABI.
///
/// # Safety
///
/// `pids` must point to an array of `PIDTYPE_MAX` live pointer slots. Each
/// non-NULL PID must satisfy the [`release`] requirements.
#[export]
unsafe extern "C" fn rust_pid_free_all(pids: *mut *mut Pid) {
    // SAFETY: The caller supplies the PID type array and owns every entry.
    unsafe {
        visit_present_reverse(pids, |_, pid| {
            release(pid);
        })
    };
}
