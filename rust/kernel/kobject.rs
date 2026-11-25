// SPDX-License-Identifier: GPL-2.0-only

//! Native `kobject` initialization, reference, and hierarchy lifecycle.
//!
//! Rust directly defines the public initialization, reference, removal,
//! rename, and move entry points. It owns initialization state, reference
//! acquisition, conditional acquisition, hierarchy state transitions,
//! automatic cleanup, and final-reference dispatch. The native Rust `kref` and
//! `refcount_t` implementations perform the count transitions.
//!
//! C retains export metadata, bitfield accessors, diagnostics, sysfs and kset
//! operations, allocation primitives, and the debug delayed-work boundary.

use crate::bindings;
use crate::ffi::{c_char, c_int};
use core::ptr;
use macros::export;

type Kobject = bindings::kobject;
type KobjType = bindings::kobj_type;
type Kref = bindings::kref;

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

/// Duplicates a name with the original const-name lifetime rule.
///
/// # Safety
///
/// `name` must point to a valid NUL-terminated string for the duration of the
/// call.
unsafe fn duplicate_name(name: *const c_char) -> *const c_char {
    // SAFETY: The caller guarantees a valid string.
    unsafe { bindings::kstrdup_const(name, bindings::GFP_KERNEL) }
}

/// Builds the `DEVPATH_OLD=` environment entry used by move events.
///
/// # Safety
///
/// `devpath` must point to a valid NUL-terminated string for the duration of
/// the call.
unsafe fn make_old_devpath(devpath: *const c_char) -> *mut c_char {
    const PREFIX: &[u8] = b"DEVPATH_OLD=";

    // SAFETY: The caller guarantees a valid string.
    let path_len = unsafe { bindings::strlen(devpath) };
    // SAFETY: The original implementation allocated the path length plus 15
    // bytes. The returned allocation remains owned by this function.
    let buffer =
        unsafe { bindings::__kmalloc_noprof(path_len + 15, bindings::GFP_KERNEL).cast::<c_char>() };
    if buffer.is_null() {
        return buffer;
    }

    // SAFETY: The allocation is large enough for the prefix, path, and
    // terminator. The source strings remain valid for the copies.
    unsafe {
        ptr::copy_nonoverlapping(PREFIX.as_ptr().cast::<c_char>(), buffer, PREFIX.len());
        ptr::copy_nonoverlapping(devpath, buffer.add(PREFIX.len()), path_len + 1);
    }
    buffer
}

/// Detaches a registered kobject from sysfs and its kset.
///
/// # Safety
///
/// `kobj` must point to valid, registered `kobject` storage.
unsafe fn unlink(kobj: *mut Kobject) {
    // SAFETY: The caller guarantees valid storage.
    let send_remove = unsafe {
        bindings::rust_kobject_state_add_uevent_sent(kobj)
            && !bindings::rust_kobject_state_remove_uevent_sent(kobj)
    };

    // SAFETY: The helper preserves the original sysfs group boundary.
    unsafe { bindings::rust_helper_kobject_remove_groups(kobj) };
    if send_remove {
        // SAFETY: The kobject still owns its hierarchy state.
        unsafe { bindings::kobject_uevent(kobj, bindings::kobject_action_KOBJ_REMOVE) };
    }
    // SAFETY: The helper removes the directory and drops its kernfs reference.
    unsafe { bindings::rust_helper_kobject_remove_dir(kobj) };
    // SAFETY: The caller guarantees valid storage.
    unsafe { bindings::rust_kobject_state_mark_in_sysfs(kobj, false) };
    // SAFETY: The helper removes the kset list entry and its reference.
    unsafe { bindings::rust_helper_kobject_kset_leave(kobj) };
    // SAFETY: The parent reference moves to `kobject_del()` or cleanup.
    unsafe { (*kobj).parent = ptr::null_mut() };
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
        bindings::kref_init(kref(kobj));
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
unsafe extern "C" fn kobject_init(kobj: *mut Kobject, ktype: *const KobjType) {
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
unsafe extern "C" fn kobject_get(kobj: *mut Kobject) -> *mut Kobject {
    if !kobj.is_null() {
        // SAFETY: The caller guarantees valid `kobject` storage.
        if !unsafe { bindings::rust_kobject_state_initialized(kobj) } {
            // SAFETY: The caller supplied a valid `kobject` pointer.
            unsafe { bindings::rust_kobject_warn_uninitialized(kobj, false) };
        }
        // SAFETY: The caller holds a reference to the initialized object.
        unsafe { bindings::kref_get(kref(kobj)) };
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
unsafe extern "C" fn kobject_get_unless_zero(kobj: *mut Kobject) -> *mut Kobject {
    if kobj.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    if unsafe { bindings::kref_get_unless_zero(kref(kobj)) } != 0 {
        kobj
    } else {
        ptr::null_mut()
    }
}

/// Unlinks a kobject from its hierarchy.
///
/// # Safety
///
/// `kobj` must be null or point to a valid registered kobject.
#[export]
unsafe extern "C" fn kobject_del(kobj: *mut Kobject) {
    if kobj.is_null() {
        return;
    }

    // SAFETY: The caller guarantees valid storage.
    let parent = unsafe { (*kobj).parent };
    // SAFETY: The caller guarantees a registered kobject.
    unsafe { unlink(kobj) };
    // SAFETY: `kobject_del()` consumes the parent's registration reference.
    unsafe { kobject_put(parent) };
}

/// Changes the name of a registered kobject.
///
/// # Safety
///
/// `kobj` must be null or point to a valid registered kobject. `new_name` must
/// point to a valid NUL-terminated string. The caller must serialize rename
/// operations on the same kobject.
#[export]
unsafe extern "C" fn kobject_rename(kobj: *mut Kobject, new_name: *const c_char) -> c_int {
    // SAFETY: The caller satisfies the reference contract.
    let kobj = unsafe { kobject_get(kobj) };
    if kobj.is_null() {
        return -(bindings::EINVAL as c_int);
    }
    // SAFETY: The acquired reference keeps the kobject alive.
    if unsafe { (*kobj).parent.is_null() } {
        // SAFETY: Drop the temporary reference.
        unsafe { kobject_put(kobj) };
        return -(bindings::EINVAL as c_int);
    }

    let mut duplicated_name = ptr::null();
    let mut devpath_string = ptr::null_mut();
    // SAFETY: The acquired reference keeps the kobject alive.
    let devpath = unsafe { bindings::kobject_get_path(kobj, bindings::GFP_KERNEL) };
    if devpath.is_null() {
        let error = -(bindings::ENOMEM as c_int);
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_rename(kobj, duplicated_name, devpath_string, devpath, error) };
    }

    // SAFETY: The path is valid until the cleanup below.
    devpath_string = unsafe { make_old_devpath(devpath) };
    if devpath_string.is_null() {
        let error = -(bindings::ENOMEM as c_int);
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_rename(kobj, duplicated_name, devpath_string, devpath, error) };
    }

    // SAFETY: The caller guarantees a valid new name.
    duplicated_name = unsafe { duplicate_name(new_name) };
    if duplicated_name.is_null() {
        let error = -(bindings::ENOMEM as c_int);
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_rename(kobj, duplicated_name, devpath_string, devpath, error) };
    }

    // SAFETY: The kobject remains registered and the namespace is valid for
    // this operation.
    let error =
        unsafe { bindings::sysfs_rename_dir_ns(kobj, new_name, bindings::kobject_namespace(kobj)) };
    if error != 0 {
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_rename(kobj, duplicated_name, devpath_string, devpath, error) };
    }

    // SAFETY: sysfs accepted the name. Install it and retain the old pointer
    // for release after the move event.
    let old_name = unsafe {
        let old_name = (*kobj).name;
        (*kobj).name = duplicated_name;
        old_name
    };
    let mut envp = [devpath_string, ptr::null_mut()];
    // SAFETY: The hierarchy remains valid and the environment array is
    // NUL-terminated.
    unsafe {
        bindings::kobject_uevent_env(kobj, bindings::kobject_action_KOBJ_MOVE, envp.as_mut_ptr())
    };
    // SAFETY: The pointers use the operation cleanup ownership rules.
    unsafe { duplicate_cleanup(kobj, old_name, devpath_string, devpath) };
    error
}

/// Moves a registered kobject to another parent.
///
/// # Safety
///
/// `kobj` must be null or point to a valid registered kobject. `new_parent`
/// must be null or remain alive for the duration of the call.
#[export]
unsafe extern "C" fn kobject_move(kobj: *mut Kobject, new_parent: *mut Kobject) -> c_int {
    // SAFETY: The caller satisfies the reference contract.
    let kobj = unsafe { kobject_get(kobj) };
    if kobj.is_null() {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The caller satisfies the new-parent reference contract.
    let mut new_parent = unsafe { kobject_get(new_parent) };
    if new_parent.is_null() {
        // SAFETY: The acquired kobject reference keeps the kset pointer valid.
        let kset = unsafe { (*kobj).kset };
        if !kset.is_null() {
            // SAFETY: The kset owns an embedded kobject with a valid lifetime.
            new_parent = unsafe { kobject_get(ptr::addr_of_mut!((*kset).kobj)) };
        }
    }

    let mut devpath_string = ptr::null_mut();
    // SAFETY: The acquired reference keeps the kobject alive.
    let devpath = unsafe { bindings::kobject_get_path(kobj, bindings::GFP_KERNEL) };
    if devpath.is_null() {
        let error = -(bindings::ENOMEM as c_int);
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_move(kobj, new_parent, devpath_string, devpath, error) };
    }

    // SAFETY: The path is valid until the cleanup below.
    devpath_string = unsafe { make_old_devpath(devpath) };
    if devpath_string.is_null() {
        let error = -(bindings::ENOMEM as c_int);
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_move(kobj, new_parent, devpath_string, devpath, error) };
    }

    // SAFETY: The hierarchy state and parent reference remain valid.
    let error =
        unsafe { bindings::sysfs_move_dir_ns(kobj, new_parent, bindings::kobject_namespace(kobj)) };
    if error != 0 {
        // SAFETY: The pointers use the operation cleanup ownership rules.
        return unsafe { finish_move(kobj, new_parent, devpath_string, devpath, error) };
    }

    // SAFETY: sysfs accepted the move. Transfer the acquired parent reference
    // into the kobject and drop the old parent reference.
    let old_parent = unsafe {
        let old_parent = (*kobj).parent;
        (*kobj).parent = new_parent;
        old_parent
    };
    new_parent = ptr::null_mut();
    // SAFETY: The old parent reference belonged to the old hierarchy edge.
    unsafe { kobject_put(old_parent) };

    let mut envp = [devpath_string, ptr::null_mut()];
    // SAFETY: The hierarchy remains valid and the environment array is
    // NUL-terminated.
    unsafe {
        bindings::kobject_uevent_env(kobj, bindings::kobject_action_KOBJ_MOVE, envp.as_mut_ptr())
    };
    // SAFETY: The pointers use the operation cleanup ownership rules.
    unsafe { finish_move(kobj, new_parent, devpath_string, devpath, error) }
}

/// Finishes a failed or successful rename operation.
///
/// # Safety
///
/// The pointers must come from the rename operation and obey the original
/// cleanup ownership rules.
unsafe fn finish_rename(
    kobj: *mut Kobject,
    duplicated_name: *const c_char,
    devpath_string: *mut c_char,
    devpath: *mut c_char,
    error: c_int,
) -> c_int {
    // SAFETY: The pointers use the operation cleanup ownership rules.
    unsafe { duplicate_cleanup(kobj, duplicated_name, devpath_string, devpath) };
    error
}

/// Releases temporary rename or move resources.
///
/// # Safety
///
/// The pointers must be null or valid allocations owned by the caller.
unsafe fn duplicate_cleanup(
    kobj: *mut Kobject,
    name: *const c_char,
    devpath_string: *mut c_char,
    devpath: *mut c_char,
) {
    // SAFETY: These pointers are null or owned by this operation.
    unsafe {
        bindings::kfree_const(name.cast());
        bindings::kfree(devpath_string.cast());
        bindings::kfree(devpath.cast());
        kobject_put(kobj);
    }
}

/// Finishes a failed or successful move operation.
///
/// # Safety
///
/// The pointers must come from the move operation and obey the original
/// cleanup ownership rules.
unsafe fn finish_move(
    kobj: *mut Kobject,
    new_parent: *mut Kobject,
    devpath_string: *mut c_char,
    devpath: *mut c_char,
    error: c_int,
) -> c_int {
    // SAFETY: These pointers are null or owned by this operation.
    unsafe {
        kobject_put(new_parent);
        kobject_put(kobj);
        bindings::kfree(devpath_string.cast());
        bindings::kfree(devpath.cast());
    }
    error
}

/// Cleans up a kobject after its final reference is dropped.
///
/// # Safety
///
/// `kobj` must point to valid storage with no remaining references.
unsafe fn cleanup(kobj: *mut Kobject) {
    // SAFETY: The caller guarantees valid storage.
    let (mut parent, ktype, name) = unsafe { ((*kobj).parent, (*kobj).ktype, (*kobj).name) };

    // SAFETY: The caller transfers final-reference ownership.
    if unsafe { bindings::rust_kobject_state_in_sysfs(kobj) } {
        // SAFETY: Automatic cleanup must remove the hierarchy before release.
        unsafe { unlink(kobj) };
    } else {
        parent = ptr::null_mut();
    }

    if !ktype.is_null() {
        // SAFETY: The ktype remains valid for the kobject lifetime.
        let release = unsafe { (*ktype).release };
        if let Some(release) = release {
            // SAFETY: The final reference transfers release ownership.
            unsafe { release(kobj) };
        }
    }

    // SAFETY: The name pointer was captured before the release callback.
    unsafe { bindings::kfree_const(name.cast()) };
    // SAFETY: The saved parent reference is consumed by final cleanup.
    unsafe { kobject_put(parent) };
}

#[cfg(CONFIG_DEBUG_KOBJECT_RELEASE)]
unsafe extern "C" fn delayed_cleanup(work: *mut bindings::work_struct) {
    // SAFETY: The work item is embedded in `kobject::release`.
    let delayed = unsafe { crate::container_of!(work, bindings::delayed_work, work) };
    // SAFETY: The delayed work is embedded in the released kobject.
    let kobj = unsafe { crate::container_of!(delayed, Kobject, release) };
    // SAFETY: The delayed callback owns the final cleanup.
    unsafe { cleanup(kobj) };
}

#[cfg(CONFIG_DEBUG_KOBJECT_RELEASE)]
unsafe fn schedule_delayed_cleanup(kobj: *mut Kobject) {
    // SAFETY: The random helper accepts the fixed nonzero bound.
    let random = unsafe { bindings::__get_random_u32_below(4) };
    let delay = (bindings::HZ + bindings::HZ * random) as crate::ffi::c_ulong;
    // SAFETY: The helper initializes and queues the embedded work item.
    unsafe {
        bindings::rust_helper_kobject_schedule_delayed_release(kobj, Some(delayed_cleanup), delay)
    };
}

/// Dispatches cleanup on the final kobject reference.
///
/// # Safety
///
/// `kref` must be embedded in a valid kobject whose final reference was
/// dropped.
unsafe extern "C" fn release(kref: *mut Kref) {
    // SAFETY: The callback contract embeds this kref in a kobject.
    let kobj = unsafe { crate::container_of!(kref, Kobject, kref) };
    #[cfg(CONFIG_DEBUG_KOBJECT_RELEASE)]
    // SAFETY: The final reference transfers ownership to delayed cleanup.
    unsafe {
        schedule_delayed_cleanup(kobj)
    };
    #[cfg(not(CONFIG_DEBUG_KOBJECT_RELEASE))]
    // SAFETY: The final reference transfers ownership to immediate cleanup.
    unsafe {
        cleanup(kobj)
    };
}

/// Releases a kobject reference and dispatches cleanup on the final one.
///
/// # Safety
///
/// `kobj` must be null or point to valid `kobject` storage. A non-null object
/// must be initialized and the caller must hold a reference.
#[export]
unsafe extern "C" fn kobject_put(kobj: *mut Kobject) {
    if kobj.is_null() {
        return;
    }

    // SAFETY: The caller guarantees valid `kobject` storage.
    if !unsafe { bindings::rust_kobject_state_initialized(kobj) } {
        // SAFETY: The caller supplied a valid `kobject` pointer.
        unsafe { bindings::rust_kobject_warn_uninitialized(kobj, true) };
    }
    // SAFETY: The caller holds a reference to the initialized object.
    unsafe { bindings::kref_put(kref(kobj), Some(release)) };
}
