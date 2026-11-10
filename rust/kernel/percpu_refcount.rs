// SPDX-License-Identifier: GPL-2.0-only

//! Native lifecycle state machine for `percpu_ref()`.
//!
//! The per-CPU get/put fast paths remain inline C because they depend on
//! architecture-specific atomic and preemption primitives. This module owns
//! initialization, mode switching, RCU-assisted kill confirmation, exit,
//! reinitialization, and resurrection.

use crate::{bindings, ffi::c_long, ffi::c_ulong};
use core::{ffi::c_int, ffi::c_uint, ptr};
use macros::export;

type Ref = bindings::percpu_ref;
type Data = bindings::percpu_ref_data;
type Callback = bindings::percpu_ref_func_t;

const ATOMIC: c_ulong = bindings::__PERCPU_REF_ATOMIC as c_ulong;
const DEAD: c_ulong = bindings::__PERCPU_REF_DEAD as c_ulong;
const ATOMIC_DEAD: c_ulong = bindings::__PERCPU_REF_ATOMIC_DEAD as c_ulong;
const FLAG_BITS: c_uint = bindings::__PERCPU_REF_FLAG_BITS;
const INIT_ATOMIC: c_uint = bindings::PERCPU_REF_INIT_ATOMIC;
const INIT_DEAD: c_uint = bindings::PERCPU_REF_INIT_DEAD;
const ALLOW_REINIT: c_uint = bindings::PERCPU_REF_ALLOW_REINIT;
const COUNT_BIAS: c_ulong = 1 << (c_ulong::BITS - 1);

struct SwitchLock {
    flags: c_ulong,
}

impl SwitchLock {
    /// Acquires the global `percpu_ref` switch lock.
    ///
    /// # Safety
    ///
    /// The returned guard must remain in scope while the protected state is
    /// accessed.
    #[inline]
    unsafe fn acquire() -> Self {
        let flags = {
            // SAFETY: The C helper returns the interrupt flags associated with
            // the acquired global percpu_ref switch lock.
            unsafe { bindings::rust_percpu_ref_lock() }
        };
        Self { flags }
    }
}

impl Drop for SwitchLock {
    #[inline]
    fn drop(&mut self) {
        // SAFETY: This guard is the unique owner of the lock and stores the
        // flags returned when it was acquired.
        unsafe { bindings::rust_percpu_ref_unlock(self.flags) };
    }
}

/// Returns the per-CPU allocation address after removing the flag bits.
///
/// # Safety
///
/// `ref_` must point to an initialized `percpu_ref`.
#[inline]
unsafe fn percpu_count_ptr(ref_: *mut Ref) -> c_ulong {
    // SAFETY: The caller guarantees that `ref_` is initialized.
    unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) & !ATOMIC_DEAD }
}

/// Writes the encoded per-CPU allocation and flag word.
///
/// # Safety
///
/// `ref_` must point to a live, writable `percpu_ref`.
#[inline]
unsafe fn set_count_ptr(ref_: *mut Ref, value: c_ulong) {
    // SAFETY: The caller guarantees that `ref_` is initialized.
    unsafe { bindings::rust_percpu_ref_write_count_ptr(ref_, value) };
}

/// Reads the sticky atomic-mode flag.
///
/// # Safety
///
/// `data` must remain valid for the duration of the state transition.
#[inline]
unsafe fn force_atomic(data: *mut Data) -> bool {
    // SAFETY: The caller guarantees that `data` is valid for the duration of
    // the state transition.
    unsafe { Data::force_atomic_raw(data) }
}

/// Writes the sticky atomic-mode flag.
///
/// # Safety
///
/// `data` must remain valid for the duration of the state transition.
#[inline]
unsafe fn set_force_atomic(data: *mut Data, value: bool) {
    // SAFETY: The caller guarantees that `data` is valid for the duration of
    // the state transition.
    unsafe { Data::set_force_atomic_raw(data, value) };
}

/// Reads the reinitialization permission flag.
///
/// # Safety
///
/// `data` must remain valid for the duration of the state transition.
#[inline]
unsafe fn allow_reinit(data: *mut Data) -> bool {
    // SAFETY: The caller guarantees that `data` is valid for the duration of
    // the state transition.
    unsafe { Data::allow_reinit_raw(data) }
}

/// Writes the reinitialization permission flag.
///
/// # Safety
///
/// `data` must remain valid for the duration of the state transition.
#[inline]
unsafe fn set_allow_reinit(data: *mut Data, value: bool) {
    // SAFETY: The caller guarantees that `data` is valid for the duration of
    // the state transition.
    unsafe { Data::set_allow_reinit_raw(data, value) };
}

/// Calls a caller-provided `percpu_ref` callback.
///
/// # Safety
///
/// `callback` must contain a callable function.  `ref_` must satisfy the
/// callback contract.
#[inline]
unsafe fn call_callback(callback: Callback, ref_: *mut Ref) {
    // SAFETY: The caller guarantees that `callback` is present and callable
    // with `ref_`.
    unsafe { callback.unwrap_unchecked()(ref_) };
}

/// Implements the callback used when no confirmation callback is supplied.
///
/// # Safety
///
/// The caller passes a live `percpu_ref`, as required by the callback ABI.
unsafe extern "C" fn noop_confirm_switch(_ref: *mut Ref) {}

/// Releases the per-CPU allocation owned by a reference.
///
/// # Safety
///
/// `ref_` must be initialized, inactive, and free of an in-progress switch.
unsafe fn exit_inner(ref_: *mut Ref) {
    // SAFETY: The caller guarantees that `ref_` is initialized and not in use.
    let percpu = unsafe { percpu_count_ptr(ref_) };
    if percpu == 0 {
        return;
    }

    // SAFETY: `ref_` is still initialized at this point.
    unsafe { bindings::rust_percpu_ref_warn_confirm_switch(ref_) };
    // SAFETY: `percpu` is the allocation owned by `ref_`.
    unsafe { bindings::rust_percpu_ref_free_percpu(percpu) };
    // SAFETY: `ref_` is valid and the allocation has just been released.
    unsafe { set_count_ptr(ref_, ATOMIC_DEAD) };
}

/// Completes an RCU-assisted mode switch.
///
/// # Safety
///
/// `rcu` must be embedded in an initialized `percpu_ref_data`.  The
/// confirmation callback must remain valid through this invocation.
unsafe extern "C" fn call_confirm_rcu(rcu: *mut bindings::callback_head) {
    // SAFETY: The RCU callback receives the embedded callback head.
    let data = unsafe { bindings::rust_percpu_ref_data_from_rcu(rcu) };
    // SAFETY: The callback was armed only after `data` was initialized.
    let ref_ = unsafe { (*data).ref_ };
    // SAFETY: A non-null confirmation callback is the switch-in-progress
    // invariant.
    let confirm = unsafe { (*data).confirm_switch };
    // SAFETY: The non-null confirmation callback invariant holds.
    unsafe { call_callback(confirm, ref_) };
    // SAFETY: `data` remains live until the final put below.
    unsafe { (*data).confirm_switch = None };
    // SAFETY: The switch-completion helper only accesses its global wait queue.
    unsafe { bindings::rust_percpu_ref_wake_all() };

    // SAFETY: `data` remains live until the final put below.
    if !unsafe { allow_reinit(data) } {
        // SAFETY: `ref_` is visible as dead and the callback still owns a
        // reference that prevents final destruction.
        unsafe { exit_inner(ref_) };
    }

    // SAFETY: This drops the temporary reference acquired before scheduling.
    unsafe { bindings::rust_percpu_ref_put(ref_) };
}

/// Collects per-CPU counts and completes an atomic-mode switch.
///
/// # Safety
///
/// `rcu` must be the callback head armed by `switch_to_atomic_locked()`.
unsafe extern "C" fn switch_to_atomic_rcu(rcu: *mut bindings::callback_head) {
    // SAFETY: The RCU callback receives the embedded callback head.
    let data = unsafe { bindings::rust_percpu_ref_data_from_rcu(rcu) };
    // SAFETY: The callback was armed only after `data` was initialized.
    let ref_ = unsafe { (*data).ref_ };
    // SAFETY: Atomic mode implies the per-CPU allocation remains present.
    let percpu = unsafe { percpu_count_ptr(ref_) };
    // SAFETY: `percpu` is the valid allocation owned by `ref_`.
    let count = unsafe { bindings::rust_percpu_ref_sum_percpu(percpu) };

    // SAFETY: `data` is live until the final put below.
    unsafe { bindings::rust_percpu_ref_debug_counts(data, count) };
    // SAFETY: `data` is live and the bias adjustment preserves the refcount.
    unsafe { bindings::rust_percpu_ref_atomic_add(data, count.wrapping_sub(COUNT_BIAS) as c_long) };
    // SAFETY: `data` is live and remains valid when reporting an underflow.
    unsafe { bindings::rust_percpu_ref_warn_underflow(data) };

    // SAFETY: `rcu` is the callback head embedded in `data`.
    unsafe { call_confirm_rcu(rcu) };
}

/// Switches a reference to atomic mode while the caller holds the switch lock.
///
/// # Safety
///
/// The caller must hold the global switch lock.  `ref_` must remain live until
/// confirmation completes.
unsafe fn switch_to_atomic_locked(ref_: *mut Ref, confirm: Callback) {
    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    let count_ptr = unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) };
    if count_ptr & ATOMIC != 0 {
        if let Some(callback) = confirm {
            // SAFETY: The caller supplied a callable confirmation callback.
            unsafe { callback(ref_) };
        }
        return;
    }

    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    unsafe { set_count_ptr(ref_, count_ptr | ATOMIC) };
    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    let data = unsafe { (*ref_).data };
    // SAFETY: A live initialized ref always has backing data.
    unsafe {
        (*data).confirm_switch = confirm.or(Some(noop_confirm_switch));
    }

    // SAFETY: The temporary reference keeps `ref_` live until confirmation.
    unsafe { bindings::rust_percpu_ref_get(ref_) };
    // SAFETY: `data` is initialized and the callback uses its embedded head.
    unsafe {
        bindings::rust_percpu_ref_call_rcu_hurry(data, Some(switch_to_atomic_rcu));
    }
}

/// Switches a reference to per-CPU mode while the caller holds the switch lock.
///
/// # Safety
///
/// The caller must hold the global switch lock and `ref_` must be live.
unsafe fn switch_to_percpu_locked(ref_: *mut Ref) {
    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    let percpu = unsafe { percpu_count_ptr(ref_) };
    if percpu == 0 {
        // SAFETY: The caller violated a state-machine invariant.
        unsafe { bindings::rust_percpu_ref_warn_no_percpu() };
        return;
    }

    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    let count_ptr = unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) };
    if count_ptr & ATOMIC == 0 {
        return;
    }

    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    if unsafe { bindings::rust_percpu_ref_warn_no_reinit(ref_) } {
        return;
    }
    // SAFETY: The caller holds the global switch lock and `ref_` is live.
    let data = unsafe { (*ref_).data };
    // SAFETY: The caller holds the global switch lock and `data` is live.
    unsafe {
        bindings::rust_percpu_ref_atomic_add(data, COUNT_BIAS as c_long);
    }
    // SAFETY: `percpu` is the allocation owned by `ref_`.
    unsafe { bindings::rust_percpu_ref_zero_percpu(percpu) };
    // SAFETY: This release store is paired with the inline fast-path load.
    unsafe {
        bindings::rust_percpu_ref_store_release_count_ptr(ref_, count_ptr & !ATOMIC);
    }
}

/// Applies the mode selected by the reference state.
///
/// # Safety
///
/// The caller must hold the global switch lock.  `ref_` must remain live until
/// any asynchronous switch completes.
unsafe fn switch_mode_locked(ref_: *mut Ref, confirm: Callback) {
    // SAFETY: Only this module calls this helper while holding the switch lock.
    unsafe { bindings::rust_percpu_ref_assert_locked() };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    unsafe { bindings::rust_percpu_ref_wait_locked(ref_) };

    // SAFETY: The caller holds the switch lock and `ref_` is live.
    let data = unsafe { (*ref_).data };
    // SAFETY: A live initialized ref always has backing data.
    let force_atomic = unsafe { force_atomic(data) };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    let dying = unsafe { bindings::rust_percpu_ref_is_dying_inline(ref_) };

    if force_atomic || dying {
        // SAFETY: The caller holds the switch lock and `ref_` is live.
        unsafe { switch_to_atomic_locked(ref_, confirm) };
    } else {
        // SAFETY: The caller holds the switch lock and `ref_` is live.
        unsafe { switch_to_percpu_locked(ref_) };
    }
}

/// Initializes a per-CPU reference counter.
///
/// # Safety
///
/// `ref_` must point to uninitialized storage suitable for a `percpu_ref`.
/// `release` must satisfy the contract documented by `percpu_ref_init()`.
#[export]
unsafe extern "C" fn rust_percpu_ref_init(
    ref_: *mut Ref,
    release: Callback,
    flags: c_uint,
    gfp: bindings::gfp_t,
) -> c_int {
    // SAFETY: The caller provides writable uninitialized `percpu_ref` storage.
    let ret = unsafe { bindings::rust_percpu_ref_alloc_percpu(ref_, gfp) };
    if ret != 0 {
        return ret;
    }

    // SAFETY: Allocation without a sleeping dependency is valid in this context.
    let data = unsafe { bindings::rust_percpu_ref_alloc_data(gfp) };
    if data.is_null() {
        // SAFETY: `ref_` owns the allocation from the successful call above.
        let percpu = unsafe { (*ref_).percpu_count_ptr };
        // SAFETY: `percpu` is the allocation owned by `ref_`.
        unsafe { bindings::rust_percpu_ref_free_percpu(percpu) };
        // SAFETY: `ref_` is writable.
        unsafe { (*ref_).percpu_count_ptr = 0 };
        return -(bindings::ENOMEM as c_int);
    }

    // SAFETY: `data` is a newly allocated zeroed object.
    unsafe {
        set_force_atomic(data, flags & INIT_ATOMIC != 0);
        set_allow_reinit(data, flags & ALLOW_REINIT != 0);
    }

    let mut start_count: c_ulong = 0;
    if flags & (INIT_ATOMIC | INIT_DEAD) != 0 {
        // SAFETY: `ref_` is writable and owns its per-CPU allocation.
        let count_ptr = unsafe { (*ref_).percpu_count_ptr };
        // SAFETY: `ref_` is writable.
        unsafe { (*ref_).percpu_count_ptr = count_ptr | ATOMIC };
        // SAFETY: `data` is a newly allocated object owned by this function.
        unsafe { set_allow_reinit(data, true) };
    } else {
        start_count += COUNT_BIAS;
    }

    if flags & INIT_DEAD != 0 {
        // SAFETY: `ref_` is writable.
        let count_ptr = unsafe { (*ref_).percpu_count_ptr };
        // SAFETY: `ref_` is writable.
        unsafe { (*ref_).percpu_count_ptr = count_ptr | DEAD };
    } else {
        start_count += 1;
    }

    // SAFETY: `data` is allocated and owned by this function.
    unsafe {
        bindings::rust_percpu_ref_atomic_set(data, start_count as c_long);
        (*data).release = release;
        (*data).confirm_switch = None;
        (*data).ref_ = ref_;
        (*ref_).data = data;
    }
    0
}

/// Releases resources owned by a `percpu_ref`.
///
/// # Safety
///
/// The caller must ensure the reference is no longer in active use.
#[export]
unsafe extern "C" fn rust_percpu_ref_exit(ref_: *mut Ref) {
    // SAFETY: The caller guarantees that `ref_` is initialized and inactive.
    unsafe { exit_inner(ref_) };
    // SAFETY: The caller guarantees that `ref_` is initialized.
    let data = unsafe { (*ref_).data };
    if data.is_null() {
        return;
    }

    // SAFETY: Holding the switch lock prevents a concurrent teardown from
    // changing `percpu_count_ptr` or clearing `data`.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: `data` is live and serialized by the switch lock.
    let count = unsafe { bindings::rust_percpu_ref_atomic_read(data) };
    // SAFETY: The caller guarantees that `ref_` is still writable.
    let count_ptr = unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) };
    // SAFETY: `ref_` is writable and `count_ptr` is current.
    unsafe {
        set_count_ptr(ref_, count_ptr | ((count as c_ulong) << FLAG_BITS));
        (*ref_).data = ptr::null_mut();
    }
    drop(_lock);

    // SAFETY: `data` was allocated by `percpu_ref_init()` and is no longer
    // reachable from `ref_`.
    unsafe { bindings::rust_percpu_ref_free_data(data) };
}

/// Switches a reference to atomic mode.
///
/// # Safety
///
/// `ref_` must be between successful initialization and exit.
#[export]
unsafe extern "C" fn rust_percpu_ref_switch_to_atomic(ref_: *mut Ref, confirm: Callback) {
    // SAFETY: The public API contract guarantees a live initialized `ref_`.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    let data = unsafe { (*ref_).data };
    // SAFETY: `data` is live under the switch lock.
    unsafe { set_force_atomic(data, true) };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    unsafe { switch_mode_locked(ref_, confirm) };
}

/// Switches to atomic mode and waits for completion.
///
/// # Safety
///
/// `ref_` must be between successful initialization and exit.
#[export]
unsafe extern "C" fn rust_percpu_ref_switch_to_atomic_sync(ref_: *mut Ref) {
    // SAFETY: This forwards the caller's live-reference contract.
    unsafe { rust_percpu_ref_switch_to_atomic(ref_, None) };
    // SAFETY: The caller guarantees that no concurrent switch changes state.
    unsafe { bindings::rust_percpu_ref_wait_unlocked(ref_) };
}

/// Switches a reference back to per-CPU mode.
///
/// # Safety
///
/// `ref_` must be between successful initialization and exit.
#[export]
unsafe extern "C" fn rust_percpu_ref_switch_to_percpu(ref_: *mut Ref) {
    // SAFETY: The public API contract guarantees a live initialized `ref_`.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    let data = unsafe { (*ref_).data };
    // SAFETY: `data` is live under the switch lock.
    unsafe { set_force_atomic(data, false) };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    unsafe { switch_mode_locked(ref_, None) };
}

/// Kills a reference and optionally schedules confirmation.
///
/// # Safety
///
/// `ref_` must be between successful initialization and exit.
#[export]
unsafe extern "C" fn rust_percpu_ref_kill_and_confirm(ref_: *mut Ref, confirm_kill: Callback) {
    // SAFETY: The public API contract guarantees a live initialized `ref_`.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: `ref_` is live under the switch lock.
    unsafe { bindings::rust_percpu_ref_warn_kill_twice(ref_) };
    // SAFETY: The caller holds the switch lock and `ref_` is writable.
    let count_ptr = unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) };
    // SAFETY: The caller holds the switch lock and `ref_` is writable.
    unsafe { set_count_ptr(ref_, count_ptr | DEAD) };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    unsafe { switch_mode_locked(ref_, confirm_kill) };
    // SAFETY: Kill drops the initial reference after arming confirmation.
    unsafe { bindings::rust_percpu_ref_put(ref_) };
}

/// Tests whether a reference reached zero.
///
/// # Safety
///
/// `ref_` must be between successful initialization and exit.
#[export]
unsafe extern "C" fn rust_percpu_ref_is_zero(ref_: *mut Ref) -> bool {
    let mut percpu = 0;
    // SAFETY: The public API contract guarantees a live initialized `ref_`.
    if unsafe { bindings::rust_percpu_ref_read_is_percpu(ref_, &mut percpu) } {
        return false;
    }

    // SAFETY: The switch lock protects against concurrent exit while reading
    // the backing counter.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: `ref_` is live under the switch lock.
    let data = unsafe { (*ref_).data };
    let count = if data.is_null() {
        // SAFETY: The encoded count is stored in the count pointer after exit.
        unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) >> FLAG_BITS }
    } else {
        // SAFETY: `data` is live under the switch lock.
        unsafe { bindings::rust_percpu_ref_atomic_read(data) as c_ulong }
    };
    count == 0
}

/// Reinitalizes a zeroed reference.
///
/// # Safety
///
/// `ref_` must have reached zero but must not have exited.
#[export]
unsafe extern "C" fn rust_percpu_ref_reinit(ref_: *mut Ref) {
    // SAFETY: The public API contract guarantees that `ref_` is live.
    let zero = unsafe { rust_percpu_ref_is_zero(ref_) };
    // SAFETY: The zero check above is the warning condition.
    unsafe { bindings::rust_percpu_ref_warn_reinit_not_zero(!zero) };
    // SAFETY: Resurrect applies the remaining state transition.
    unsafe { rust_percpu_ref_resurrect(ref_) };
}

/// Resurrects a dead reference.
///
/// # Safety
///
/// `ref_` must be dead and must not have exited.
#[export]
unsafe extern "C" fn rust_percpu_ref_resurrect(ref_: *mut Ref) {
    // SAFETY: The public API contract guarantees a live initialized `ref_`.
    let _lock = unsafe { SwitchLock::acquire() };
    // SAFETY: `ref_` is live under the switch lock.
    unsafe { bindings::rust_percpu_ref_warn_resurrect_not_dying(ref_) };
    // SAFETY: `ref_` is live under the switch lock.
    unsafe { bindings::rust_percpu_ref_warn_resurrect_percpu(ref_) };
    // SAFETY: The caller holds the switch lock and `ref_` is writable.
    let count_ptr = unsafe { bindings::rust_percpu_ref_read_count_ptr(ref_) };
    // SAFETY: The caller holds the switch lock and `ref_` is writable.
    unsafe { set_count_ptr(ref_, count_ptr & !DEAD) };
    // SAFETY: Resurrection restores the initial reference.
    unsafe { bindings::rust_percpu_ref_get(ref_) };
    // SAFETY: The caller holds the switch lock and `ref_` is live.
    unsafe { switch_mode_locked(ref_, None) };
}
