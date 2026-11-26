// SPDX-License-Identifier: GPL-2.0-only

//! Native device bus lifecycle.
//!
//! Rust directly defines bus registration and unregistration. C retains the
//! shared layouts, lookup and reference accounting, static kset and kobject
//! types, sysfs attributes, device reference callbacks, and diagnostics.

use crate::alloc::{flags::GFP_KERNEL, KBox};
use crate::bindings;
use crate::ffi::c_int;
use crate::str::CStrExt as _;
use core::ptr;
use macros::export;

type BusType = bindings::bus_type;
type SubsysPrivate = bindings::subsys_private;

/// Allocates zeroed bus-private storage.
fn alloc_subsys_private() -> *mut SubsysPrivate {
    match KBox::new(SubsysPrivate::default(), GFP_KERNEL) {
        Ok(private) => KBox::into_raw(private),
        Err(_) => ptr::null_mut(),
    }
}

/// Frees bus-private storage that never entered the kobject hierarchy.
///
/// # Safety
///
/// `private` must come from `alloc_subsys_private()` and must not be
/// registered.
unsafe fn free_subsys_private(private: *mut SubsysPrivate) {
    // SAFETY: The caller guarantees ownership and no live registration.
    unsafe { drop(KBox::from_raw(private)) };
}

/// Initializes the private state needed before kset registration.
///
/// # Safety
///
/// `private` must be a fresh allocation. `bus` must remain valid for the
/// lifetime of the registered bus.
unsafe fn initialize_private(private: *mut SubsysPrivate, bus: *const BusType) -> c_int {
    // SAFETY: The private allocation is valid and the bus remains live.
    unsafe {
        (*private).bus = bus;
        bindings::rust_helper_bus_init_notifier(private);
        bindings::kset_init(ptr::addr_of_mut!((*private).subsys));
    }

    // SAFETY: The private allocation is initialized and the bus name remains
    // live for the bus lifetime.
    let error = unsafe { bindings::rust_helper_bus_set_name(private, (*bus).name) };
    if error != 0 {
        return error;
    }

    // SAFETY: The private allocation is initialized and the public statics
    // remain live for the kernel lifetime.
    unsafe {
        (*private).subsys.kobj.kset = bindings::rust_helper_bus_kset();
        (*private).subsys.kobj.ktype = bindings::rust_helper_bus_ktype();
        (*private).set_drivers_autoprobe(1);
    }

    0
}

/// Rolls back a bus whose main kset and uevent file were created.
///
/// # Safety
///
/// `private` must own a registered main kset and the corresponding uevent
/// file. The caller must not use `private` after this function.
unsafe fn rollback_registered(private: *mut SubsysPrivate, bus: *const BusType) {
    // SAFETY: The child kset pointers are either null or registered.
    unsafe {
        if !(*private).drivers_kset.is_null() {
            bindings::kset_unregister((*private).drivers_kset);
        }
        if !(*private).devices_kset.is_null() {
            bindings::kset_unregister((*private).devices_kset);
        }
        bindings::rust_helper_bus_remove_uevent_file(bus);
        bindings::kset_unregister(ptr::addr_of_mut!((*private).subsys));
    }
}

/// Registers a device bus.
///
/// # Safety
///
/// `bus` must point to valid bus storage that remains live until
/// unregistration completes.
#[export]
unsafe extern "C" fn bus_register(bus: *const BusType) -> c_int {
    if bus.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    let private = alloc_subsys_private();
    if private.is_null() {
        return -(bindings::ENOMEM as c_int);
    }

    // SAFETY: The allocation is fresh and the bus remains live.
    let error = unsafe { initialize_private(private, bus) };
    if error != 0 {
        // SAFETY: Initialization failed before kset registration.
        unsafe { free_subsys_private(private) };
        return error;
    }

    // SAFETY: The embedded kset and kobject type are initialized.
    let error = unsafe { bindings::kset_register(ptr::addr_of_mut!((*private).subsys)) };
    if error != 0 {
        // SAFETY: Registration failed and did not retain the allocation.
        unsafe { free_subsys_private(private) };
        return error;
    }

    // SAFETY: The bus kset is registered and the bus remains live.
    let error = unsafe { bindings::rust_helper_bus_create_uevent_file(bus) };
    if error != 0 {
        // SAFETY: The main kset is the only registered resource.
        unsafe { bindings::kset_unregister(ptr::addr_of_mut!((*private).subsys)) };
        return error;
    }

    // SAFETY: The parent kobject is registered and the child name is static.
    let devices_kset = unsafe {
        bindings::kset_create_and_add(
            c"devices".as_char_ptr(),
            ptr::null(),
            ptr::addr_of_mut!((*private).subsys.kobj),
        )
    };
    if devices_kset.is_null() {
        // SAFETY: The main kset and uevent file are registered.
        unsafe { rollback_registered(private, bus) };
        return -(bindings::ENOMEM as c_int);
    }
    // SAFETY: The private allocation owns the child kset registration.
    unsafe { (*private).devices_kset = devices_kset };

    // SAFETY: The parent kobject is registered and the child name is static.
    let drivers_kset = unsafe {
        bindings::kset_create_and_add(
            c"drivers".as_char_ptr(),
            ptr::null(),
            ptr::addr_of_mut!((*private).subsys.kobj),
        )
    };
    if drivers_kset.is_null() {
        // SAFETY: The main kset, uevent file, and devices kset are registered.
        unsafe { rollback_registered(private, bus) };
        return -(bindings::ENOMEM as c_int);
    }
    // SAFETY: The private allocation owns the child kset registration.
    unsafe { (*private).drivers_kset = drivers_kset };

    // SAFETY: The private allocation is registered and all embedded locks and
    // lists remain owned by it.
    unsafe {
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*private).interfaces));
        bindings::lockdep_register_key(ptr::addr_of_mut!((*private).lock_key));
        bindings::__mutex_init(
            ptr::addr_of_mut!((*private).mutex),
            c"subsys mutex".as_char_ptr(),
            ptr::addr_of_mut!((*private).lock_key),
        );
        bindings::klist_init(
            ptr::addr_of_mut!((*private).klist_devices),
            Some(bindings::rust_helper_bus_klist_devices_get),
            Some(bindings::rust_helper_bus_klist_devices_put),
        );
        bindings::klist_init(ptr::addr_of_mut!((*private).klist_drivers), None, None);
    }

    // SAFETY: The bus kset is registered.
    let error = unsafe { bindings::rust_helper_bus_add_probe_files(bus) };
    if error != 0 {
        // SAFETY: Probe-file creation rolls back its partial state.
        unsafe { rollback_registered(private, bus) };
        return error;
    }

    // SAFETY: The bus kobject is registered and the groups remain live.
    let error = unsafe {
        bindings::sysfs_create_groups(ptr::addr_of_mut!((*private).subsys.kobj), (*bus).bus_groups)
    };
    if error != 0 {
        // SAFETY: Group creation rolls back its partial state.
        unsafe {
            bindings::rust_helper_bus_remove_probe_files(bus);
            rollback_registered(private, bus);
        }
        return error;
    }

    // SAFETY: The bus is fully registered and remains live.
    unsafe { bindings::rust_helper_bus_debug_registered(bus) };
    0
}

/// Unregisters a device bus.
///
/// # Safety
///
/// `bus` must be null or point to valid registered bus storage.
#[export]
unsafe extern "C" fn bus_unregister(bus: *const BusType) {
    // SAFETY: The lookup tolerates null and returns a referenced private
    // object only for a registered bus.
    let private = unsafe { bindings::bus_to_subsys(bus) };
    if private.is_null() {
        return;
    }

    // SAFETY: The lookup reference keeps the private object live.
    unsafe {
        bindings::rust_helper_bus_debug_unregistering(bus);
        if !(*private).dev_root.is_null() {
            bindings::device_unregister((*private).dev_root);
        }
        bindings::sysfs_remove_groups(ptr::addr_of_mut!((*private).subsys.kobj), (*bus).bus_groups);
        bindings::rust_helper_bus_remove_probe_files(bus);
        bindings::rust_helper_bus_remove_uevent_file(bus);
        bindings::kset_unregister((*private).drivers_kset);
        bindings::kset_unregister((*private).devices_kset);
        bindings::kset_unregister(ptr::addr_of_mut!((*private).subsys));
        bindings::kobject_put(ptr::addr_of_mut!((*private).subsys.kobj));
    }
}
