// SPDX-License-Identifier: GPL-2.0-only

//! Native device driver registration lifecycle.
//!
//! Rust defines driver registration and unregistration, including private
//! allocation, bus membership, probing, module links, sysfs attributes,
//! rollback, and teardown order. C retains shared layouts and the underlying
//! bus, kobject, klist, sysfs, and attach/detach primitives.

use crate::alloc::{flags::GFP_KERNEL, KBox};
use crate::bindings;
use crate::ffi::c_int;
use crate::pr_err;
use crate::pr_warn;
use crate::str::{CStr, CStrExt as _};
use core::ptr;
use macros::export;

type DeviceDriver = bindings::device_driver;
type DriverPrivate = bindings::driver_private;
type SubsysPrivate = bindings::subsys_private;

/// Releases a bus lookup reference.
///
/// # Safety
///
/// `sp` must be null or hold a bus reference returned by `bus_to_subsys()`.
unsafe fn bus_put(sp: *mut SubsysPrivate) {
    // SAFETY: The caller guarantees reference ownership.
    unsafe { bindings::rust_helper_driver_bus_put(sp) };
}

/// Rolls back a driver that was linked into a bus.
///
/// # Safety
///
/// `drv` must own a linked driver-private object. `sp` must hold the bus
/// reference retained for that object.
unsafe fn rollback_linked_driver(
    drv: *mut DeviceDriver,
    sp: *mut SubsysPrivate,
    detach_devices: bool,
) {
    // SAFETY: Registration linked the driver and retained the bus reference.
    let private = unsafe { (*drv).p };
    if detach_devices {
        // SAFETY: The caller guarantees that autoprobe attached devices.
        unsafe { bindings::driver_detach(drv) };
    }
    // SAFETY: The bus list node is linked and the private object is live.
    unsafe { bindings::klist_del(ptr::addr_of_mut!((*private).knode_bus)) };
    // SAFETY: The kobject was initialized and owns the private allocation.
    unsafe { bindings::kobject_put(ptr::addr_of_mut!((*private).kobj)) };
    // SAFETY: The driver is no longer registered and remains caller-owned.
    unsafe { (*drv).p = ptr::null_mut() };
    // SAFETY: The registration retained this bus reference.
    unsafe { bus_put(sp) };
}

/// Adds a driver to its registered bus.
///
/// # Safety
///
/// `drv` must point to caller-owned driver storage that remains live until
/// unregistration.
unsafe fn add_driver(drv: *mut DeviceDriver) -> c_int {
    // SAFETY: The caller guarantees a valid live driver.
    let bus = unsafe { (*drv).bus };
    // SAFETY: The bus may be null or registered. A successful lookup retains a
    // reference for the driver lifetime.
    let sp = unsafe { bindings::bus_to_subsys(bus) };
    if sp.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    let private = match KBox::new(DriverPrivate::default(), GFP_KERNEL) {
        Ok(private) => KBox::into_raw(private),
        Err(_) => {
            // SAFETY: The lookup reference is no longer needed.
            unsafe { bus_put(sp) };
            return -(bindings::ENOMEM as c_int);
        }
    };

    // SAFETY: The private allocation is fresh and owned by this function.
    unsafe {
        bindings::klist_init(ptr::addr_of_mut!((*private).klist_devices), None, None);
        (*private).driver = drv;
        (*private).kobj.kset = (*sp).drivers_kset;
        (*drv).p = private;
    }

    // SAFETY: The private object is initialized and the driver name remains
    // live. Failure leaves the initialized kobject owned by the caller.
    let error = unsafe { bindings::rust_helper_driver_kobject_init_and_add(drv) };
    if error != 0 {
        // SAFETY: The initialized kobject owns the private allocation.
        unsafe { bindings::kobject_put(ptr::addr_of_mut!((*private).kobj)) };
        // SAFETY: The driver is not registered and remains caller-owned.
        unsafe { (*drv).p = ptr::null_mut() };
        // SAFETY: The registration attempt retained this bus reference.
        unsafe { bus_put(sp) };
        return error;
    }

    // SAFETY: The driver kobject is registered and the bus holds the list.
    unsafe {
        bindings::klist_add_tail(
            ptr::addr_of_mut!((*private).knode_bus),
            ptr::addr_of_mut!((*sp).klist_drivers),
        );
    }

    // SAFETY: The bus registration remains live and the driver is linked.
    if unsafe { (*sp).drivers_autoprobe() != 0 } {
        // SAFETY: The driver is fully visible to the bus.
        let error = unsafe { bindings::driver_attach(drv) };
        if error != 0 {
            // SAFETY: Registration has linked state to roll back.
            unsafe { rollback_linked_driver(drv, sp, false) };
            return error;
        }
    }

    // SAFETY: The driver is linked and its owner and name remain live.
    let error = unsafe { bindings::rust_helper_driver_module_add(drv) };
    if error != 0 {
        // SAFETY: The driver is linked and module links failed atomically.
        unsafe { rollback_linked_driver(drv, sp, true) };
        return error;
    }

    // SAFETY: The driver kobject is registered.
    let error = unsafe { bindings::rust_helper_driver_add_uevent_file(drv) };
    if error != 0 {
        // SAFETY: The registered driver owns a valid name.
        let name = unsafe { CStr::from_char_ptr((*drv).name) };
        pr_err!("driver_add: uevent attr ({}) failed\n", name);
    }

    // SAFETY: The registered bus and driver remain live.
    let error = unsafe { bindings::driver_add_groups(drv, (*bus).drv_groups) };
    if error != 0 {
        // SAFETY: The registered driver owns a valid name.
        let name = unsafe { CStr::from_char_ptr((*drv).name) };
        pr_err!("driver_add_groups({}) failed\n", name);
    }

    // SAFETY: The registered driver remains live.
    if !unsafe { (*drv).suppress_bind_attrs } {
        // SAFETY: The driver kobject is registered.
        let error = unsafe { bindings::rust_helper_driver_add_bind_files(drv) };
        if error != 0 {
            // SAFETY: The registered driver owns a valid name.
            let name = unsafe { CStr::from_char_ptr((*drv).name) };
            pr_err!("add_bind_files({}) failed\n", name);
        }
    }

    0
}

/// Removes a registered driver from its bus.
///
/// # Safety
///
/// `drv` must be registered and remain live until the removal completes.
unsafe fn remove_driver(drv: *mut DeviceDriver) {
    // SAFETY: The caller guarantees a registered live driver.
    let bus = unsafe { (*drv).bus };
    // SAFETY: A successful lookup retains one temporary bus reference.
    let sp = unsafe { bindings::bus_to_subsys(bus) };
    if sp.is_null() {
        return;
    }

    // SAFETY: The driver is registered and owns this private object.
    let private = unsafe { (*drv).p };

    // SAFETY: The driver kobject is registered.
    if !unsafe { (*drv).suppress_bind_attrs } {
        // SAFETY: Bind files were created for this registered driver.
        unsafe { bindings::rust_helper_driver_remove_bind_files(drv) };
    }

    // SAFETY: The registered bus and driver remain live.
    unsafe { bindings::driver_remove_groups(drv, (*bus).drv_groups) };
    // SAFETY: The uevent attribute belongs to this registered driver.
    unsafe { bindings::rust_helper_driver_remove_uevent_file(drv) };
    // SAFETY: The bus list node is linked.
    unsafe { bindings::klist_remove(ptr::addr_of_mut!((*private).knode_bus)) };
    // SAFETY: The driver is still registered and can detach all devices.
    unsafe { bindings::driver_detach(drv) };
    // SAFETY: Module links belong to this registered driver.
    unsafe { bindings::rust_helper_driver_module_remove(drv) };
    // SAFETY: The kobject owns the private allocation.
    unsafe { bindings::kobject_put(ptr::addr_of_mut!((*private).kobj)) };

    // SAFETY: One reference came from this lookup and one was retained when
    // the driver joined the bus.
    unsafe {
        bus_put(sp);
        bus_put(sp);
    }
}

/// Registers a device driver.
///
/// # Safety
///
/// `drv` must be null or point to valid driver storage that remains live until
/// unregistration completes.
#[export]
unsafe extern "C" fn driver_register(drv: *mut DeviceDriver) -> c_int {
    if drv.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller guarantees a valid live driver.
    let bus = unsafe { (*drv).bus };
    if bus.is_null() {
        return -(bindings::EINVAL as c_int);
    }
    // SAFETY: The caller guarantees a valid live driver.
    if unsafe { (*drv).name.is_null() } {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The bus pointer is valid and the driver remains live.
    if !unsafe { bindings::bus_is_registered(bus) } {
        // SAFETY: The driver remains live and owns a valid name.
        let driver_name = unsafe { CStr::from_char_ptr((*drv).name) };
        // SAFETY: The bus remains live and owns a valid name.
        let bus_name = unsafe { CStr::from_char_ptr((*bus).name) };
        pr_err!(
            "Driver '{}' was unable to register with bus_type '{}' because the bus was not initialized.\n",
            driver_name,
            bus_name
        );
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The bus and driver remain live for the registration call.
    let obsolete_methods = unsafe {
        ((*bus).probe.is_some() && (*drv).probe.is_some())
            || ((*bus).remove.is_some() && (*drv).remove.is_some())
            || ((*bus).shutdown.is_some() && (*drv).shutdown.is_some())
    };
    if obsolete_methods {
        // SAFETY: The driver remains live and owns a valid name.
        let name = unsafe { CStr::from_char_ptr((*drv).name) };
        pr_warn!(
            "Driver '{}' needs updating - please use bus_type methods\n",
            name
        );
    }

    // SAFETY: The bus is registered and the driver name remains live.
    let other = unsafe { bindings::driver_find((*drv).name, bus) };
    if !other.is_null() {
        // SAFETY: The driver remains live and owns a valid name.
        let name = unsafe { CStr::from_char_ptr((*drv).name) };
        pr_err!(
            "Error: Driver '{}' is already registered, aborting...\n",
            name
        );
        return -(bindings::EBUSY as c_int);
    }

    // SAFETY: The caller guarantees driver storage and all configuration
    // pointers remain live for the registration lifetime.
    let error = unsafe { add_driver(drv) };
    if error != 0 {
        return error;
    }

    // SAFETY: The driver is registered and its group list remains live.
    let error = unsafe { bindings::driver_add_groups(drv, (*drv).groups) };
    if error != 0 {
        // SAFETY: The driver is fully registered and can be removed.
        unsafe { remove_driver(drv) };
        return error;
    }

    // SAFETY: The driver kobject is registered.
    unsafe {
        bindings::kobject_uevent(
            ptr::addr_of_mut!((*(*drv).p).kobj),
            bindings::kobject_action_KOBJ_ADD,
        );
        bindings::deferred_probe_extend_timeout();
    }

    0
}

/// Unregisters a device driver.
///
/// # Safety
///
/// `drv` must be null or point to a driver returned from a successful
/// `driver_register()` call.
#[export]
unsafe extern "C" fn driver_unregister(drv: *mut DeviceDriver) {
    if drv.is_null() {
        // SAFETY: This is the diagnostic for invalid unregistration.
        unsafe { bindings::rust_helper_driver_warn_unexpected_unregister() };
        return;
    }
    // SAFETY: The caller provides valid driver storage.
    if unsafe { (*drv).p.is_null() } {
        // SAFETY: This is the diagnostic for invalid unregistration.
        unsafe { bindings::rust_helper_driver_warn_unexpected_unregister() };
        return;
    }

    // SAFETY: The caller guarantees a registered live driver.
    unsafe { bindings::driver_remove_groups(drv, (*drv).groups) };
    // SAFETY: Removing the driver releases the matching bus state.
    unsafe { remove_driver(drv) };
}
