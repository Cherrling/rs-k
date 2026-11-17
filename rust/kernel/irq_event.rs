// SPDX-License-Identifier: GPL-2.0-only

//! Native IRQ event state machine.
//!
//! Rust owns action traversal, handler result aggregation, threaded wake
//! policy, run-state claiming, oneshot and active-count transitions, and the
//! pending/in-progress lifecycle around per-CPU event dispatch.
//!
//! C helpers retain action fields and links, handler invocation with tracing
//! and duration diagnostics, descriptor lock transitions, atomic thread flags,
//! and scheduler wake primitives.

use crate::{bindings, ffi::c_uint};
use macros::export;

type Action = bindings::irqaction;
type Desc = bindings::irq_desc;
type EventResult = c_uint;

const IRQ_NONE: c_uint = bindings::irqreturn::IRQ_NONE.0;
const IRQ_WAKE_THREAD: c_uint = bindings::irqreturn::IRQ_WAKE_THREAD.0;

/// Wakes the thread for one action that returned `IRQ_WAKE_THREAD`.
///
/// The caller serializes this hard-interrupt path. The IRQ thread serializes
/// its matching oneshot and run-state transitions with `desc->lock`. The
/// `IRQS_INPROGRESS` protocol keeps those two paths ordered.
///
/// # Safety
///
/// `desc` and `action` must remain live. `action->thread` must identify a
/// valid handler thread. The caller must provide the hard-interrupt
/// serialization required by the IRQ thread state machine.
unsafe fn wake_thread(desc: *mut Desc, action: *mut Action) {
    // A crashed handler thread cannot consume another notification. The hard
    // interrupt path has already disabled the device interrupt.
    // SAFETY: The caller guarantees valid action and thread storage.
    if unsafe { bindings::rust_irq_event_thread_exiting(action) } {
        return;
    }

    // RUNTHREAD suppresses duplicate wakeups while the thread is pending.
    // SAFETY: The caller guarantees valid action storage.
    if !unsafe { bindings::rust_irq_event_claim_thread(action) } {
        return;
    }

    // The hard-interrupt path owns this lockless mask update. Thread-side
    // clearing is serialized by desc->lock and the IRQS_INPROGRESS protocol.
    // SAFETY: The caller guarantees valid descriptor and action storage.
    unsafe { bindings::rust_irq_event_add_oneshot(desc, action) };
    // SAFETY: This path woke one action thread.
    unsafe { bindings::rust_irq_event_inc_thread_active(desc) };
    // SAFETY: The action thread is valid and RUNTHREAD is now set.
    unsafe { bindings::rust_irq_event_wake_task(action) };
}

/// Dispatches all actions for one descriptor on the current CPU.
///
/// # Safety
///
/// `desc` must remain live and its action list must remain valid for the call.
/// The caller must disable interrupts and set `IRQD_IRQ_INPROGRESS` before it
/// releases `desc->lock`, or otherwise provide the equivalent tracked context.
unsafe fn dispatch(desc: *mut Desc) -> EventResult {
    let mut retval = IRQ_NONE;
    // SAFETY: The caller guarantees a live descriptor and action list.
    let mut action = unsafe { bindings::rust_irq_event_first_action(desc) };

    while !action.is_null() {
        // SAFETY: `action` belongs to the descriptor's live action list.
        if unsafe { bindings::rust_irq_event_action_can_thread(desc, action) } {
            // SAFETY: This is the hard-interrupt handler path.
            unsafe { bindings::rust_irq_event_mark_force_threaded() };
        }

        // Tracepoints and the optional duration timer surround the callback.
        // SAFETY: `desc` and `action` are live and the handler is valid.
        let res = unsafe { bindings::rust_irq_event_call_handler(desc, action) };

        // Hardware handlers must return with interrupts disabled.
        // SAFETY: This helper only reads the local interrupt state.
        if !unsafe { bindings::rust_irq_event_irqs_disabled() } {
            // SAFETY: The descriptor and action provide diagnostic metadata.
            unsafe { bindings::rust_irq_event_warn_irqs_enabled(desc, action) };
            // SAFETY: This restores the hard-interrupt handler contract.
            unsafe { bindings::rust_irq_event_local_irq_disable() };
        }

        if res == IRQ_WAKE_THREAD {
            // SAFETY: `action` belongs to the live descriptor action list.
            if unsafe { bindings::rust_irq_event_action_has_thread(action) } {
                // SAFETY: The action has a valid thread callback and task.
                unsafe { wake_thread(desc, action) };
            } else {
                // SAFETY: The descriptor and action provide diagnostic metadata.
                unsafe { bindings::rust_irq_event_warn_no_thread(desc, action) };
            }
        }

        retval |= res;
        // SAFETY: `action` belongs to the descriptor's live action list.
        action = unsafe { bindings::rust_irq_event_next_action(action) };
    }

    retval
}

/// Dispatches per-CPU events and records randomness and spurious state.
///
/// # Safety
///
/// `desc` must satisfy the [`dispatch`] contract and remain live while its
/// randomness and diagnostic state is updated.
unsafe fn percpu(desc: *mut Desc) -> EventResult {
    // SAFETY: The caller provides the dispatch context.
    let ret = unsafe { dispatch(desc) };
    // SAFETY: The descriptor belongs to this interrupt and remains live.
    unsafe { bindings::rust_irq_event_add_randomness(desc) };
    // SAFETY: The descriptor remains live.
    if unsafe { bindings::rust_irq_event_has_debug(desc) } {
        // SAFETY: The descriptor remains live and no descriptor lock is held.
        unsafe { bindings::rust_irq_event_note(desc, ret) };
    }
    ret
}

/// Dispatches all actions for one descriptor on the current CPU.
///
/// # Safety
///
/// `desc` must remain live. The caller must provide the [`dispatch`] context.
#[export]
unsafe extern "C" fn rust_irq_event_dispatch(desc: *mut Desc) -> EventResult {
    // SAFETY: The caller provides the dispatch contract.
    unsafe { dispatch(desc) }
}

/// Dispatches an event and updates per-CPU diagnostic state.
///
/// # Safety
///
/// `desc` must remain live. The caller must provide the [`dispatch`] context.
#[export]
unsafe extern "C" fn rust_irq_event_percpu(desc: *mut Desc) -> EventResult {
    // SAFETY: The caller provides the per-CPU event contract.
    unsafe { percpu(desc) }
}

/// Handles one IRQ event while preserving the descriptor lock contract.
///
/// Rust clears pending state, marks the descriptor in progress, drops and
/// retakes `desc->lock`, dispatches the event, and clears in-progress state.
///
/// # Safety
///
/// `desc` must remain live. The caller must hold `desc->lock` and have
/// interrupts disabled.
#[export]
unsafe extern "C" fn rust_irq_event_handle(desc: *mut Desc) -> EventResult {
    // SAFETY: The caller guarantees descriptor lock and interrupt state.
    unsafe { bindings::rust_irq_event_begin(desc) };
    // SAFETY: The caller guarantees the descriptor lock is held.
    unsafe { bindings::rust_irq_event_unlock(desc) };
    // SAFETY: Interrupts remain disabled while the descriptor lock is dropped.
    let ret = unsafe { percpu(desc) };
    // SAFETY: The event dispatch path returns with interrupts disabled.
    unsafe { bindings::rust_irq_event_lock(desc) };
    // SAFETY: The descriptor lock is held again.
    unsafe { bindings::rust_irq_event_finish(desc) };
    ret
}

/// Wakes one IRQ action thread from hard-interrupt context.
///
/// # Safety
///
/// `desc` and `action` must remain live. `action->thread` must identify a
/// valid handler thread. The caller must provide the hard-interrupt
/// serialization required by the IRQ thread state machine.
#[export]
unsafe extern "C" fn rust_irq_event_wake_thread(desc: *mut Desc, action: *mut Action) {
    // SAFETY: The caller provides the wake contract.
    unsafe { wake_thread(desc, action) };
}
