// SPDX-License-Identifier: GPL-2.0-only

//! Native `kref` lifecycle implementation.
//!
//! Rust owns initialization, reference reads, successful and conditional
//! references, final-reference detection, and release callback dispatch.
//! The native Rust `refcount_t` state machine performs the count transitions.
//! Rust directly defines the public entry points. C retains the shared layout,
//! mutex and spinlock primitives, callback ABI, and ordinary export metadata.

use crate::bindings;
use core::{ffi::c_int, ptr};

type Kref = bindings::kref;
type Mutex = bindings::mutex;
type Ref = bindings::refcount_t;
type Release = Option<unsafe extern "C" fn(*mut Kref)>;

/// Returns the embedded refcount.
///
/// # Safety
///
/// `kref` must point to valid, writable `kref` storage.
#[inline]
unsafe fn refcount(kref: *mut Kref) -> *mut Ref {
    // SAFETY: The caller guarantees valid `kref` storage.
    unsafe { ptr::addr_of_mut!((*kref).refcount) }
}

/// Invokes the release callback.
///
/// # Safety
///
/// `kref` must point to the object whose final reference was released. The
/// callback must satisfy the `kref_put()` release contract.
#[inline]
unsafe fn release(kref: *mut Kref, callback: Release) {
    let Some(callback) = callback else {
        panic!("kref release callback must not be NULL");
    };
    // SAFETY: The public kref contract requires a valid release callback.
    unsafe { callback(kref) };
}

/// Initializes a kref with one reference.
///
/// # Safety
///
/// `kref` must point to valid, writable `kref` storage.
#[no_mangle]
unsafe extern "C" fn kref_init(kref: *mut Kref) {
    // SAFETY: The caller guarantees valid `kref` storage.
    unsafe { bindings::refcount_set(refcount(kref), 1) };
}

/// Reads a kref count.
///
/// # Safety
///
/// `kref` must point to valid `kref` storage.
#[no_mangle]
unsafe extern "C" fn kref_read(kref: *const Kref) -> core::ffi::c_uint {
    // SAFETY: The caller guarantees valid `kref` storage.
    let refcount = unsafe { ptr::addr_of!((*kref).refcount) };
    // SAFETY: The embedded refcount is valid for the lifetime of `kref`.
    unsafe { bindings::rust_refcount_atomic_read(refcount) as core::ffi::c_uint }
}

/// Acquires another reference.
///
/// # Safety
///
/// `kref` must point to valid `kref` storage and the caller must hold a
/// reference to the object.
#[no_mangle]
unsafe extern "C" fn kref_get(kref: *mut Kref) {
    // SAFETY: The caller guarantees valid `kref` storage and a live reference.
    unsafe { bindings::rust_refcount_add(1, refcount(kref), ptr::null_mut()) };
}

/// Acquires another reference unless the count is zero.
///
/// # Safety
///
/// `kref` must point to valid `kref` storage with a stable lifetime.
#[no_mangle]
unsafe extern "C" fn kref_get_unless_zero(kref: *mut Kref) -> c_int {
    // SAFETY: The caller guarantees valid `kref` storage.
    unsafe {
        c_int::from(bindings::rust_refcount_add_not_zero(
            1,
            refcount(kref),
            ptr::null_mut(),
        ))
    }
}

/// Releases a reference and invokes the release callback on the final one.
///
/// # Safety
///
/// `kref` must point to valid `kref` storage and the caller must hold a
/// reference. `release` must satisfy the public `kref_put()` contract.
#[no_mangle]
unsafe extern "C" fn kref_put(kref: *mut Kref, callback: Release) -> c_int {
    // SAFETY: The caller guarantees valid `kref` storage and a live reference.
    let released =
        unsafe { bindings::rust_refcount_sub_and_test(1, refcount(kref), ptr::null_mut()) };
    if released {
        // SAFETY: The refcount transitioned from one to zero.
        unsafe { release(kref, callback) };
        1
    } else {
        0
    }
}

/// Releases a reference while conditionally holding a mutex for the releaser.
///
/// # Safety
///
/// `kref` and `mutex` must be valid. The caller must hold a reference. The
/// release callback receives the mutex and must unlock it.
#[no_mangle]
unsafe extern "C" fn kref_put_mutex(
    kref: *mut Kref,
    callback: Release,
    mutex: *mut Mutex,
) -> c_int {
    // SAFETY: The caller guarantees valid kref and mutex storage.
    let released = unsafe { bindings::rust_refcount_dec_and_mutex_lock(refcount(kref), mutex) };
    if released {
        // SAFETY: The conditional transition returned with the mutex held.
        unsafe { release(kref, callback) };
        1
    } else {
        0
    }
}

/// Releases a reference while conditionally holding a spinlock for the releaser.
///
/// # Safety
///
/// `kref` and `lock` must be valid. The caller must hold a reference. The
/// release callback receives the lock and must unlock it.
#[no_mangle]
unsafe extern "C" fn kref_put_lock(
    kref: *mut Kref,
    callback: Release,
    lock: *mut bindings::spinlock_t,
) -> c_int {
    // SAFETY: The caller guarantees valid kref and lock storage.
    let released = unsafe { bindings::rust_refcount_dec_and_lock(refcount(kref), lock) };
    if released {
        // SAFETY: The conditional transition returned with the spinlock held.
        unsafe { release(kref, callback) };
        1
    } else {
        0
    }
}
