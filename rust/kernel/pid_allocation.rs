// SPDX-License-Identifier: GPL-2.0

//! Native PID allocation state machine.
//!
//! Rust owns set_tid validation, namespace traversal, IDR preload retry
//! selection, identifier allocation, rollback, publication, and pidfs failure
//! handling.

use crate::bindings;
use core::ffi::c_int;
use macros::export;

type Pid = bindings::pid;
type PidNamespace = bindings::pid_namespace;

const MAX_LEVELS: usize = bindings::MAX_PID_NS_LEVEL as usize + 1;

fn errno(error: u32) -> c_int {
    error as c_int
}

fn pid_min(cursor: u32) -> c_int {
    if cursor > bindings::RESERVED_PIDS {
        bindings::RESERVED_PIDS as c_int
    } else {
        1
    }
}

fn map_idr_error(error: c_int, cyclic: bool) -> c_int {
    if error == -errno(bindings::ENOSPC) {
        if cyclic {
            -errno(bindings::EAGAIN)
        } else {
            -errno(bindings::EEXIST)
        }
    } else {
        error
    }
}

/// Returns the first cyclic PID candidate for an IDR cursor.
#[cfg(CONFIG_PID_ALLOCATION_KUNIT_TEST)]
#[export]
extern "C" fn rust_pid_alloc_test_pid_min(cursor: u32) -> c_int {
    pid_min(cursor)
}

/// Maps an IDR allocation error to the caller-visible PID error.
#[cfg(CONFIG_PID_ALLOCATION_KUNIT_TEST)]
#[export]
extern "C" fn rust_pid_alloc_test_map_idr_error(error: c_int, cyclic: u32) -> c_int {
    map_idr_error(error, cyclic != 0)
}

/// Frees an allocated but unpublished PID.
///
/// # Safety
///
/// `ns` and `pid` must describe a successful cache allocation. This function
/// consumes the PID cache storage.
unsafe fn abort(pid: *mut Pid, ns: *mut PidNamespace, retval: c_int) -> c_int {
    // SAFETY: The allocation path acquired this namespace reference.
    unsafe { bindings::rust_helper_pid_alloc_put_ns(ns) };
    // SAFETY: The PID was allocated from this namespace cache and is not
    // published.
    unsafe { bindings::rust_helper_pid_alloc_cache_free(pid, ns) };
    retval
}

/// Rolls back identifiers allocated before publication.
///
/// # Safety
///
/// `pidmap_lock` and the IDR preload section must be active. `pid` and `ns`
/// must satisfy [`abort`]. Every index from `first` through `level` must have
/// a successfully allocated identifier.
unsafe fn out_free(
    pid: *mut Pid,
    ns: *mut PidNamespace,
    level: isize,
    first: isize,
    retval: c_int,
) -> c_int {
    let mut index = first;

    while index <= level {
        // SAFETY: The caller guarantees that this identifier was allocated.
        let number = unsafe { bindings::rust_helper_pid_alloc_number(pid, index as u32) };
        // SAFETY: The number entry is live and still registered in its IDR.
        unsafe {
            bindings::rust_helper_pid_alloc_idr_remove((*number).ns, (*number).nr);
        }
        index += 1;
    }

    // SAFETY: This balances the lock acquired by the allocation path.
    unsafe { bindings::rust_helper_pidmap_unlock() };
    // SAFETY: This balances `rust_helper_pid_alloc_idr_preload()`.
    unsafe { bindings::rust_helper_pid_alloc_idr_preload_end() };
    // SAFETY: The caller passes the cache allocation and namespace reference.
    unsafe { abort(pid, ns, retval) }
}

/// Allocates one PID in a namespace hierarchy.
///
/// # Safety
///
/// `ns` must be live. If `arg_set_tid_size` is non-zero, `arg_set_tid` must
/// point to that many readable identifiers. `out_pid` must point to writable
/// storage and must not alias the input array.
#[export]
unsafe extern "C" fn rust_pid_alloc(
    ns: *mut PidNamespace,
    arg_set_tid: *mut i32,
    arg_set_tid_size: usize,
    out_pid: *mut *mut Pid,
) -> c_int {
    if out_pid.is_null() || ns.is_null() {
        return -errno(bindings::EINVAL);
    }
    // SAFETY: The caller guarantees writable output storage.
    unsafe { *out_pid = core::ptr::null_mut() };

    // SAFETY: The caller guarantees a live namespace.
    let level = unsafe { (*ns).level as usize };
    if arg_set_tid_size > level + 1 {
        return -errno(bindings::EINVAL);
    }
    if arg_set_tid_size != 0 && arg_set_tid.is_null() {
        return -errno(bindings::EINVAL);
    }

    // SAFETY: The namespace is live and owns the PID cache.
    let pid = unsafe { bindings::rust_helper_pid_alloc_cache_alloc(ns) };
    if pid.is_null() {
        return -errno(bindings::ENOMEM);
    }

    // SAFETY: `pid` is the new cache object and `ns` is live.
    unsafe {
        bindings::rust_helper_pid_alloc_get_ns(ns);
        (*pid).level = level as u32;
        bindings::refcount_set(&mut (*pid).count, 1);
        bindings::rust_helper_pid_alloc_init(pid);
    }

    let mut set_tid = [0_i32; MAX_LEVELS];
    let mut pid_max = [0_i32; MAX_LEVELS];
    let mut current = ns;
    let mut remaining = arg_set_tid_size;

    for slot in 0..=level {
        // SAFETY: Every namespace through `level` is live.
        pid_max[slot] = unsafe { bindings::rust_helper_pid_alloc_pid_max(current) };

        if remaining != 0 {
            // SAFETY: The caller supplies `arg_set_tid_size` readable entries.
            let tid = unsafe { *arg_set_tid.add(slot) };
            set_tid[slot] = tid;

            if tid < 1 || tid >= pid_max[slot] {
                // SAFETY: The cache allocation and namespace reference are live.
                return unsafe { abort(pid, ns, -errno(bindings::EINVAL)) };
            }
            // SAFETY: `current` is live for the complete namespace walk.
            if !unsafe { bindings::rust_helper_pid_alloc_capable(current) } {
                // SAFETY: The cache allocation and namespace reference are live.
                return unsafe { abort(pid, ns, -errno(bindings::EPERM)) };
            }
            remaining -= 1;
        }

        // SAFETY: Every non-root namespace records its parent.
        current = unsafe { (*current).parent };
    }

    let mut retried_preload = false;
    // SAFETY: This starts the matching IDR preload section.
    unsafe {
        bindings::rust_helper_pid_alloc_idr_preload();
        bindings::rust_helper_pidmap_lock();
    }

    // SAFETY: `pidmap_lock` serializes this namespace count.
    if unsafe { bindings::rust_helper_pid_alloc_allocated(ns) } == bindings::PIDNS_ADDING {
        // SAFETY: The namespace IDR is protected by `pidmap_lock`.
        unsafe { bindings::rust_helper_pid_alloc_idr_set_cursor(ns, 0) };
    }

    let mut index = level as isize;
    current = ns;

    while index >= 0 {
        let slot = (level as isize - index) as usize;
        let tid = set_tid[slot];
        let mut nr;

        if tid != 0 {
            // SAFETY: The namespace IDR is protected by `pidmap_lock`.
            nr = unsafe { bindings::rust_helper_pid_alloc_idr_alloc(current, tid, tid + 1) };
            nr = map_idr_error(nr, false);
        } else {
            // SAFETY: The namespace IDR is protected by `pidmap_lock`.
            let cursor = unsafe { bindings::rust_helper_pid_alloc_idr_cursor(current) };
            // SAFETY: The namespace IDR is protected by `pidmap_lock`.
            nr = unsafe {
                bindings::rust_helper_pid_alloc_idr_alloc_cyclic(
                    current,
                    pid_min(cursor),
                    pid_max[slot],
                )
            };
            nr = map_idr_error(nr, true);
        }

        if nr < 0 {
            if nr == -errno(bindings::ENOMEM) && !retried_preload {
                // SAFETY: This releases and restarts the preload section.
                unsafe {
                    bindings::rust_helper_pidmap_unlock();
                    bindings::rust_helper_pid_alloc_idr_preload_end();
                }
                retried_preload = true;
                // SAFETY: This restarts the matching preload section and lock.
                unsafe {
                    bindings::rust_helper_pid_alloc_idr_preload();
                    bindings::rust_helper_pidmap_lock();
                }
                continue;
            }

            // SAFETY: This removes every identifier allocated in this attempt.
            return unsafe { out_free(pid, ns, level as isize, index + 1, nr) };
        }

        // SAFETY: The identifier was allocated and the PID entry is writable.
        unsafe {
            bindings::rust_helper_pid_alloc_number_set(pid, index as u32, nr, current);
        }
        index -= 1;
        retried_preload = false;

        // SAFETY: The namespace remains live under the IDR preload section.
        if unsafe { bindings::rust_helper_pid_alloc_child_reaper_missing(current) } && nr != 1 {
            // SAFETY: This removes the identifier assigned above.
            return unsafe {
                out_free(pid, ns, level as isize, index + 1, -errno(bindings::EINVAL))
            };
        }

        // SAFETY: Every non-root namespace records its parent.
        current = unsafe { (*current).parent };
    }

    index = level as isize;
    while index >= 0 {
        // SAFETY: Every identifier through `level` was allocated above.
        let number = unsafe { bindings::rust_helper_pid_alloc_number(pid, index as u32) };
        // SAFETY: The namespace outlives its published PID entry.
        if unsafe { bindings::rust_helper_pid_alloc_disabled((*number).ns) } {
            // SAFETY: This removes every identifier allocated in this attempt.
            return unsafe { out_free(pid, ns, level as isize, 0, -errno(bindings::ENOMEM)) };
        }
        index -= 1;
    }

    index = level as isize;
    while index >= 0 {
        // SAFETY: Every identifier through `level` was allocated above.
        let number = unsafe { bindings::rust_helper_pid_alloc_number(pid, index as u32) };
        // SAFETY: Publication uses the same lock as allocation.
        unsafe {
            bindings::rust_helper_pid_alloc_idr_replace((*number).ns, pid, (*number).nr);
            bindings::rust_helper_pid_alloc_count_inc((*number).ns);
        }
        index -= 1;
    }

    // SAFETY: This balances the lock and preload section acquired above.
    unsafe {
        bindings::rust_helper_pidmap_unlock();
        bindings::rust_helper_pid_alloc_idr_preload_end();
        bindings::rust_helper_pid_alloc_active_get(ns);
    }

    // SAFETY: The PID is fully published and owns one reference.
    let retval = unsafe { bindings::rust_helper_pid_alloc_pidfs_add(pid) };
    if retval != 0 {
        // SAFETY: This consumes the published PID and defers its release.
        unsafe { bindings::free_pid(pid) };
        return -errno(bindings::ENOMEM);
    }

    // SAFETY: The caller guarantees writable output storage.
    unsafe { *out_pid = pid };
    0
}
