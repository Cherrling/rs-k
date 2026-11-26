// SPDX-License-Identifier: GPL-2.0-only

//! Native closure reference-count and wait lifecycle.
//!
//! Rust owns remaining-count transitions, guard validation, wait-list
//! membership and ordering, callback rescheduling, destructor and parent
//! release ordering, synchronous handoff, and timeout rollback. C retains the
//! shared layout, architecture atomics and barriers, lock-less list
//! primitives, workqueue queuing, task-state and scheduler primitives, and
//! locking diagnostics.

use crate::{
    bindings, container_of,
    ffi::{c_int, c_long, c_ulong},
};
use core::{mem::MaybeUninit, ptr};
use macros::export;

type Closure = bindings::closure;
type WaitList = bindings::closure_waitlist;
type Syncer = bindings::closure_syncer;
type Callback = bindings::closure_fn;
type Work = bindings::work_struct;

const REMAINING_MASK: c_int = (1 << 26) - 1;
const DESTRUCTOR: c_int = bindings::closure_state_CLOSURE_DESTRUCTOR as c_int;
const WAITING: c_int = bindings::closure_state_CLOSURE_WAITING as c_int;
const RUNNING: c_int = bindings::closure_state_CLOSURE_RUNNING as c_int;
const REMAINING_INITIALIZER: c_int = 1 | RUNNING;

/// Reads the closure remaining count.
///
/// # Safety
///
/// `cl` must point to a valid closure.
#[inline]
unsafe fn remaining(cl: *mut Closure) -> c_int {
    // SAFETY: The caller guarantees a valid closure.
    unsafe { bindings::rust_closure_atomic_read(cl) }
}

/// Writes the closure remaining count.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must own the transition.
#[inline]
unsafe fn set_remaining(cl: *mut Closure, value: c_int) {
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { bindings::rust_closure_atomic_set(cl, value) };
}

/// Adds to the closure remaining count.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must own the transition.
#[inline]
unsafe fn add_remaining(cl: *mut Closure, value: c_int) {
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { bindings::rust_closure_atomic_add(cl, value) };
}

/// Subtracts from the closure remaining count with release ordering.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must own the transition.
#[inline]
unsafe fn sub_remaining_release(cl: *mut Closure, value: c_int) -> c_int {
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { bindings::rust_closure_atomic_sub_return_release(cl, value) }
}

/// Decrements the closure remaining count with release ordering.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must own the transition.
#[inline]
unsafe fn dec_remaining_release(cl: *mut Closure) -> c_int {
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { bindings::rust_closure_atomic_dec_return_release(cl) }
}

/// Attempts one closure count compare-exchange.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must own the transition.
#[inline]
unsafe fn cmpxchg_remaining(cl: *mut Closure, old: c_int, new: c_int) -> c_int {
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { bindings::rust_closure_atomic_cmpxchg(cl, old, new) }
}

/// Returns the callback stored in the closure work union.
///
/// # Safety
///
/// `cl` must point to a valid closure.
#[inline]
unsafe fn callback(cl: *mut Closure) -> Callback {
    // SAFETY: The caller guarantees a valid closure.
    unsafe { (*cl).__bindgen_anon_1.__bindgen_anon_1.fn_ }
}

/// Stores a callback and workqueue in the closure work union.
///
/// # Safety
///
/// `cl` must point to a valid closure. The caller must not use the other
/// members of the work union concurrently.
#[inline]
unsafe fn set_callback(cl: *mut Closure, function: Callback, wq: *mut bindings::workqueue_struct) {
    // SAFETY: The caller guarantees a valid closure and exclusive union use.
    unsafe {
        (*cl).__bindgen_anon_1.__bindgen_anon_1.fn_ = function;
        (*cl).__bindgen_anon_1.__bindgen_anon_1.wq = wq;
        bindings::rust_closure_set_ip(cl);
    }
}

/// Returns the closure work item.
///
/// # Safety
///
/// `cl` must point to a valid closure.
#[inline]
unsafe fn work(cl: *mut Closure) -> *mut Work {
    // SAFETY: The caller guarantees a valid closure.
    unsafe { ptr::addr_of_mut!((*cl).__bindgen_anon_1.work) }
}

/// Invokes one closure callback.
///
/// # Safety
///
/// `function` must be callable with `work`, and `work` must remain valid for
/// the callback duration.
#[inline]
unsafe fn call_callback(function: Callback, work: *mut Work) {
    if let Some(function) = function {
        // SAFETY: The caller guarantees a callable callback and valid work.
        unsafe { function(work) };
    }
}

/// Validates guard bits and reports inconsistent zero transitions.
///
/// # Safety
///
/// The caller must pass a value obtained from the closure remaining count.
#[inline]
unsafe fn validate_flags(flags: c_int) {
    // SAFETY: The caller provides a value from the closure state machine.
    unsafe { bindings::rust_closure_put_after_sub_checks(flags) };
}

/// Applies the closure transition after one subtraction.
///
/// # Safety
///
/// `cl` must remain valid until the transition and any synchronous callback
/// or parent release completes.
unsafe fn put_after_sub(cl: *mut Closure, flags: c_int) {
    // SAFETY: The caller provides a value from the closure state machine.
    unsafe { validate_flags(flags) };

    if flags & REMAINING_MASK != 0 {
        return;
    }

    // SAFETY: This helper has no additional requirements.
    unsafe { bindings::rust_closure_acquire_after_ctrl_dep() };
    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { (*cl).closure_get_happened = false };

    // SAFETY: The caller guarantees a valid closure.
    let function = unsafe { callback(cl) };
    if function.is_some() && flags & DESTRUCTOR == 0 {
        // SAFETY: The caller guarantees a valid closure and transition ownership.
        unsafe { set_remaining(cl, REMAINING_INITIALIZER) };
        // SAFETY: The callback and workqueue fields are installed.
        unsafe { bindings::rust_closure_queue(cl) };
        return;
    }

    // SAFETY: The caller guarantees a valid closure.
    let parent = unsafe { (*cl).parent };
    #[cfg(CONFIG_DEBUG_CLOSURES)]
    // SAFETY: The closure reached its final reference.
    unsafe {
        bindings::rust_closure_debug_unregister(cl)
    };
    // SAFETY: The callback may reclaim the closure after this point.
    unsafe { call_callback(function, work(cl)) };
    if !parent.is_null() {
        // SAFETY: The closure owns one reference on its parent.
        unsafe { closure_put(parent) };
    }
}

/// Converts one reversed list node back to its closure.
///
/// # Safety
///
/// `node` must be the embedded wait-list node of a live closure.
#[inline]
unsafe fn node_closure(node: *mut bindings::llist_node) -> *mut Closure {
    // SAFETY: The caller guarantees the node is embedded in a live closure.
    unsafe { container_of!(node, Closure, __bindgen_anon_1.__bindgen_anon_1.list) }
}

/// Wakes the task waiting for one synchronous closure transition.
///
/// # Safety
///
/// `work` must be embedded in a live closure. The closure must contain a live
/// syncer with a published task pointer.
unsafe extern "C" fn sync_callback(work: *mut Work) {
    // SAFETY: The callback contract embeds `work` in a closure.
    let cl = unsafe { container_of!(work, Closure, __bindgen_anon_1.work) };
    // SAFETY: A synchronous waiter installed a live syncer before queuing.
    let syncer = unsafe { (*cl).__bindgen_anon_1.__bindgen_anon_1.s };

    // SAFETY: The RCU read-side section protects the published task pointer.
    unsafe { bindings::rcu_read_lock() };
    // SAFETY: The waiter keeps the syncer live until this callback completes.
    let task = unsafe { ptr::read_volatile(ptr::addr_of!((*syncer).task)) };
    // SAFETY: The waiter keeps the syncer and task live.
    unsafe { ptr::write_volatile(ptr::addr_of_mut!((*syncer).done), 1) };
    // SAFETY: The published task pointer references a live task.
    unsafe { bindings::wake_up_process(task) };
    // SAFETY: This function acquired the RCU read-side section.
    unsafe { bindings::rcu_read_unlock() };
}

/// Initializes a stack syncer for the current task.
///
/// # Safety
///
/// The returned syncer must remain valid until all closure callbacks finish.
unsafe fn init_syncer(syncer: *mut Syncer) {
    // SAFETY: The caller provides writable syncer storage.
    unsafe {
        (*syncer).task = bindings::get_current();
        (*syncer).done = 0;
    }
}

/// Waits until a synchronous closure callback publishes completion.
///
/// # Safety
///
/// `syncer` must remain valid. The caller must permit sleeping.
unsafe fn wait_for_syncer(syncer: *mut Syncer) {
    loop {
        // SAFETY: The caller provides a sleepable task context.
        unsafe { bindings::rust_closure_set_current_state(bindings::TASK_UNINTERRUPTIBLE) };
        // SAFETY: The syncer remains live for the wait.
        if unsafe { ptr::read_volatile(ptr::addr_of!((*syncer).done)) } != 0 {
            break;
        }
        // SAFETY: The caller permits sleeping.
        unsafe { bindings::rust_closure_schedule() };
    }

    // SAFETY: The wait completed and the current task is runnable again.
    unsafe { bindings::rust_closure_set_current_running() };
}

/// Subtracts closure state and dispatches the resulting lifecycle transition.
///
/// # Safety
///
/// `cl` must point to a valid closure. `v` must be a value that preserves the
/// public closure guard-bit contract.
#[export]
unsafe extern "C" fn closure_sub(cl: *mut Closure, v: c_int) {
    // SAFETY: The caller provides the public `closure_sub()` contract.
    let flags = unsafe { sub_remaining_release(cl, v) };
    // SAFETY: The caller keeps the closure live through this transition.
    unsafe { put_after_sub(cl, flags) };
}

/// Drops one closure reference.
///
/// # Safety
///
/// `cl` must point to a valid closure with a reference owned by the caller.
#[export]
unsafe extern "C" fn closure_put(cl: *mut Closure) {
    // SAFETY: The caller provides the public `closure_put()` contract.
    let flags = unsafe { dec_remaining_release(cl) };
    // SAFETY: The caller keeps the closure live through this transition.
    unsafe { put_after_sub(cl, flags) };
}

/// Reverses a detached lock-less chain in place.
///
/// # Safety
///
/// Every node must be valid and detached from concurrent producers.
unsafe fn reverse_list(mut node: *mut bindings::llist_node) -> *mut bindings::llist_node {
    let mut reversed = ptr::null_mut();

    while !node.is_null() {
        // SAFETY: The caller provides a valid detached chain.
        let next = unsafe { (*node).next };
        // SAFETY: The detached node is writable.
        unsafe { (*node).next = reversed };
        reversed = node;
        node = next;
    }

    reversed
}

/// Wakes every closure on a wait list in FIFO order.
///
/// # Safety
///
/// `wait_list` must remain valid and concurrent producers must satisfy the
/// lock-less list contract.
#[export]
unsafe extern "C" fn __closure_wake_up(wait_list: *mut WaitList) {
    // SAFETY: The caller provides the public wait-list contract.
    let list = unsafe { bindings::rust_closure_llist_del_all(wait_list) };
    // SAFETY: The detached chain is no longer visible to producers.
    let mut node = unsafe { reverse_list(list) };

    while !node.is_null() {
        // SAFETY: Nodes in the detached chain remain valid until processed.
        let next = unsafe { (*node).next };
        // SAFETY: The reversed node embeds a live closure.
        let cl = unsafe { node_closure(node) };
        // SAFETY: The closure is leaving the wait list.
        unsafe { bindings::rust_closure_set_waiting(cl, 0) };
        // SAFETY: The wait-list reference is being released.
        unsafe { closure_sub(cl, WAITING + 1) };
        node = next;
    }
}

/// Adds a closure to a wait list unless it is already waiting.
///
/// # Safety
///
/// `wait_list` and `cl` must remain valid. The caller must satisfy the public
/// `closure_wait()` ownership rules.
#[export]
unsafe extern "C" fn closure_wait(wait_list: *mut WaitList, cl: *mut Closure) -> bool {
    // SAFETY: The caller guarantees a valid closure.
    if unsafe { remaining(cl) } & WAITING != 0 {
        return false;
    }

    // SAFETY: The caller guarantees a valid closure and transition ownership.
    unsafe { (*cl).closure_get_happened = true };
    // `_RET_IP_` records the caller that added this closure to the wait list.
    #[cfg(CONFIG_DEBUG_CLOSURES)]
    let ip = core::intrinsics::return_address() as c_ulong;
    // SAFETY: The diagnostic address is supplemental to the wait transition.
    #[cfg(CONFIG_DEBUG_CLOSURES)]
    unsafe {
        bindings::rust_closure_set_waiting(cl, ip)
    };
    // SAFETY: The caller guarantees a valid closure.
    unsafe { add_remaining(cl, WAITING + 1) };
    // SAFETY: The closure owns one wait-list reference after publication.
    unsafe { bindings::rust_closure_llist_add(cl, wait_list) };
    true
}

/// Waits synchronously for outstanding closure references.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must permit sleeping.
#[export]
unsafe extern "C" fn __closure_sync(cl: *mut Closure) {
    let mut syncer = MaybeUninit::<Syncer>::uninit();
    // SAFETY: The local syncer is valid for this wait.
    let syncer = unsafe {
        init_syncer(syncer.as_mut_ptr());
        syncer.assume_init_mut()
    };

    // SAFETY: The closure remains live while the caller owns a reference.
    unsafe {
        (*cl).__bindgen_anon_1.__bindgen_anon_1.s = syncer;
        set_callback(cl, Some(sync_callback), ptr::null_mut());
    }
    // SAFETY: The caller transfers its running reference to the callback.
    unsafe { closure_sub(cl, RUNNING + 1) };
    // SAFETY: The syncer remains live for the callback.
    unsafe { wait_for_syncer(syncer) };
}

/// Waits synchronously for the final closure reference without rearming it.
///
/// # Safety
///
/// `cl` must point to a valid closure. The caller must own its running
/// reference and permit sleeping.
#[export]
unsafe extern "C" fn closure_return_sync(cl: *mut Closure) {
    let mut syncer = MaybeUninit::<Syncer>::uninit();
    // SAFETY: The local syncer is valid for this wait.
    let syncer = unsafe {
        init_syncer(syncer.as_mut_ptr());
        syncer.assume_init_mut()
    };

    // SAFETY: The closure remains live while the caller owns a reference.
    unsafe {
        (*cl).__bindgen_anon_1.__bindgen_anon_1.s = syncer;
        set_callback(cl, Some(sync_callback), ptr::null_mut());
    }
    // SAFETY: The caller owns the running reference.
    let flags = unsafe { sub_remaining_release(cl, 1 + RUNNING - DESTRUCTOR) };
    // SAFETY: The flags came from the closure state machine.
    unsafe { validate_flags(flags) };

    if flags & REMAINING_MASK != 0 {
        // SAFETY: The syncer remains live for the callback.
        unsafe { wait_for_syncer(syncer) };
    }

    // SAFETY: The closure remains live until parent release.
    let parent = unsafe { (*cl).parent };
    if !parent.is_null() {
        // SAFETY: The closure owns one reference on its parent.
        unsafe { closure_put(parent) };
    }
}

/// Waits for closure retirement with a timeout.
///
/// # Safety
///
/// `cl` must point to a valid closure and the caller must permit sleeping.
#[export]
unsafe extern "C" fn __closure_sync_timeout(cl: *mut Closure, timeout: c_ulong) -> c_int {
    let mut syncer = MaybeUninit::<Syncer>::uninit();
    // SAFETY: The local syncer is valid for this wait.
    let syncer = unsafe {
        init_syncer(syncer.as_mut_ptr());
        syncer.assume_init_mut()
    };
    let mut timeout = timeout as c_long;
    let mut ret = 0;

    // SAFETY: The closure remains live while the caller owns a reference.
    unsafe {
        (*cl).__bindgen_anon_1.__bindgen_anon_1.s = syncer;
        set_callback(cl, Some(sync_callback), ptr::null_mut());
    }
    // SAFETY: The caller transfers its running reference to the callback.
    unsafe { closure_sub(cl, RUNNING + 1) };

    loop {
        // SAFETY: The caller provides a sleepable task context.
        unsafe { bindings::rust_closure_set_current_state(bindings::TASK_UNINTERRUPTIBLE) };
        // SAFETY: The syncer remains live for the wait.
        if unsafe { ptr::read_volatile(ptr::addr_of!(syncer.done)) } != 0 {
            break;
        }

        if timeout == 0 {
            // SAFETY: The closure remains live until the callback completes.
            let mut value = unsafe { remaining(cl) };
            loop {
                let old = value;
                if old == 0 || old & RUNNING != 0 {
                    // SAFETY: The wait completed or the closure was rearmed.
                    unsafe { bindings::rust_closure_set_current_running() };
                    return ret;
                }

                let new = old + REMAINING_INITIALIZER;
                // SAFETY: The caller owns the timeout rollback.
                value = unsafe { cmpxchg_remaining(cl, old, new) };
                if value == old {
                    break;
                }
            }
            ret = -(bindings::ETIME as c_int);
        }

        // SAFETY: The caller permits sleeping and provided the remaining time.
        timeout = unsafe { bindings::rust_closure_schedule_timeout(timeout) };
    }

    // SAFETY: The wait completed and the current task is runnable again.
    unsafe { bindings::rust_closure_set_current_running() };
    ret
}

/// Registers an active closure with the locking diagnostics.
///
/// # Safety
///
/// `cl` must point to valid closure storage that can enter the debug list.
#[cfg(CONFIG_DEBUG_CLOSURES)]
#[export]
unsafe extern "C" fn closure_debug_create(cl: *mut Closure) {
    // SAFETY: The caller provides the public debug registration contract.
    unsafe { bindings::rust_closure_debug_register(cl) };
}

/// Removes a closure from the locking diagnostics.
///
/// # Safety
///
/// `cl` must be a live closure or a stack closure accepted by the debug
/// contract.
#[cfg(CONFIG_DEBUG_CLOSURES)]
#[export]
unsafe extern "C" fn closure_debug_destroy(cl: *mut Closure) {
    // SAFETY: The caller provides the public debug unregister contract.
    unsafe { bindings::rust_closure_debug_unregister(cl) };
}
