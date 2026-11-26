// SPDX-License-Identifier: GPL-2.0

//! Native namespace proxy lifecycle.
//!
//! Rust owns namespace copy selection, partial-failure cleanup, proxy
//! publication, task switching, reference release ordering, and credential
//! namespace accounting. C retains the shared layouts, slab allocator,
//! namespace copy and put primitives, task locking, capability checks,
//! timens transitions, and active-reference primitives.

use crate::bindings;
use core::{ffi::c_int, ptr};
use macros::export;

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Cred = bindings::cred;
type NsProxy = bindings::nsproxy;
type Task = bindings::task_struct;
type UserNamespace = bindings::user_namespace;
type FsStruct = bindings::fs_struct;
type MntNamespace = bindings::mnt_namespace;
type UtsNamespace = bindings::uts_namespace;
type IpcNamespace = bindings::ipc_namespace;
type PidNamespace = bindings::pid_namespace;
type CgroupNamespace = bindings::cgroup_namespace;
type Net = bindings::net;
type TimeNamespace = bindings::time_namespace;

const FAIL_ALLOC: u32 = 1;
const FAIL_MNT: u32 = 2;
const FAIL_UTS: u32 = 3;
const FAIL_IPC: u32 = 4;
const FAIL_PID: u32 = 5;
const FAIL_CGROUP: u32 = 6;
const FAIL_NET: u32 = 7;
const FAIL_TIME: u32 = 8;

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static CREATE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static COPY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static SWITCH_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static FREE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static RELEASE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static FAILURE_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
static NEXT_FAILURE: AtomicU32 = AtomicU32::new(0);

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_create() {
    CREATE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_create() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_copy() {
    COPY_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_copy() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_switch() {
    SWITCH_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_switch() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_free() {
    FREE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_free() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_release() {
    RELEASE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_release() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn record_failure() {
    FAILURE_COUNT.fetch_add(1, Ordering::Relaxed);
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn record_failure() {}

#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
fn requested_failure(stage: u32) -> bool {
    NEXT_FAILURE
        .compare_exchange(stage, 0, Ordering::AcqRel, Ordering::Relaxed)
        .is_ok()
}

#[cfg(not(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST))]
fn requested_failure(_stage: u32) -> bool {
    false
}

fn injected_failure(stage: u32) -> bool {
    if requested_failure(stage) {
        record_failure();
        true
    } else {
        false
    }
}

fn is_error<T>(value: *const T) -> bool {
    // SAFETY: The helper only classifies the pointer value.
    unsafe { bindings::rust_helper_nsproxy_is_err(value.cast()) }
}

fn ptr_error<T>(value: *const T) -> c_int {
    // SAFETY: This is called only after `is_error()` found an error pointer.
    unsafe { bindings::rust_helper_nsproxy_ptr_err(value.cast()) as c_int }
}

fn error_ptr<T>(error: c_int) -> *mut T {
    // SAFETY: The helper encodes a small negative errno in pointer form.
    unsafe { bindings::rust_helper_nsproxy_err_ptr(error as isize).cast() }
}

/// Releases one mount namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_mnt(ns: *mut MntNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_mnt(ns) };
    record_release();
}

/// Releases one UTS namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_uts(ns: *mut UtsNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_uts(ns) };
    record_release();
}

/// Releases one IPC namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_ipc(ns: *mut IpcNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_ipc(ns) };
    record_release();
}

/// Releases one PID namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_pid(ns: *mut PidNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_pid(ns) };
    record_release();
}

/// Releases one cgroup namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_cgroup(ns: *mut CgroupNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_cgroup(ns) };
    record_release();
}

/// Releases one network namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_net(ns: *mut Net) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_net(ns) };
    record_release();
}

/// Releases one time namespace reference.
///
/// # Safety
///
/// `ns` must contain a live reference owned by the caller.
unsafe fn put_time(ns: *mut TimeNamespace) {
    // SAFETY: The caller transfers one namespace reference.
    unsafe { bindings::rust_helper_nsproxy_put_time(ns) };
    record_release();
}

/// Releases an unpublished or detached namespace proxy object.
///
/// # Safety
///
/// The caller must own the final proxy reference and must have released every
/// namespace reference stored in `ns`.
unsafe fn free_proxy(ns: *mut NsProxy) {
    // SAFETY: All field references have been released.
    unsafe { bindings::rust_helper_nsproxy_free_object(ns) };
}

/// Creates a namespace proxy and every namespace contained in it.
///
/// # Safety
///
/// `task` must be a live task. `new_fs` must be live when non-NULL.
#[export]
unsafe extern "C" fn create_new_namespaces(
    flags: u64,
    task: *mut Task,
    user_ns: *mut UserNamespace,
    new_fs: *mut FsStruct,
) -> *mut NsProxy {
    record_create();

    let new = if injected_failure(FAIL_ALLOC) {
        ptr::null_mut()
    } else {
        // SAFETY: The namespace proxy cache is initialized.
        unsafe { bindings::rust_helper_nsproxy_alloc() }
    };
    if new.is_null() {
        return error_ptr(-(bindings::ENOMEM as c_int));
    }

    // SAFETY: The caller provides a live source task.
    let old = unsafe { bindings::rust_helper_nsproxy_task(task) };

    let mnt = if injected_failure(FAIL_MNT) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source task and fs struct are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_mnt(
                flags,
                bindings::rust_helper_nsproxy_mnt(old),
                user_ns,
                new_fs,
            )
        }
    };
    if is_error(mnt) {
        let error = ptr_error(mnt);
        // SAFETY: No namespace reference was acquired.
        unsafe { free_proxy(new) };
        return error_ptr(error);
    }

    let uts = if injected_failure(FAIL_UTS) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_uts(
                flags,
                bindings::rust_helper_nsproxy_uts(old),
                user_ns,
            )
        }
    };
    if is_error(uts) {
        let error = ptr_error(uts);
        // SAFETY: The mount namespace copy is the only acquired reference.
        unsafe {
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    let ipc = if injected_failure(FAIL_IPC) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_ipc(
                flags,
                bindings::rust_helper_nsproxy_ipc(old),
                user_ns,
            )
        }
    };
    if is_error(ipc) {
        let error = ptr_error(ipc);
        // SAFETY: UTS and mount copies own one reference each.
        unsafe {
            put_uts(uts);
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    let pid = if injected_failure(FAIL_PID) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_pid(
                flags,
                bindings::rust_helper_nsproxy_pid(old),
                user_ns,
            )
        }
    };
    if is_error(pid) {
        let error = ptr_error(pid);
        // SAFETY: IPC, UTS, and mount copies own one reference each.
        unsafe {
            put_ipc(ipc);
            put_uts(uts);
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    let cgroup = if injected_failure(FAIL_CGROUP) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_cgroup(
                flags,
                bindings::rust_helper_nsproxy_cgroup(old),
                user_ns,
            )
        }
    };
    if is_error(cgroup) {
        let error = ptr_error(cgroup);
        // SAFETY: PID, IPC, UTS, and mount copies own one reference each.
        unsafe {
            put_pid(pid);
            put_ipc(ipc);
            put_uts(uts);
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    let net = if injected_failure(FAIL_NET) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_net(
                flags,
                bindings::rust_helper_nsproxy_net(old),
                user_ns,
            )
        }
    };
    if is_error(net) {
        let error = ptr_error(net);
        // SAFETY: Cgroup, PID, IPC, UTS, and mount copies own one reference.
        unsafe {
            put_cgroup(cgroup);
            put_pid(pid);
            put_ipc(ipc);
            put_uts(uts);
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    let time_for_children = if injected_failure(FAIL_TIME) {
        error_ptr(-(bindings::ENOMEM as c_int))
    } else {
        // SAFETY: The source proxy and user namespace are live.
        unsafe {
            bindings::rust_helper_nsproxy_copy_time(
                flags,
                bindings::rust_helper_nsproxy_time_for_children(old),
                user_ns,
            )
        }
    };
    if is_error(time_for_children) {
        let error = ptr_error(time_for_children);
        // SAFETY: Net, cgroup, PID, IPC, UTS, and mount own one reference.
        unsafe {
            put_net(net);
            put_cgroup(cgroup);
            put_pid(pid);
            put_ipc(ipc);
            put_uts(uts);
            put_mnt(mnt);
            free_proxy(new);
        }
        return error_ptr(error);
    }

    // SAFETY: The source proxy remains live through this copy.
    let time =
        unsafe { bindings::rust_helper_nsproxy_get_time(bindings::rust_helper_nsproxy_time(old)) };

    // SAFETY: `new` is owned by this path and remains uninitialized until the
    // complete copy sequence succeeds.
    unsafe {
        bindings::rust_helper_nsproxy_set_mnt(new, mnt);
        bindings::rust_helper_nsproxy_set_uts(new, uts);
        bindings::rust_helper_nsproxy_set_ipc(new, ipc);
        bindings::rust_helper_nsproxy_set_pid(new, pid);
        bindings::rust_helper_nsproxy_set_cgroup(new, cgroup);
        bindings::rust_helper_nsproxy_set_net(new, net);
        bindings::rust_helper_nsproxy_set_time_for_children(new, time_for_children);
        bindings::rust_helper_nsproxy_set_time(new, time);
    }

    new
}

/// Releases every namespace owned by a detached proxy and the proxy itself.
///
/// # Safety
///
/// `ns` must be a live proxy with no remaining proxy references.
#[export]
unsafe extern "C" fn nsproxy_free(ns: *mut NsProxy) {
    record_free();

    // SAFETY: The caller owns the complete proxy and all its references.
    unsafe {
        let mnt = bindings::rust_helper_nsproxy_mnt(ns);
        let uts = bindings::rust_helper_nsproxy_uts(ns);
        let ipc = bindings::rust_helper_nsproxy_ipc(ns);
        let pid = bindings::rust_helper_nsproxy_pid(ns);
        let time = bindings::rust_helper_nsproxy_time(ns);
        let time_for_children = bindings::rust_helper_nsproxy_time_for_children(ns);
        let cgroup = bindings::rust_helper_nsproxy_cgroup(ns);
        let net = bindings::rust_helper_nsproxy_net(ns);

        put_mnt(mnt);
        put_uts(uts);
        put_ipc(ipc);
        put_pid(pid);
        put_time(time);
        put_time(time_for_children);
        put_cgroup(cgroup);
        put_net(net);
        free_proxy(ns);
    }
}

/// Deactivates and releases the final namespace proxy reference.
///
/// # Safety
///
/// `ns` must be a live proxy whose proxy count reached zero.
#[export]
unsafe extern "C" fn deactivate_nsproxy(ns: *mut NsProxy) {
    // SAFETY: The caller owns the final proxy reference.
    unsafe {
        bindings::rust_helper_nsproxy_active_put(ns);
        nsproxy_free(ns);
    }
}

/// Copies the namespace proxy for a task created by clone or fork.
///
/// # Safety
///
/// `task` must be the new task before it becomes visible to other tasks.
#[export]
unsafe extern "C" fn copy_namespaces(flags: u64, task: *mut Task) -> c_int {
    record_copy();

    // SAFETY: The caller provides a live new task with an inherited proxy.
    let old = unsafe { bindings::rust_helper_nsproxy_task(task) };
    // SAFETY: The task has a live objective credential.
    let user_ns = unsafe { bindings::rust_helper_nsproxy_task_user_ns(task) };
    // SAFETY: The helper returns a constant namespace flag mask.
    let nonuser_flags = unsafe { bindings::rust_helper_nsproxy_nonuser_flags() };

    if flags & nonuser_flags == 0 {
        let clone_vm = u64::from(bindings::CLONE_VM);
        // SAFETY: The inherited proxy remains live until publication.
        if flags & clone_vm != 0 || unsafe { bindings::rust_helper_nsproxy_time_equal(old) } {
            // SAFETY: The new task will own one additional proxy reference.
            unsafe { bindings::rust_helper_nsproxy_get(old) };
            return 0;
        }
    } else {
        // SAFETY: The task credential owns a live user namespace.
        let capable = unsafe { bindings::rust_helper_nsproxy_capable(user_ns) };
        if !capable {
            return -(bindings::EPERM as c_int);
        }
    }

    let new_ipc = u64::from(bindings::CLONE_NEWIPC);
    let sysvsem = u64::from(bindings::CLONE_SYSVSEM);
    if flags & (new_ipc | sysvsem) == new_ipc | sysvsem {
        return -(bindings::EINVAL as c_int);
    }

    // SAFETY: The new task and its filesystem state remain live.
    let new = unsafe {
        create_new_namespaces(
            flags,
            task,
            user_ns,
            bindings::rust_helper_nsproxy_task_fs(task),
        )
    };
    if is_error(new) {
        return ptr_error(new);
    }

    let clone_vm = u64::from(bindings::CLONE_VM);
    if flags & clone_vm == 0 {
        // SAFETY: The new proxy and task are live and not yet published.
        unsafe { bindings::rust_helper_nsproxy_timens_on_fork(new, task) };
    }

    // SAFETY: The new proxy is ready to become active.
    unsafe {
        bindings::rust_helper_nsproxy_active_get(new);
        bindings::rust_helper_nsproxy_set_task(task, new);
    }
    0
}

/// Creates the namespace proxy used by an unshare operation.
///
/// # Safety
///
/// `new_nsp` must be writable. `new_cred` and `new_fs` must be live when
/// non-NULL.
#[export]
unsafe extern "C" fn unshare_nsproxy_namespaces(
    unshare_flags: usize,
    new_nsp: *mut *mut NsProxy,
    new_cred: *mut Cred,
    new_fs: *mut FsStruct,
) -> c_int {
    let mut flags = unshare_flags as u64;
    // SAFETY: This helper exposes the namespace mask without choosing policy.
    let nonuser_flags = unsafe { bindings::rust_helper_nsproxy_nonuser_flags() };
    if flags & nonuser_flags == 0 {
        return 0;
    }

    // SAFETY: The selected credential or current task has a live user ns.
    let user_ns = unsafe {
        if new_cred.is_null() {
            bindings::rust_helper_nsproxy_current_user_ns()
        } else {
            bindings::rust_helper_nsproxy_cred_user_ns(new_cred)
        }
    };
    // SAFETY: The selected credential or current task owns a live user
    // namespace.
    let capable = unsafe { bindings::rust_helper_nsproxy_capable(user_ns) };
    if !capable {
        return -(bindings::EPERM as c_int);
    }

    // SAFETY: Both flags describe the same 32-bit unshare request bit.
    let empty_flag = unsafe { bindings::rust_helper_nsproxy_empty_mntns_flag() };
    if flags & empty_flag != 0 {
        // SAFETY: This exposes the unique 64-bit clone flag.
        let clone_empty = unsafe { bindings::rust_helper_nsproxy_clone_empty_mntns_flag() };
        flags &= !empty_flag;
        flags |= clone_empty;
    }

    // SAFETY: The current task is live for the complete creation.
    let task = unsafe { bindings::rust_helper_nsproxy_current() };
    // SAFETY: The selected filesystem state is live.
    let fs = unsafe {
        if new_fs.is_null() {
            bindings::rust_helper_nsproxy_current_fs()
        } else {
            new_fs
        }
    };
    // SAFETY: The source task and selected namespace owners remain live.
    let new = unsafe { create_new_namespaces(flags, task, user_ns, fs) };
    if is_error(new) {
        let error = ptr_error(new);
        // SAFETY: The caller only inspects the pointer after seeing nonzero
        // return, matching the original error-pointer contract.
        unsafe { *new_nsp = new };
        return error;
    }

    // SAFETY: The caller supplies writable output storage.
    unsafe { *new_nsp = new };
    0
}

/// Replaces a task's namespace proxy under the task lock.
///
/// # Safety
///
/// The caller must hold the task's lifetime through this operation. `new`
/// must be a live proxy when non-NULL.
#[export]
unsafe extern "C" fn switch_task_namespaces(task: *mut Task, new: *mut NsProxy) {
    record_switch();

    // SAFETY: This entry point has the original sleepable context contract.
    unsafe { bindings::rust_helper_nsproxy_might_sleep() };
    if !new.is_null() {
        // SAFETY: The caller transfers a proxy that must remain live.
        unsafe { bindings::rust_helper_nsproxy_active_get(new) };
    }

    // SAFETY: The task remains live and the helper protects publication.
    let old = unsafe { bindings::rust_helper_nsproxy_swap_task(task, new) };
    if !old.is_null() {
        // SAFETY: The task transferred its old proxy reference.
        unsafe { bindings::rust_helper_nsproxy_put(old) };
    }
}

/// Releases the namespace proxy held by an exiting task.
///
/// # Safety
///
/// `task` must be exiting and must not race with another namespace switch.
#[export]
unsafe extern "C" fn exit_nsproxy_namespaces(task: *mut Task) {
    // SAFETY: The caller guarantees the exiting-task contract.
    unsafe { switch_task_namespaces(task, ptr::null_mut()) };
}

/// Moves the active user-namespace reference during credential commit.
///
/// # Safety
///
/// Both credentials must be live and their user namespaces must remain live
/// through this transition.
#[export]
unsafe extern "C" fn switch_cred_namespaces(old: *const Cred, new: *const Cred) {
    // SAFETY: Both credentials remain live through the commit transition.
    unsafe {
        bindings::rust_helper_nsproxy_user_ns_active_get(
            bindings::rust_helper_nsproxy_cred_user_ns(new),
        );
        bindings::rust_helper_nsproxy_user_ns_active_put(
            bindings::rust_helper_nsproxy_cred_user_ns(old),
        );
    }
}

/// Acquires the active user-namespace reference for a new task credential.
///
/// # Safety
///
/// `task` must have a live objective credential.
#[export]
unsafe extern "C" fn get_cred_namespaces(task: *mut Task) {
    // SAFETY: The task and its objective credential remain live.
    unsafe {
        bindings::rust_helper_nsproxy_user_ns_active_get(
            bindings::rust_helper_nsproxy_task_user_ns(task),
        )
    };
}

/// Releases the active user-namespace reference for an exiting task.
///
/// # Safety
///
/// `task` must still own the objective credential reference.
#[export]
unsafe extern "C" fn exit_cred_namespaces(task: *mut Task) {
    // SAFETY: The task and its objective credential remain live.
    unsafe {
        bindings::rust_helper_nsproxy_user_ns_active_put(
            bindings::rust_helper_nsproxy_task_user_ns(task),
        )
    };
}

/// Applies a pending time namespace when a task completes exec.
///
/// # Safety
///
/// This function has the original exec namespace context requirements.
#[export]
unsafe extern "C" fn exec_task_namespaces() -> c_int {
    // SAFETY: The current task is live in exec context.
    let task = unsafe { bindings::rust_helper_nsproxy_current() };
    // SAFETY: The current task has a live namespace proxy.
    let old = unsafe { bindings::rust_helper_nsproxy_task(task) };
    // SAFETY: The current proxy is live for this comparison.
    if unsafe { bindings::rust_helper_nsproxy_time_equal(old) } {
        return 0;
    }

    // SAFETY: The current task and filesystem state remain live.
    let new = unsafe {
        create_new_namespaces(
            0,
            task,
            bindings::rust_helper_nsproxy_current_user_ns(),
            bindings::rust_helper_nsproxy_current_fs(),
        )
    };
    if is_error(new) {
        return ptr_error(new);
    }

    // SAFETY: The new proxy and current task are live.
    unsafe {
        bindings::rust_helper_nsproxy_timens_on_fork(new, task);
        switch_task_namespaces(task, new);
    }
    0
}

/// Returns the number of namespace proxy creations observed.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_create_count() -> u32 {
    CREATE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of namespace copy entries observed.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_copy_count() -> u32 {
    COPY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of task proxy switches observed.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_switch_count() -> u32 {
    SWITCH_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of namespace proxy frees observed.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_free_count() -> u32 {
    FREE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of namespace field references released by Rust.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_release_count() -> u32 {
    RELEASE_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of injected failures observed.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_failure_count() -> u32 {
    FAILURE_COUNT.load(Ordering::Relaxed)
}

/// Requests one failure at the selected namespace copy stage.
#[cfg(CONFIG_NSPROXY_LIFECYCLE_KUNIT_TEST)]
#[export]
extern "C" fn rust_nsproxy_test_fail_next(stage: u32) {
    NEXT_FAILURE.store(stage, Ordering::Release);
}
