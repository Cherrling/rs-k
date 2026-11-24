// SPDX-License-Identifier: GPL-2.0-only

//! Native mutex owner protocol.
//!
//! Rust owns the task-pointer encoding, fast acquisition and release,
//! contention trylock, and direct handoff state. C helpers retain atomic
//! operations, scheduler proxy state, intrusive wait lists, optimistic
//! spinning, task wakeups, lockdep, debugging, and public ABI adapters.

use crate::{
    bindings,
    ffi::{c_int, c_uint, c_ulong},
};
use core::ptr;
use macros::export;

type Mutex = bindings::mutex;
type Task = bindings::task_struct;
type WwMutex = bindings::ww_mutex;
type WwAcquireCtx = bindings::ww_acquire_ctx;
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
type LockClassKey = bindings::lock_class_key;
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
type LockdepMap = bindings::lockdep_map;

const FLAG_WAITERS: c_ulong = bindings::MUTEX_FLAG_WAITERS as c_ulong;
const FLAG_HANDOFF: c_ulong = bindings::MUTEX_FLAG_HANDOFF as c_ulong;
const FLAG_PICKUP: c_ulong = bindings::MUTEX_FLAG_PICKUP as c_ulong;
const FLAGS: c_ulong = bindings::MUTEX_FLAGS as c_ulong;

#[inline]
fn owner_task(owner: c_ulong) -> c_ulong {
    owner & !FLAGS
}

#[inline]
fn owner_flags(owner: c_ulong) -> c_ulong {
    owner & FLAGS
}

/// Reads the encoded owner.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
#[inline]
unsafe fn read_owner(lock: *mut Mutex) -> c_ulong {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_mutex_read_owner(lock) }
}

/// Attempts an acquire-ordered owner update.
///
/// # Safety
///
/// `lock` must point to a valid mutex and `owner` must be writable.
#[inline]
unsafe fn try_cmpxchg_acquire(lock: *mut Mutex, owner: &mut c_ulong, new: c_ulong) -> bool {
    // SAFETY: The caller guarantees validity of `lock` and `owner`.
    unsafe { bindings::rust_mutex_try_cmpxchg_owner_acquire(lock, owner, new) }
}

/// Attempts a release-ordered owner update.
///
/// # Safety
///
/// `lock` must point to a valid mutex and `owner` must be writable.
#[inline]
unsafe fn try_cmpxchg_release(lock: *mut Mutex, owner: &mut c_ulong, new: c_ulong) -> bool {
    // SAFETY: The caller guarantees validity of `lock` and `owner`.
    unsafe { bindings::rust_mutex_try_cmpxchg_owner_release(lock, owner, new) }
}

/// Returns the current task as an encoded-owner address.
///
/// # Safety
///
/// The caller must be in a valid kernel task context.
#[inline]
unsafe fn current() -> c_ulong {
    // SAFETY: The caller guarantees a current task.
    unsafe { bindings::rust_mutex_current() }
}

/// Runs the common contention trylock or handoff protocol.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
unsafe fn trylock_common(lock: *mut Mutex, handoff: bool) -> *mut Task {
    // SAFETY: The caller guarantees a current task.
    let curr = unsafe { current() };
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut owner = unsafe { read_owner(lock) };

    let failed_task = loop {
        let mut flags = owner_flags(owner);
        let mut task = owner_task(owner);

        if task != 0 {
            if flags & FLAG_PICKUP != 0 {
                if task != curr {
                    break task;
                }
                flags &= !FLAG_PICKUP;
            } else if handoff {
                if flags & FLAG_HANDOFF != 0 {
                    break task;
                }
                flags |= FLAG_HANDOFF;
            } else {
                break task;
            }
        } else {
            // SAFETY: The caller guarantees that `lock` is valid.
            unsafe { bindings::rust_mutex_warn_bad_trylock_flags(lock, flags) };
            task = curr;
        }

        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_acquire(lock, &mut owner, task | flags) } {
            if task == curr {
                return ptr::null_mut();
            }
            break task;
        }
    };

    failed_task as *mut Task
}

/// Transfers ownership to `task`, or unlocks when `task` is null.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task.
unsafe fn handoff(lock: *mut Mutex, task: *mut Task) {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut owner = unsafe { read_owner(lock) };

    loop {
        let mut new = owner & FLAG_WAITERS;
        new |= task as c_ulong;
        if !task.is_null() {
            new |= FLAG_PICKUP;
        }

        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_release(lock, &mut owner, new) } {
            break;
        }
    }
}

/// Decides whether a mutex owner is encoded as locked.
///
/// # Safety
///
/// `lock` must point to a valid mutex or be null.
#[export]
unsafe extern "C" fn rust_mutex_is_locked(lock: *mut Mutex) -> bool {
    if lock.is_null() {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    owner_task(unsafe { read_owner(lock) }) != 0
}

/// Returns the owning task address without protocol flags.
///
/// # Safety
///
/// `lock` must point to a valid mutex.
#[export]
unsafe extern "C" fn rust_mutex_get_owner(lock: *mut Mutex) -> c_ulong {
    // SAFETY: The caller guarantees that `lock` is valid.
    owner_task(unsafe { read_owner(lock) })
}

/// Attempts the uncontended acquisition fast path.
///
/// # Safety
///
/// `lock` must point to a valid mutex and the caller must be in a task.
#[export]
unsafe extern "C" fn rust_mutex_trylock_fast(lock: *mut Mutex) -> bool {
    // SAFETY: The caller guarantees a current task.
    let curr = unsafe { current() };
    let mut expected = 0;
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_acquire(lock, &mut expected, curr) }
}

/// Attempts the uncontended release fast path.
///
/// # Safety
///
/// `lock` must point to a valid mutex and be owned by the current task.
#[export]
unsafe extern "C" fn rust_mutex_unlock_fast(lock: *mut Mutex) -> bool {
    // SAFETY: The caller guarantees a current task.
    let mut expected = unsafe { current() };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { try_cmpxchg_release(lock, &mut expected, 0) }
}

/// Runs trylock or requests a direct handoff.
///
/// Returns null on success, otherwise the owner task address.
///
/// # Safety
///
/// `lock` must point to a valid mutex and the caller must be in a task.
#[export]
unsafe extern "C" fn rust_mutex_trylock_common(lock: *mut Mutex, handoff: bool) -> *mut Task {
    // SAFETY: The caller guarantees that `lock` is valid and a task exists.
    unsafe { trylock_common(lock, handoff) }
}

/// Prepares the slow unlock owner state.
///
/// Returns true when the mutex has been fully unlocked and the caller can
/// return without selecting or waking a waiter.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task, and `owner` must be
/// writable.
#[export]
unsafe extern "C" fn rust_mutex_unlock_prepare(lock: *mut Mutex, owner: *mut c_ulong) -> bool {
    // SAFETY: The caller guarantees that `lock` is valid.
    let mut observed = unsafe { read_owner(lock) };

    loop {
        // SAFETY: The caller guarantees that `lock` is valid and owned here.
        unsafe { bindings::rust_mutex_warn_bad_unlock(lock, observed) };
        // SAFETY: The scheduler helper reports whether proxy execution must
        // force a handoff.
        if unsafe { bindings::rust_mutex_force_handoff() } {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(FLAG_HANDOFF) };
            return false;
        }
        if observed & FLAG_HANDOFF != 0 {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(observed) };
            return false;
        }

        let new = owner_flags(observed) & FLAG_WAITERS;
        // SAFETY: `lock` and the local expected owner are valid.
        if unsafe { try_cmpxchg_release(lock, &mut observed, new) } {
            // SAFETY: The caller guarantees that `owner` is writable.
            unsafe { owner.write(observed) };
            return observed & FLAG_WAITERS == 0;
        }
    }
}

/// Applies a direct handoff when the prepared owner requested one.
///
/// # Safety
///
/// `lock` must be valid and owned by the current task.
#[export]
unsafe extern "C" fn rust_mutex_handoff_if_requested(
    lock: *mut Mutex,
    owner: c_ulong,
    task: *mut Task,
) -> bool {
    if owner & FLAG_HANDOFF == 0 {
        return false;
    }

    // SAFETY: The caller guarantees that `lock` is valid and owned here.
    unsafe { handoff(lock, task) };
    true
}

/// Runs a mutex trylock and applies the configured lockdep annotation.
///
/// # Safety
///
/// `lock` must be valid and the caller must be in a task.
unsafe fn trylock_with_lockdep(lock: *mut Mutex, _nest: *mut core::ffi::c_void) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid here.
    unsafe { bindings::rust_mutex_warn_bad_lock(lock) };
    // SAFETY: The caller guarantees that `lock` is valid and a task exists.
    let acquired = unsafe { trylock_common(lock, false) }.is_null();
    if acquired {
        #[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
        // SAFETY: The caller guarantees that `lock` and `nest` are valid.
        unsafe {
            bindings::rust_mutex_lockdep_acquire(lock, 0, true, _nest.cast())
        };
    }
    c_int::from(acquired)
}

/// Runs the common mutex acquisition protocol.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping. `nest` must be
/// valid when it is non-null.
unsafe fn lock_common(
    lock: *mut Mutex,
    state: c_uint,
    _subclass: c_uint,
    _nest: *mut core::ffi::c_void,
    ww_ctx: *mut WwAcquireCtx,
    use_ww_ctx: bool,
) -> c_int {
    let ww_ctx = if use_ww_ctx { ww_ctx } else { ptr::null_mut() };
    let ww = lock.cast::<WwMutex>();

    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe { bindings::rust_mutex_might_sleep() };
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_mutex_warn_bad_lock(lock) };

    if !ww_ctx.is_null() {
        // SAFETY: The caller guarantees that an acquire context is valid.
        if ww_ctx == unsafe { (*ww).ctx } {
            return -(bindings::EALREADY as c_int);
        }

        // SAFETY: The caller guarantees that the acquire context is valid.
        if unsafe { (*ww_ctx).acquired } == 0 {
            // SAFETY: The caller guarantees that the acquire context is valid.
            unsafe { (*ww_ctx).wounded = 0 };
        }

        #[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
        {
            // SAFETY: The caller guarantees that the acquire context is valid.
            let nest: *mut LockdepMap = unsafe { ptr::addr_of_mut!((*ww_ctx).dep_map) };
            // SAFETY: The caller guarantees that `lock` and `nest` are valid.
            unsafe { bindings::rust_mutex_lockdep_acquire(lock, _subclass, false, nest) };
        }
    }

    #[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
    {
        if ww_ctx.is_null() {
            // SAFETY: The caller guarantees that `lock` and `nest` are valid.
            unsafe { bindings::rust_mutex_lockdep_acquire(lock, _subclass, false, _nest.cast()) };
        }
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_mutex_preempt_disable() };
    // SAFETY: Preemption is disabled and `lock` is valid.
    unsafe { bindings::rust_mutex_contention_begin_spin(lock) };

    // SAFETY: The caller guarantees that `lock` is valid and a task exists.
    let contended = !unsafe { trylock_common(lock, false) }.is_null();
    let ret = if contended {
        // SAFETY: Preemption is disabled and the fast path observed
        // contention. The helper leaves preemption disabled.
        unsafe { bindings::rust_mutex_lock_slow(lock, state, ww_ctx, use_ww_ctx) }
    } else {
        // SAFETY: The caller guarantees that `lock` is valid.
        unsafe { bindings::rust_mutex_lock_acquired(lock) };
        if !ww_ctx.is_null() {
            // SAFETY: The fast path acquired the lock and installed the owner.
            unsafe { bindings::rust_ww_mutex_set_context_fastpath(ww, ww_ctx) };
        }
        // SAFETY: The lock is now held.
        unsafe { bindings::rust_mutex_contention_end(lock, 0) };
        0
    };

    // SAFETY: This function disabled preemption above.
    unsafe { bindings::rust_mutex_preempt_enable() };
    ret
}

/// Runs the common write-lock acquisition for a wound/wait mutex.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
unsafe fn ww_lock_common(lock: *mut WwMutex, ctx: *mut WwAcquireCtx, state: c_uint) -> c_int {
    // SAFETY: `base` is the first field of `ww_mutex`.
    let base = unsafe { ptr::addr_of_mut!((*lock).base) };
    // SAFETY: The caller guarantees that the lock and context are valid.
    let ret = unsafe { lock_common(base, state, 0, ptr::null_mut(), ctx, true) };
    #[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
    if ret == 0 && !ctx.is_null() {
        // SAFETY: The caller guarantees that the acquire context is valid.
        if unsafe { (*ctx).acquired } > 1 {
            // SAFETY: The lock was acquired under the valid context.
            return unsafe { bindings::rust_ww_mutex_deadlock_injection(lock, ctx) };
        }
    }
    ret
}

/// Initializes a mutex without lockdep state.
///
/// # Safety
///
/// `lock` must point to writable mutex storage.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
unsafe extern "C" fn mutex_init_generic(lock: *mut Mutex) {
    // SAFETY: The caller guarantees writable mutex storage.
    unsafe { bindings::rust_mutex_init_generic(lock) };
}

/// Initializes a mutex and its lockdep state.
///
/// # Safety
///
/// `lock` must point to writable mutex storage.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
unsafe extern "C" fn mutex_init_lockdep(
    lock: *mut Mutex,
    name: *const crate::ffi::c_char,
    key: *mut LockClassKey,
) {
    // SAFETY: The caller guarantees writable mutex storage.
    unsafe { bindings::rust_mutex_init_lockdep(lock, name, key) };
}

/// Reports whether a mutex is locked.
///
/// # Safety
///
/// `lock` must be valid or null.
#[export]
unsafe extern "C" fn mutex_is_locked(lock: *mut Mutex) -> bool {
    // SAFETY: The caller permits a null pointer.
    unsafe { rust_mutex_is_locked(lock) }
}

/// Returns the encoded owner task without protocol flags.
///
/// # Safety
///
/// `lock` must be valid.
#[export]
unsafe extern "C" fn mutex_get_owner(lock: *mut Mutex) -> c_ulong {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { rust_mutex_get_owner(lock) }
}

/// Acquires a mutex.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock(lock: *mut Mutex) {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_UNINTERRUPTIBLE,
            0,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    };
}

/// Acquires a mutex unless interrupted.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_interruptible(lock: *mut Mutex) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_INTERRUPTIBLE,
            0,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    }
}

/// Acquires a mutex unless killed.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_killable(lock: *mut Mutex) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_KILLABLE,
            0,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    }
}

/// Acquires a mutex while accounting the wait as I/O.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_io(lock: *mut Mutex) {
    // SAFETY: The caller guarantees that sleeping is permitted.
    let token = unsafe { bindings::io_schedule_prepare() };
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe { mutex_lock(lock) };
    // SAFETY: The token was returned by `io_schedule_prepare`.
    unsafe { bindings::io_schedule_finish(token) };
}

/// Tries to acquire a mutex without waiting.
///
/// # Safety
///
/// `lock` must be valid.
#[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_trylock(lock: *mut Mutex) -> c_int {
    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { trylock_with_lockdep(lock, ptr::null_mut()) }
}

/// Releases a mutex.
///
/// # Safety
///
/// `lock` must be owned by the caller.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_unlock(lock: *mut Mutex) {
    #[cfg(not(CONFIG_DEBUG_LOCK_ALLOC))]
    // SAFETY: The caller guarantees that `lock` is owned by this task.
    if unsafe { rust_mutex_unlock_fast(lock) } {
        return;
    }

    // SAFETY: The caller guarantees that `lock` is owned by this task.
    unsafe { bindings::rust_mutex_unlock_slow(lock) };
}

/// Acquires a mutex with a lockdep subclass.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_nested(lock: *mut Mutex, subclass: c_uint) {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_UNINTERRUPTIBLE,
            subclass,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    };
}

/// Acquires a mutex in the order of a nested lockdep map.
///
/// # Safety
///
/// `lock` and `nest` must be valid and sleeping must be permitted.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn _mutex_lock_nest_lock(lock: *mut Mutex, nest: *mut LockdepMap) {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_UNINTERRUPTIBLE,
            0,
            nest.cast(),
            ptr::null_mut(),
            false,
        )
    };
}

/// Acquires a mutex unless killed, with lockdep state.
///
/// # Safety
///
/// `lock` and `nest` must be valid and sleeping must be permitted.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn _mutex_lock_killable(
    lock: *mut Mutex,
    subclass: c_uint,
    nest: *mut LockdepMap,
) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_KILLABLE,
            subclass,
            nest.cast(),
            ptr::null_mut(),
            false,
        )
    }
}

/// Acquires a mutex unless interrupted, with a lockdep subclass.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_interruptible_nested(lock: *mut Mutex, subclass: c_uint) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_INTERRUPTIBLE,
            subclass,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    }
}

/// Acquires a mutex while accounting the wait as I/O, with lockdep state.
///
/// # Safety
///
/// `lock` must be valid and the caller must permit sleeping.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn mutex_lock_io_nested(lock: *mut Mutex, subclass: c_uint) {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe { bindings::rust_mutex_might_sleep() };
    // SAFETY: The caller guarantees that sleeping is permitted.
    let token = unsafe { bindings::io_schedule_prepare() };
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe {
        lock_common(
            lock,
            bindings::TASK_UNINTERRUPTIBLE,
            subclass,
            ptr::null_mut(),
            ptr::null_mut(),
            false,
        )
    };
    // SAFETY: The token was returned by `io_schedule_prepare`.
    unsafe { bindings::io_schedule_finish(token) };
}

/// Tries to acquire a mutex with a nested lockdep map.
///
/// # Safety
///
/// `lock` and `nest` must be valid.
#[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn _mutex_trylock_nest_lock(lock: *mut Mutex, nest: *mut LockdepMap) -> c_int {
    // SAFETY: The caller guarantees that `lock` and `nest` are valid.
    unsafe { trylock_with_lockdep(lock, nest.cast()) }
}

/// Tries to acquire a wound/wait mutex.
///
/// # Safety
///
/// `lock` and `ctx` must be valid when `ctx` is non-null.
#[export]
unsafe extern "C" fn ww_mutex_trylock(lock: *mut WwMutex, ctx: *mut WwAcquireCtx) -> c_int {
    // SAFETY: `base` is the first field of `ww_mutex`.
    let base = unsafe { ptr::addr_of_mut!((*lock).base) };
    if ctx.is_null() {
        // SAFETY: The caller guarantees that `lock` is valid.
        return unsafe { trylock_with_lockdep(base, ptr::null_mut()) };
    }

    // SAFETY: The caller guarantees that `lock` is valid.
    unsafe { bindings::rust_mutex_warn_bad_ww_lock(lock) };
    // SAFETY: The caller guarantees that the acquire context is valid.
    if unsafe { (*ctx).acquired } == 0 {
        // SAFETY: The caller guarantees that the acquire context is valid.
        unsafe { (*ctx).wounded = 0 };
    }

    // SAFETY: The caller guarantees that the lock is valid.
    if unsafe { trylock_common(base, false) }.is_null() {
        // SAFETY: The fast path acquired the lock.
        unsafe { bindings::rust_ww_mutex_set_context_fastpath(lock, ctx) };
        #[cfg(CONFIG_DEBUG_LOCK_ALLOC)]
        // SAFETY: The caller guarantees that the acquire context is valid.
        unsafe {
            bindings::rust_mutex_lockdep_acquire(base, 0, true, ptr::addr_of_mut!((*ctx).dep_map))
        };
        1
    } else {
        0
    }
}

/// Releases a wound/wait mutex.
///
/// # Safety
///
/// `lock` must be owned by the caller.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn ww_mutex_unlock(lock: *mut WwMutex) {
    // SAFETY: The caller guarantees that `lock` is owned by this task.
    unsafe { bindings::rust_ww_mutex_unlock_context(lock) };
    // SAFETY: `base` is the first field of `ww_mutex`.
    let base = unsafe { ptr::addr_of_mut!((*lock).base) };
    // SAFETY: The caller guarantees that the base mutex is owned here.
    unsafe { mutex_unlock(base) };
}

/// Acquires a wound/wait mutex.
///
/// # Safety
///
/// `lock` must be valid, `ctx` must be valid when non-null, and sleeping must
/// be permitted.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn ww_mutex_lock(lock: *mut WwMutex, ctx: *mut WwAcquireCtx) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe { ww_lock_common(lock, ctx, bindings::TASK_UNINTERRUPTIBLE) }
}

/// Acquires a wound/wait mutex unless interrupted.
///
/// # Safety
///
/// `lock` must be valid, `ctx` must be valid when non-null, and sleeping must
/// be permitted.
#[export]
#[link_section = ".sched.text"]
unsafe extern "C" fn ww_mutex_lock_interruptible(
    lock: *mut WwMutex,
    ctx: *mut WwAcquireCtx,
) -> c_int {
    // SAFETY: The caller guarantees that sleeping is permitted.
    unsafe { ww_lock_common(lock, ctx, bindings::TASK_INTERRUPTIBLE) }
}
