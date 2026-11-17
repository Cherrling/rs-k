// SPDX-License-Identifier: GPL-2.0-only

//! Native `kobject` initialization and reference lifecycle.
//!
//! Rust owns initialization state, reference acquisition, conditional
//! acquisition, and final-reference dispatch. The native Rust `kref` and
//! `refcount_t` implementations perform the count transitions.
//! C retains bitfield accessors, diagnostics, sysfs and kset operations, and
//! the final cleanup callback.

use crate::bindings;
use core::ptr;
use macros::export;

type Kobject = bindings::kobject;
type KobjType = bindings::kobj_type;
type Kref = bindings::kref;
type Release = Option<unsafe extern "C" fn(*mut Kref)>;

/// Returns the embedded kref.
///
/// # Safety
///
/// `kobj` must point to valid `kobject` storage.
#[inline]
unsafe fn kref(kobj: *mut Kobject) -> *mut Kref {
    // SAFETY: The caller guarantees valid `kobject` storage.
    unsafe { ptr::addr_of_mut!((*kobj).kref) }
}

/// Initializes the kobject state without changing its type.
///
/// # Safety
///
/// `kobj` must point to valid, writable `kobject` storage.
#[export]
unsafe extern "C" fn rust_kobject_init_internal(kobj: *mut Kobject) {
    if kobj.is_null() {
        return;
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    unsafe {
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*kobj).entry));
        bindings::rust_kref_init(kref(kobj));
        bindings::rust_kobject_state_reset(kobj);
        bindings::rust_kobject_state_mark_initialized(kobj);
    }
}

/// Initializes a kobject and assigns its type.
///
/// # Safety
///
/// `kobj` must point to valid, writable `kobject` storage. `ktype` must remain
/// valid for the lifetime of `kobj`.
#[export]
unsafe extern "C" fn rust_kobject_init(kobj: *mut Kobject, ktype: *const KobjType) {
    if kobj.is_null() {
        // SAFETY: This helper only emits the diagnostic.
        unsafe { bindings::rust_kobject_warn_invalid_pointer() };
        return;
    }
    if ktype.is_null() {
        // SAFETY: The caller supplied a valid `kobject` pointer.
        unsafe { bindings::rust_kobject_warn_missing_ktype(kobj) };
        return;
    }

    // SAFETY: The caller guarantees that `kobj` is valid and writable.
    if unsafe { bindings::rust_kobject_state_initialized(kobj) } {
        // SAFETY: The caller supplied a valid `kobject` pointer.
        unsafe { bindings::rust_kobject_warn_reinitialized(kobj) };
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    unsafe {
        rust_kobject_init_internal(kobj);
        (*kobj).ktype = ktype;
    }
}

/// Acquires a reference to a kobject.
///
/// # Safety
///
/// `kobj` must be null or point to valid `kobject` storage. A non-null object
/// must be initialized and the caller must hold a reference.
#[export]
unsafe extern "C" fn rust_kobject_get(kobj: *mut Kobject) -> *mut Kobject {
    if !kobj.is_null() {
        // SAFETY: The caller guarantees valid `kobject` storage.
        if !unsafe { bindings::rust_kobject_state_initialized(kobj) } {
            // SAFETY: The caller supplied a valid `kobject` pointer.
            unsafe { bindings::rust_kobject_warn_uninitialized(kobj, false) };
        }
        // SAFETY: The caller holds a reference to the initialized object.
        unsafe { bindings::rust_kref_get(kref(kobj)) };
    }

    kobj
}

/// Acquires a reference unless the count is zero.
///
/// # Safety
///
/// `kobj` must be null or point to valid, initialized `kobject` storage with a
/// stable lifetime.
#[export]
unsafe extern "C" fn rust_kobject_get_unless_zero(kobj: *mut Kobject) -> *mut Kobject {
    if kobj.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    if unsafe { bindings::rust_kref_get_unless_zero(kref(kobj)) } != 0 {
        kobj
    } else {
        ptr::null_mut()
    }
}

/// Releases a kobject reference and dispatches cleanup on the final one.
///
/// # Safety
///
/// `kobj` must be null or point to valid `kobject` storage. A non-null object
/// must be initialized and the caller must hold a reference. `release` must
/// implement the `kobject_release()` contract.
#[export]
unsafe extern "C" fn rust_kobject_put(kobj: *mut Kobject, release: Release) {
    if kobj.is_null() {
        return;
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    if !unsafe { bindings::rust_kobject_state_initialized(kobj) } {
        // SAFETY: The caller supplied a valid `kobject` pointer.
        unsafe { bindings::rust_kobject_warn_uninitialized(kobj, true) };
    }
    // SAFETY: The caller holds a reference to the initialized object.
    unsafe { bindings::rust_kref_put(kref(kobj), release) };
}
