// SPDX-License-Identifier: GPL-2.0-only

//! Native IRQ resend state machine.
//!
//! Rust owns level, replay, and pending decisions, hardware retrigger fallback,
//! nested parent selection, software queue insertion policy, tasklet drain
//! ordering, and resend state transitions.
//!
//! C helpers retain descriptor field access, the raw resend lock, hlist
//! operations, tasklet scheduling, irqchip callbacks, and flow-handler calls.

use crate::{
    bindings,
    ffi::{c_int, c_uint},
};
use macros::export;

type Desc = bindings::irq_desc;

/// Schedules a descriptor after validating the software-resend context.
///
/// # Safety
///
/// `desc` must point to a live IRQ descriptor. The descriptor lock must be
/// held by the caller.
unsafe fn software_resend(mut desc: *mut Desc) -> c_int {
    // SAFETY: The caller guarantees a live descriptor.
    if unsafe { bindings::rust_irq_resend_enforces_irqctx(desc) } {
        return -(bindings::EINVAL as c_int);
    }

    // Nested-thread interrupts must resend through a valid parent.
    // SAFETY: The caller guarantees a live descriptor.
    if unsafe { bindings::rust_irq_resend_is_nested(desc) } {
        // SAFETY: The caller guarantees a live descriptor.
        let parent = unsafe { bindings::rust_irq_resend_parent_irq(desc) };
        if parent <= 0 {
            return -(bindings::EINVAL as c_int);
        }

        // SAFETY: The helper performs the normal IRQ-number lookup.
        desc = unsafe { bindings::rust_irq_resend_desc(parent as c_uint) };
        if desc.is_null() {
            return -(bindings::EINVAL as c_int);
        }
    }

    // The lock makes the hashed check and insertion one atomic policy step.
    // SAFETY: The caller guarantees a live descriptor.
    unsafe { bindings::rust_irq_resend_lock() };
    // SAFETY: The caller guarantees a live descriptor.
    if !unsafe { bindings::rust_irq_resend_node_hashed(desc) } {
        // SAFETY: The resend lock is held and `desc` is live.
        unsafe { bindings::rust_irq_resend_node_add(desc) };
    }
    // SAFETY: This function acquired the resend lock.
    unsafe { bindings::rust_irq_resend_unlock() };

    // SAFETY: The descriptor is queued or was already queued.
    unsafe { bindings::rust_irq_resend_schedule() };
    0
}

/// Attempts a hardware retrigger and reports whether the software path is
/// required.
///
/// The irqchip ABI uses one for a successful hardware retrigger and zero when
/// the software fallback must run.
///
/// # Safety
///
/// `desc` must point to a live IRQ descriptor with a valid chip hierarchy.
unsafe fn retrigger(desc: *mut Desc) -> c_int {
    // SAFETY: The caller guarantees a live descriptor.
    if unsafe { bindings::rust_irq_resend_has_direct_retrigger(desc) } {
        // SAFETY: A direct retrigger callback is present.
        return unsafe { bindings::rust_irq_resend_retrigger(desc) };
    }

    if cfg!(CONFIG_IRQ_DOMAIN_HIERARCHY) {
        // SAFETY: The caller guarantees a valid IRQ chip hierarchy.
        return unsafe { bindings::rust_irq_resend_retrigger_hierarchy(desc) };
    }

    0
}

/// Checks and starts a pending IRQ resend.
///
/// # Safety
///
/// `desc` must point to a live IRQ descriptor. The caller must disable
/// interrupts and hold `desc->lock`.
#[export]
unsafe extern "C" fn rust_irq_resend_check(desc: *mut Desc, inject: bool) -> c_int {
    // Level interrupts are resent by hardware while the line remains active.
    // SAFETY: The caller guarantees a live descriptor.
    if unsafe { bindings::rust_irq_resend_is_level(desc) } {
        // SAFETY: The caller holds the descriptor lock.
        unsafe { bindings::rust_irq_resend_clear_pending(desc) };
        return -(bindings::EINVAL as c_int);
    }

    // Only one replay may be outstanding until the flow handler clears it.
    // SAFETY: The caller guarantees a live descriptor.
    if unsafe { bindings::rust_irq_resend_is_replay(desc) } {
        return -(bindings::EBUSY as c_int);
    }

    // SAFETY: The caller guarantees a live descriptor.
    if !unsafe { bindings::rust_irq_resend_is_pending(desc) } && !inject {
        return 0;
    }

    // SAFETY: The caller holds the descriptor lock.
    unsafe { bindings::rust_irq_resend_clear_pending(desc) };

    let mut err = 0;
    // SAFETY: The caller guarantees a live descriptor and chip hierarchy.
    if unsafe { retrigger(desc) } == 0 {
        // SAFETY: The caller holds the descriptor lock.
        err = unsafe { software_resend(desc) };
    }

    if err == 0 {
        // SAFETY: The caller holds the descriptor lock.
        unsafe { bindings::rust_irq_resend_set_replay(desc) };
    }
    err
}

/// Removes a descriptor from the software resend queue.
///
/// # Safety
///
/// `desc` must point to a live IRQ descriptor.
#[export]
unsafe extern "C" fn rust_irq_resend_clear(desc: *mut Desc) {
    // SAFETY: The caller guarantees a live descriptor.
    unsafe { bindings::rust_irq_resend_lock() };
    // SAFETY: The caller guarantees a live descriptor and the lock is held.
    unsafe { bindings::rust_irq_resend_node_del(desc) };
    // SAFETY: This function acquired the resend lock.
    unsafe { bindings::rust_irq_resend_unlock() };
}

/// Initializes the software queue node for a descriptor.
///
/// # Safety
///
/// `desc` must point to valid, writable IRQ descriptor storage.
#[export]
unsafe extern "C" fn rust_irq_resend_init(desc: *mut Desc) {
    // SAFETY: The caller guarantees writable descriptor storage.
    unsafe { bindings::rust_irq_resend_init_node(desc) };
}

/// Drains software resends in descriptor queue-head order.
///
/// The queue inserts new descriptors at the head, so later software requests
/// are dispatched first. The lock is dropped while a flow handler runs.
///
/// # Safety
///
/// This function must run from the resend tasklet. Every queued descriptor
/// must remain live until it is removed.
#[export]
unsafe extern "C" fn rust_irq_resend_tasklet(_unused: *mut bindings::tasklet_struct) {
    // SAFETY: This is the only consumer of the software resend queue.
    unsafe { bindings::rust_irq_resend_lock_irq() };
    loop {
        // SAFETY: The tasklet owns queue consumption and holds the lock.
        let desc = unsafe { bindings::rust_irq_resend_pop() };
        if desc.is_null() {
            break;
        }

        // SAFETY: IRQs remain disabled until the tasklet returns.
        unsafe { bindings::rust_irq_resend_unlock() };
        // SAFETY: The queue owned a reference to a live descriptor.
        unsafe { bindings::rust_irq_resend_handle(desc) };
        // SAFETY: The flow handler has returned with IRQs still disabled.
        unsafe { bindings::rust_irq_resend_lock() };
    }
    // SAFETY: This function acquired the resend lock with IRQs disabled.
    unsafe { bindings::rust_irq_resend_unlock_irq() };
}
