// SPDX-License-Identifier: GPL-2.0-only

//! Native device class lifecycle.
//!
//! Rust directly defines class registration, unregistration, dynamic creation,
//! and destruction. C retains the shared layouts, sysfs and klist operations,
//! allocation helpers, static kset and kobject types, diagnostics, and the
//! final class release callback.

use crate::alloc::{flags::GFP_KERNEL, KBox};
use crate::bindings;
use crate::ffi::{c_char, c_int, c_long};
use crate::str::CStrExt as _;
use core::ptr;
use macros::export;

type Class = bindings::class;
type Device = bindings::device;
type DevicePrivate = bindings::device_private;
type KlistNode = bindings::klist_node;
type SubsysPrivate = bindings::subsys_private;

/// Returns the device associated with a class-list node.
///
/// # Safety
///
/// `node` must be the embedded `device_private::knode_class` link.
unsafe fn class_dev(node: *mut KlistNode) -> *mut Device {
    // SAFETY: The caller guarantees the klist node type.
    let private = unsafe { crate::container_of!(node, DevicePrivate, knode_class) };
    // SAFETY: The private object owns a live pointer while the class list node
    // is linked.
    unsafe { (*private).device }
}

/// Acquires a reference for a class-list node.
///
/// # Safety
///
/// `node` must be a live class-list node.
unsafe extern "C" fn class_dev_get(node: *mut KlistNode) {
    // SAFETY: The class list owns a valid node.
    let dev = unsafe { class_dev(node) };
    // SAFETY: The device remains valid for the node lifetime.
    unsafe { bindings::get_device(dev) };
}

/// Releases a reference for a class-list node.
///
/// # Safety
///
/// `node` must be a live class-list node.
unsafe extern "C" fn class_dev_put(node: *mut KlistNode) {
    // SAFETY: The class list owns a valid node.
    let dev = unsafe { class_dev(node) };
    // SAFETY: The reference was acquired by the matching get callback.
    unsafe { bindings::put_device(dev) };
}

/// Allocates zeroed class-private storage.
fn alloc_subsys_private() -> *mut SubsysPrivate {
    match KBox::new(SubsysPrivate::default(), GFP_KERNEL) {
        Ok(private) => KBox::into_raw(private),
        Err(_) => ptr::null_mut(),
    }
}

/// Frees an unregistered class-private allocation.
///
/// # Safety
///
/// `private` must come from `alloc_subsys_private()` and must not be
/// registered.
unsafe fn free_subsys_private(private: *mut SubsysPrivate) {
    // SAFETY: The caller guarantees ownership and no live registration.
    unsafe { drop(KBox::from_raw(private)) };
}

/// Initializes class-private state and registers the embedded kset.
///
/// # Safety
///
/// `private` must be a fresh allocation. `cls` must remain valid for the
/// lifetime of the registered class.
unsafe fn init_and_register(private: *mut SubsysPrivate, cls: *const Class) -> c_int {
    // SAFETY: The private allocation is valid and the callbacks preserve the
    // class device reference contract.
    unsafe {
        bindings::klist_init(
            ptr::addr_of_mut!((*private).klist_devices),
            Some(class_dev_get),
            Some(class_dev_put),
        );
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*private).interfaces));
        bindings::kset_init(ptr::addr_of_mut!((*private).subsys));
        bindings::kset_init(ptr::addr_of_mut!((*private).glue_dirs));
        bindings::lockdep_register_key(ptr::addr_of_mut!((*private).lock_key));
        bindings::__mutex_init(
            ptr::addr_of_mut!((*private).mutex),
            c"subsys mutex".as_char_ptr(),
            ptr::addr_of_mut!((*private).lock_key),
        );
    }

    // SAFETY: The class name and private kobject are valid.
    let error = unsafe { bindings::rust_helper_class_set_name(private, (*cls).name) };
    if error != 0 {
        // SAFETY: The key was registered above.
        unsafe { bindings::lockdep_unregister_key(ptr::addr_of_mut!((*private).lock_key)) };
        return error;
    }

    // SAFETY: The private allocation is initialized and remains owned by this
    // function until registration succeeds.
    unsafe {
        (*private).subsys.kobj.kset = bindings::rust_helper_class_kset();
        (*private).subsys.kobj.ktype = bindings::rust_helper_class_ktype();
        (*private).class = cls;
    }

    // SAFETY: The embedded kset and class pointers are initialized.
    let error = unsafe { bindings::kset_register(ptr::addr_of_mut!((*private).subsys)) };
    if error != 0 {
        // SAFETY: The key was registered above.
        unsafe { bindings::lockdep_unregister_key(ptr::addr_of_mut!((*private).lock_key)) };
        return error;
    }

    // SAFETY: Registration succeeded and the kobject remains live.
    let error = unsafe {
        bindings::sysfs_create_groups(
            ptr::addr_of_mut!((*private).subsys.kobj),
            (*cls).class_groups,
        )
    };
    if error != 0 {
        // SAFETY: Group creation failed after kset registration.
        unsafe {
            bindings::kobject_del(ptr::addr_of_mut!((*private).subsys.kobj));
            bindings::kfree_const((*private).subsys.kobj.name.cast());
            bindings::lockdep_unregister_key(ptr::addr_of_mut!((*private).lock_key));
        }
        return error;
    }

    0
}

/// Registers a device class.
///
/// # Safety
///
/// `cls` must point to valid class storage that remains live until
/// unregistration completes.
#[export]
unsafe extern "C" fn class_register(cls: *const Class) -> c_int {
    if cls.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller guarantees valid class storage.
    let (ns_type, namespace) = unsafe { ((*cls).ns_type, (*cls).namespace) };
    if !ns_type.is_null() && namespace.is_none() {
        // SAFETY: The class pointer is valid.
        unsafe { bindings::rust_helper_class_warn_missing_namespace(cls) };
        return -(bindings::EINVAL as c_int);
    }
    if ns_type.is_null() && namespace.is_some() {
        // SAFETY: The class pointer is valid.
        unsafe { bindings::rust_helper_class_warn_missing_ns_type(cls) };
        return -(bindings::EINVAL as c_int);
    }

    let private = alloc_subsys_private();
    if private.is_null() {
        return -(bindings::ENOMEM as c_int);
    }

    // SAFETY: The allocation is fresh and the class remains live.
    let error = unsafe { init_and_register(private, cls) };
    if error != 0 {
        // SAFETY: `init_and_register()` leaves the allocation unregistered.
        unsafe { free_subsys_private(private) };
    }
    error
}

/// Unregisters a device class.
///
/// # Safety
///
/// `cls` must be null or point to valid class storage.
#[export]
unsafe extern "C" fn class_unregister(cls: *const Class) {
    // SAFETY: The lookup tolerates null and returns a referenced private
    // object only for a registered class.
    let private = unsafe { bindings::class_to_subsys(cls) };
    if private.is_null() {
        return;
    }

    // SAFETY: The lookup reference keeps the private object live.
    unsafe {
        bindings::sysfs_remove_groups(
            ptr::addr_of_mut!((*private).subsys.kobj),
            (*cls).class_groups,
        );
        bindings::kset_unregister(ptr::addr_of_mut!((*private).subsys));
        bindings::kobject_put(ptr::addr_of_mut!((*private).subsys.kobj));
    }
}

/// Allocates and registers a dynamic device class.
///
/// # Safety
///
/// `name` must remain valid for the class lifetime.
#[export]
unsafe extern "C" fn class_create(name: *const c_char) -> *mut Class {
    let cls = match KBox::new(Class::default(), GFP_KERNEL) {
        Ok(cls) => KBox::into_raw(cls),
        Err(_) => {
            // SAFETY: The encoded negative errno is a valid error pointer.
            return unsafe { bindings::ERR_PTR(-(bindings::ENOMEM as c_long)) }.cast();
        }
    };

    // SAFETY: The allocation is valid and the caller owns the name lifetime.
    unsafe {
        (*cls).name = name;
        (*cls).class_release = Some(bindings::rust_helper_class_create_release);
    }

    // SAFETY: The allocation is initialized and the name remains live.
    let error = unsafe { class_register(cls) };
    if error != 0 {
        // SAFETY: Registration failed and the allocation is unregistered.
        unsafe { drop(KBox::from_raw(cls)) };
        // SAFETY: The encoded negative errno is a valid error pointer.
        return unsafe { bindings::ERR_PTR(error as c_long) }.cast();
    }
    cls
}

/// Destroys a class created by `class_create`.
///
/// # Safety
///
/// `cls` must be null, an error pointer, or a live class from
/// `class_create()`.
#[export]
unsafe extern "C" fn class_destroy(cls: *const Class) {
    // SAFETY: The helper only classifies the pointer value.
    if cls.is_null() || unsafe { bindings::IS_ERR(cls.cast()) } {
        return;
    }
    // SAFETY: The caller guarantees a dynamically created class.
    unsafe { class_unregister(cls) };
}
