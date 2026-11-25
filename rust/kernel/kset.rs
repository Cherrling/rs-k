// SPDX-License-Identifier: GPL-2.0-only

//! Native `kset` lifecycle.
//!
//! Rust directly defines kset initialization, registration, unregistration,
//! lookup, and dynamic creation. C retains the shared layouts, spinlock,
//! kobject hierarchy and sysfs operations, uevent delivery, allocation, and
//! the static kset release callback.

use crate::bindings;
use crate::ffi::{c_char, c_int};
use core::ptr;
use macros::export;

type Kobject = bindings::kobject;
type Kset = bindings::kset;
type KsetUeventOps = bindings::kset_uevent_ops;

/// Returns whether two NUL-terminated strings are equal.
///
/// # Safety
///
/// `right` must point to a valid NUL-terminated string. `left` must be null or
/// point to a valid NUL-terminated string.
unsafe fn names_equal(left: *const c_char, right: *const c_char) -> bool {
    if left.is_null() {
        return false;
    }

    let mut index = 0;
    loop {
        // SAFETY: Both pointers are valid for reads through their terminator.
        let left_byte = unsafe { *left.add(index) };
        // SAFETY: The caller guarantees a valid string for `right`.
        let right_byte = unsafe { *right.add(index) };
        if left_byte != right_byte {
            return false;
        }
        if left_byte == 0 {
            return true;
        }
        index += 1;
    }
}

/// Initializes the embedded kset list, lock, and kobject.
///
/// # Safety
///
/// `kset` must point to valid, writable `kset` storage.
#[inline]
unsafe fn init_kset(kset: *mut Kset) {
    // SAFETY: The caller guarantees valid storage for the embedded kobject.
    unsafe { bindings::rust_kobject_init_internal(ptr::addr_of_mut!((*kset).kobj)) };
    // SAFETY: The caller guarantees valid kset storage. The helper retains
    // list and spinlock initialization for their debugging contracts.
    unsafe { bindings::rust_helper_kset_init_objects(kset) };
}

/// Creates an unregistered dynamic kset.
///
/// # Safety
///
/// `name` must point to a valid NUL-terminated string. `uevent_ops` must remain
/// valid for the kset lifetime.
unsafe fn create_kset(
    name: *const c_char,
    uevent_ops: *const KsetUeventOps,
    parent: *mut Kobject,
) -> *mut Kset {
    // SAFETY: The helper only allocates zeroed kset storage.
    let kset = unsafe { bindings::rust_helper_kset_alloc() };
    if kset.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The allocation is valid and the caller supplies a valid name.
    if unsafe { bindings::rust_helper_kset_set_name(kset, name) } != 0 {
        // SAFETY: The allocation failed to become a kset.
        unsafe { bindings::rust_helper_kset_free(kset) };
        return ptr::null_mut();
    }

    // SAFETY: The allocation is valid and unregistered.
    unsafe {
        (*kset).uevent_ops = uevent_ops;
        (*kset).kobj.parent = parent;
        (*kset).kobj.ktype = bindings::rust_helper_kset_ktype();
        (*kset).kobj.kset = ptr::null_mut();
    }
    kset
}

/// Initializes a kset for use.
///
/// # Safety
///
/// `kset` must point to valid, writable `kset` storage.
#[export]
unsafe extern "C" fn kset_init(kset: *mut Kset) {
    // SAFETY: The caller satisfies the kset initialization contract.
    unsafe { init_kset(kset) };
}

/// Initializes and adds a kset to the kobject hierarchy.
///
/// # Safety
///
/// `kset` must be null or point to valid, writable `kset` storage with a valid
/// ktype for the kset lifetime.
#[export]
unsafe extern "C" fn kset_register(kset: *mut Kset) -> c_int {
    if kset.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller guarantees valid kset storage.
    if unsafe { (*kset).kobj.ktype.is_null() } {
        // SAFETY: The helper only emits the original diagnostic.
        unsafe { bindings::rust_helper_kset_warn_missing_ktype() };
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller guarantees valid kset storage.
    unsafe { init_kset(kset) };
    // SAFETY: The kset is initialized and its embedded kobject is valid.
    let error = unsafe { bindings::rust_helper_kset_add(kset) };
    if error != 0 {
        // SAFETY: Registration failed after the name was set.
        unsafe { bindings::rust_helper_kset_free_name(kset) };
        return error;
    }

    // SAFETY: Registration succeeded and the helper preserves the original
    // uevent delivery point.
    unsafe { bindings::rust_helper_kset_uevent_add(kset) };
    0
}

/// Removes and releases a kset.
///
/// # Safety
///
/// `kset` must be null or point to a registered kset with a live reference.
#[export]
unsafe extern "C" fn kset_unregister(kset: *mut Kset) {
    if kset.is_null() {
        return;
    }

    // SAFETY: The caller guarantees a registered kset.
    unsafe { bindings::kobject_del(ptr::addr_of_mut!((*kset).kobj)) };
    // SAFETY: The caller transfers the registration-owned reference.
    unsafe { bindings::kobject_put(ptr::addr_of_mut!((*kset).kobj)) };
}

/// Finds a named kobject in a kset and acquires a reference.
///
/// # Safety
///
/// `kset` and `name` must point to valid objects for the duration of the call.
#[export]
unsafe extern "C" fn kset_find_obj(kset: *mut Kset, name: *const c_char) -> *mut Kobject {
    if kset.is_null() || name.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees valid kset storage.
    let head = unsafe { ptr::addr_of_mut!((*kset).list) };
    let mut found = ptr::null_mut();

    // SAFETY: The helper acquires the kset list lock with its original
    // context.
    unsafe { bindings::rust_helper_kset_lock(kset) };
    // SAFETY: The list head is valid and protected by the list lock.
    let mut entry = unsafe { (*head).next };
    while entry != head {
        // SAFETY: Entries in this list embed `struct kobject::entry`.
        let kobject = unsafe { crate::container_of!(entry, Kobject, entry) };
        // SAFETY: A linked kobject remains live while its list lock is held.
        if unsafe { names_equal((*kobject).name, name) } {
            // SAFETY: The linked object can be acquired while the list lock is
            // held, preserving the original lookup contract.
            found = unsafe { bindings::kobject_get_unless_zero(kobject) };
            break;
        }
        // SAFETY: The current entry is valid and remains locked.
        entry = unsafe { (*entry).next };
    }
    // SAFETY: The helper releases the matching kset list lock.
    unsafe { bindings::rust_helper_kset_unlock(kset) };

    found
}

/// Dynamically creates and registers a kset.
///
/// # Safety
///
/// `name` must point to a valid NUL-terminated string. `uevent_ops` must remain
/// valid for the kset lifetime. `parent` must be null or remain live while the
/// kset is registered.
#[export]
unsafe extern "C" fn kset_create_and_add(
    name: *const c_char,
    uevent_ops: *const KsetUeventOps,
    parent: *mut Kobject,
) -> *mut Kset {
    if name.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller satisfies the dynamic creation contract.
    let kset = unsafe { create_kset(name, uevent_ops, parent) };
    if kset.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The dynamically allocated kset is initialized by registration.
    if unsafe { kset_register(kset) } != 0 {
        // SAFETY: Registration failed and the original implementation frees
        // the unregistered allocation directly.
        unsafe { bindings::rust_helper_kset_free(kset) };
        return ptr::null_mut();
    }
    kset
}
