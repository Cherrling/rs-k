// SPDX-License-Identifier: GPL-2.0-only

//! Native device registration lifecycle.
//!
//! Rust defines device initialization, addition, registration, deletion, and
//! unregistration. It owns the complete hierarchy transaction, probe
//! activation, and rollback order. C retains shared layouts and the low-level
//! parent, sysfs, PM, class, bus, devtmpfs, and device-link primitives.

use crate::alloc::{flags::GFP_KERNEL, KBox};
use crate::bindings;
use crate::ffi::c_int;
use core::ptr;
use macros::export;

type Device = bindings::device;
type DevicePrivate = bindings::device_private;
type KObject = bindings::kobject;

const EINVAL: c_int = -(bindings::EINVAL as c_int);
const ENOMEM: c_int = -(bindings::ENOMEM as c_int);

/// Tracks how far device addition progressed for reverse-order rollback.
#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
#[repr(u8)]
enum AddStage {
    Platform = 1,
    Uevent,
    Symlinks,
    Attrs,
    Bus,
    Pm,
    DevFile,
}

/// Frees a private object that has not entered the kobject release path.
///
/// # Safety
///
/// `dev` must be live and its private pointer must have come from a `KBox`.
unsafe fn free_private(dev: *mut Device) {
    // SAFETY: The caller owns the private allocation.
    let private = unsafe { (*dev).p };
    // SAFETY: No release path can observe the private pointer after this
    // assignment.
    unsafe { (*dev).p = ptr::null_mut() };
    if !private.is_null() {
        // SAFETY: The pointer was allocated by `KBox::into_raw`.
        unsafe { drop(KBox::from_raw(private)) };
    }
}

/// Drops one device reference.
///
/// # Safety
///
/// `dev` must be null or point to a live initialized device.
unsafe fn put_device(dev: *mut Device) {
    // SAFETY: The caller guarantees reference ownership.
    unsafe { bindings::put_device(dev) };
}

/// Rolls back a failed addition before the kobject entered the hierarchy.
///
/// # Safety
///
/// `dev` must have initialized private storage. `parent` must hold a
/// reference. `parent_kobj` must come from `get_device_parent()`.
unsafe fn rollback_before_kobject(
    dev: *mut Device,
    parent: *mut Device,
    parent_kobj: *mut KObject,
) {
    // SAFETY: The parent kobject was selected for this failed addition.
    unsafe { bindings::rust_helper_device_cleanup_glue_dir(dev, parent_kobj) };
    // SAFETY: Addition acquired this parent reference.
    unsafe { put_device(parent) };
    // SAFETY: The private object never entered the kobject hierarchy.
    unsafe { free_private(dev) };
}

/// Rolls back a device that entered the kobject hierarchy.
///
/// # Safety
///
/// `dev` must have initialized private storage and registered a kobject.
/// `parent` must still hold the reference acquired by addition.
unsafe fn rollback_added_device(dev: *mut Device, parent: *mut Device, stage: AddStage) {
    if stage >= AddStage::DevFile {
        // SAFETY: The dev attribute was created for this device number.
        unsafe { bindings::rust_helper_device_remove_dev_file(dev) };
    }
    if stage >= AddStage::Pm {
        // SAFETY: Device addition registered PM state.
        unsafe {
            bindings::rust_helper_device_pm_remove(dev);
            bindings::rust_helper_device_dpm_sysfs_remove(dev);
        }
    }
    if stage >= AddStage::Bus {
        // SAFETY: Device addition attached a bus.
        unsafe {
            bindings::rust_helper_device_set_driver_null(dev);
            bindings::bus_remove_device(dev);
        }
    }
    if stage >= AddStage::Attrs {
        // SAFETY: Device attribute groups were created.
        unsafe { bindings::rust_helper_device_remove_attrs(dev) };
    }
    if stage >= AddStage::Symlinks {
        // SAFETY: Device class links were created.
        unsafe { bindings::rust_helper_device_remove_class_symlinks(dev) };
    }
    if stage >= AddStage::Uevent {
        // SAFETY: The device uevent attribute was created.
        unsafe { bindings::rust_helper_device_remove_uevent_file(dev) };
    }

    // SAFETY: Platform notification followed kobject registration.
    unsafe {
        bindings::rust_helper_device_platform_notify_remove(dev);
        bindings::kobject_uevent(
            ptr::addr_of_mut!((*dev).kobj),
            bindings::kobject_action_KOBJ_REMOVE,
        );
    }

    // SAFETY: The kobject is registered and its parent remains live.
    let glue_dir = unsafe { (*dev).kobj.parent };
    // SAFETY: The kobject is registered.
    unsafe { bindings::kobject_del(ptr::addr_of_mut!((*dev).kobj)) };
    // SAFETY: Release the temporary glue-directory reference if present.
    unsafe { bindings::rust_helper_device_cleanup_glue_dir(dev, glue_dir) };
    // SAFETY: Addition acquired this parent reference.
    unsafe { put_device(parent) };
    // SAFETY: The failed device has not reached its release callback.
    unsafe { free_private(dev) };
}

/// Performs the device-addition transaction.
///
/// # Safety
///
/// `dev` must hold a live reference returned by `get_device()`.
unsafe fn add_device(dev: *mut Device) -> c_int {
    // SAFETY: The caller holds a live reference.
    if unsafe { (*dev).p.is_null() } {
        let private = match KBox::new(DevicePrivate::default(), GFP_KERNEL) {
            Ok(private) => KBox::into_raw(private),
            Err(_) => return ENOMEM,
        };
        // SAFETY: The private allocation is fresh and caller-owned.
        unsafe {
            (*dev).p = private;
            bindings::rust_helper_device_init_private(dev);
        }
    }

    // SAFETY: The device remains live and init_name points to caller-owned
    // storage while this operation runs.
    if !unsafe { (*dev).init_name.is_null() } {
        // SAFETY: The helper copies the name and clears init_name.
        unsafe { bindings::rust_helper_device_set_init_name(dev) };
    }

    // SAFETY: The device remains live.
    let mut error = if unsafe { !bindings::rust_helper_device_name(dev).is_null() } {
        0
    } else if unsafe { !(*dev).bus.is_null() && !(*(*dev).bus).dev_name.is_null() } {
        // SAFETY: The bus has an enumeration prefix.
        unsafe { bindings::rust_helper_device_set_bus_name(dev) }
    } else {
        EINVAL
    };
    if error != 0 {
        // SAFETY: The private object never entered the kobject hierarchy.
        unsafe { free_private(dev) };
        return error;
    }

    // SAFETY: The device remains live and has a name.
    unsafe { bindings::rust_helper_device_debug_add(dev) };

    // SAFETY: The parent pointer is stable for this operation.
    let parent = unsafe { bindings::get_device((*dev).parent) };
    let mut parent_kobj = ptr::null_mut();
    // SAFETY: The device and parent references remain live.
    error = unsafe { bindings::rust_helper_device_get_parent(dev, parent, &mut parent_kobj) };
    if error != 0 {
        // SAFETY: Addition acquired the parent reference.
        unsafe { put_device(parent) };
        // SAFETY: The private object never entered the kobject hierarchy.
        unsafe { free_private(dev) };
        return error;
    }
    if !parent_kobj.is_null() {
        // SAFETY: The selected parent kobject is live.
        unsafe { (*dev).kobj.parent = parent_kobj };
    }

    // SAFETY: The parent is referenced and the device remains live.
    unsafe { bindings::rust_helper_device_inherit_parent_node(dev, parent) };

    // SAFETY: The device name is set and parent relationships are valid.
    error = unsafe { bindings::rust_helper_device_kobject_add(dev) };
    if error != 0 {
        // SAFETY: The kobject was not added and the parent kobject tracks the
        // failed hierarchy transaction.
        unsafe { rollback_before_kobject(dev, parent, parent_kobj) };
        return error;
    }

    // SAFETY: The kobject is registered and platform notification is valid.
    unsafe { bindings::rust_helper_device_platform_notify(dev) };

    // SAFETY: The kobject is registered.
    error = unsafe { bindings::rust_helper_device_add_uevent_file(dev) };
    if error != 0 {
        // SAFETY: Platform notification followed kobject registration.
        unsafe { rollback_added_device(dev, parent, AddStage::Platform) };
        return error;
    }

    // SAFETY: The kobject is registered.
    error = unsafe { bindings::rust_helper_device_add_class_symlinks(dev) };
    if error != 0 {
        // SAFETY: The uevent attribute is registered.
        unsafe { rollback_added_device(dev, parent, AddStage::Uevent) };
        return error;
    }

    // SAFETY: The device hierarchy and class links are registered.
    error = unsafe { bindings::rust_helper_device_add_attrs(dev) };
    if error != 0 {
        // SAFETY: Class symlinks are registered.
        unsafe { rollback_added_device(dev, parent, AddStage::Symlinks) };
        return error;
    }

    // SAFETY: The device remains live and may have a bus.
    error = unsafe { bindings::bus_add_device(dev) };
    if error != 0 {
        // SAFETY: Device attributes are registered.
        unsafe { rollback_added_device(dev, parent, AddStage::Attrs) };
        return error;
    }

    // SAFETY: Device sysfs state is registered.
    error = unsafe { bindings::rust_helper_device_dpm_sysfs_add(dev) };
    if error != 0 {
        // SAFETY: The device was attached to its bus.
        unsafe { rollback_added_device(dev, parent, AddStage::Bus) };
        return error;
    }

    // SAFETY: PM sysfs state is registered.
    unsafe {
        bindings::rust_helper_device_pm_add(dev);
    }

    // SAFETY: The device is live and its device number is stable.
    if unsafe { bindings::rust_helper_device_has_devt(dev) } {
        // SAFETY: PM state is registered.
        error = unsafe { bindings::rust_helper_device_add_dev_file(dev) };
        if error != 0 {
            // SAFETY: Device PM state is registered.
            unsafe { rollback_added_device(dev, parent, AddStage::Pm) };
            return error;
        }

        // SAFETY: The dev attribute is registered.
        error = unsafe { bindings::rust_helper_device_add_sys_dev_entry(dev) };
        if error != 0 {
            // SAFETY: The dev attribute is registered.
            unsafe { rollback_added_device(dev, parent, AddStage::DevFile) };
            return error;
        }

        // SAFETY: The sysfs device entry is registered. Failure to create a
        // devtmpfs node is nonfatal, matching the C implementation.
        unsafe { bindings::rust_helper_device_devtmpfs_add(dev) };
    }

    // SAFETY: All subsystem registrations required before notification are
    // complete.
    unsafe {
        bindings::bus_notify(dev, bindings::bus_notifier_event_BUS_NOTIFY_ADD_DEVICE);
        bindings::kobject_uevent(
            ptr::addr_of_mut!((*dev).kobj),
            bindings::kobject_action_KOBJ_ADD,
        );
    }

    // SAFETY: Device addition can now create pending firmware links.
    unsafe { bindings::rust_helper_device_fw_devlink_add(dev) };

    // SAFETY: Probe state is protected by the device lock.
    unsafe {
        bindings::rust_helper_device_lock(dev);
        bindings::rust_helper_device_set_ready_to_probe(dev);
        bindings::rust_helper_device_unlock(dev);
    }
    // SAFETY: Probe may now run for the fully registered device.
    unsafe { bindings::bus_probe_device(dev) };
    // SAFETY: Probe has completed and firmware-link state is stable.
    unsafe { bindings::rust_helper_device_fw_devlink_maybe_unblock(dev) };

    if !parent.is_null() {
        // SAFETY: Both device-private objects are initialized and linked.
        unsafe {
            bindings::klist_add_tail(
                ptr::addr_of_mut!((*(*dev).p).knode_parent),
                ptr::addr_of_mut!((*(*parent).p).klist_children),
            );
        }
    }

    // SAFETY: The device remains live and its class pointer is stable.
    let sp = unsafe { bindings::class_to_subsys((*dev).class) };
    if !sp.is_null() {
        // SAFETY: The class lookup holds a reference for this transaction.
        unsafe {
            bindings::rust_helper_device_class_add(dev, sp);
            bindings::rust_helper_device_class_put(sp);
        }
    }

    0
}

/// Initializes a device structure.
///
/// # Safety
///
/// `dev` must be null or point to zeroed caller-owned device storage that
/// remains live until its final reference is released.
#[export]
unsafe extern "C" fn device_initialize(dev: *mut Device) {
    if dev.is_null() {
        return;
    }

    // SAFETY: The caller provides zeroed device storage.
    unsafe {
        (*dev).kobj.kset = bindings::devices_kset;
        bindings::kobject_init(
            ptr::addr_of_mut!((*dev).kobj),
            bindings::rust_helper_device_ktype(),
        );
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*dev).dma_pools));
        bindings::rust_helper_device_init_locks(dev);
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*dev).devres_head));
        bindings::rust_helper_device_init_pm(dev);
        bindings::rust_helper_device_set_node_no_numa(dev);
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*dev).links.consumers));
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*dev).links.suppliers));
        bindings::INIT_LIST_HEAD(ptr::addr_of_mut!((*dev).links.defer_sync));
        (*dev).links.status = bindings::dl_dev_state_DL_DEV_NO_DRIVER;
        bindings::rust_helper_device_init_coherent(dev);
    }
}

/// Adds an initialized device to the driver model.
///
/// # Safety
///
/// `dev` must be null or point to initialized caller-owned storage. After any
/// call, the caller must release its reference with `put_device()`.
#[export]
unsafe extern "C" fn device_add(dev: *mut Device) -> c_int {
    // SAFETY: The reference prevents release during the transaction.
    let live = unsafe { bindings::get_device(dev) };
    if live.is_null() {
        return EINVAL;
    }

    // SAFETY: The reference keeps the device live. `add_device()` owns all
    // rollback except the temporary reference.
    let error = unsafe { add_device(live) };
    // SAFETY: Release the temporary reference acquired above.
    unsafe { put_device(live) };
    error
}

/// Registers a device with the driver model.
///
/// # Safety
///
/// `dev` must be null or point to zeroed caller-owned device storage. On
/// return, any successful or failed call owns one reference that the caller
/// must release with `put_device()`.
#[export]
unsafe extern "C" fn device_register(dev: *mut Device) -> c_int {
    if dev.is_null() {
        return EINVAL;
    }

    // SAFETY: The caller provides zeroed storage.
    unsafe { device_initialize(dev) };
    // SAFETY: Initialization completed before addition.
    unsafe { device_add(dev) }
}

/// Deletes a registered device from the driver model.
///
/// # Safety
///
/// `dev` must be null or point to a device whose addition succeeded and that
/// still owns its registration reference.
#[export]
unsafe extern "C" fn device_del(dev: *mut Device) {
    if dev.is_null() {
        return;
    }

    // SAFETY: The caller guarantees a registered live device.
    let parent = unsafe { (*dev).parent };

    // SAFETY: The device remains live for the complete removal.
    unsafe {
        bindings::rust_helper_device_lock(dev);
        bindings::kill_device(dev);
        bindings::rust_helper_device_unlock(dev);
        bindings::rust_helper_device_clear_fwnode(dev);
    }

    // SAFETY: Device removal may allocate memory for notifier callbacks.
    let noio_flags = unsafe { bindings::rust_helper_device_memalloc_noio_save() };

    // SAFETY: The device is registered.
    unsafe {
        bindings::bus_notify(dev, bindings::bus_notifier_event_BUS_NOTIFY_DEL_DEVICE);
        bindings::rust_helper_device_dpm_sysfs_remove(dev);
    }

    if !parent.is_null() {
        // SAFETY: The parent-list node was linked during addition.
        unsafe { bindings::klist_del(ptr::addr_of_mut!((*(*dev).p).knode_parent)) };
    }

    // SAFETY: The device is live and its device number is stable.
    if unsafe { bindings::rust_helper_device_has_devt(dev) } {
        // SAFETY: The device number entries were created during addition.
        unsafe {
            bindings::rust_helper_device_devtmpfs_remove(dev);
            bindings::rust_helper_device_remove_sys_dev_entry(dev);
            bindings::rust_helper_device_remove_dev_file(dev);
        }
    }

    // SAFETY: The class pointer is stable for the registered device.
    let sp = unsafe { bindings::class_to_subsys((*dev).class) };
    if !sp.is_null() {
        // SAFETY: The class lookup holds a reference.
        unsafe {
            bindings::rust_helper_device_remove_class_symlinks(dev);
            bindings::rust_helper_device_class_remove(dev, sp);
            bindings::rust_helper_device_class_put(sp);
        }
    }

    // SAFETY: The device is registered and every matching subsystem entry is
    // still live at this point in teardown.
    unsafe {
        bindings::rust_helper_device_remove_uevent_file(dev);
        bindings::rust_helper_device_remove_attrs(dev);
        bindings::bus_remove_device(dev);
        bindings::rust_helper_device_pm_remove(dev);
        bindings::driver_deferred_probe_del(dev);
        bindings::rust_helper_device_platform_notify_remove(dev);
        bindings::rust_helper_device_links_purge(dev);
        bindings::devres_release_all(dev);
    }

    // SAFETY: The device is still registered while the final notification and
    // kobject removal run.
    let glue_dir = unsafe { (*dev).kobj.parent };
    // SAFETY: The device is still registered during final removal.
    unsafe {
        bindings::bus_notify(dev, bindings::bus_notifier_event_BUS_NOTIFY_REMOVED_DEVICE);
        bindings::kobject_uevent(
            ptr::addr_of_mut!((*dev).kobj),
            bindings::kobject_action_KOBJ_REMOVE,
        );
        bindings::kobject_del(ptr::addr_of_mut!((*dev).kobj));
        bindings::rust_helper_device_cleanup_glue_dir(dev, glue_dir);
        bindings::rust_helper_device_memalloc_noio_restore(noio_flags);
    }

    // SAFETY: Addition acquired this parent reference.
    unsafe { put_device(parent) };
}

/// Unregisters a device and releases its registration reference.
///
/// # Safety
///
/// `dev` must be null or point to a device whose addition succeeded.
#[export]
unsafe extern "C" fn device_unregister(dev: *mut Device) {
    if dev.is_null() {
        return;
    }

    // SAFETY: The caller provides a registered live device.
    unsafe { bindings::rust_helper_device_debug_unregistering(dev) };

    // SAFETY: The caller guarantees a successfully registered device.
    unsafe { device_del(dev) };
    // SAFETY: Drop the reference initialized by `device_initialize()`.
    unsafe { put_device(dev) };
}
