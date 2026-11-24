// SPDX-License-Identifier: GPL-2.0-only

//! Native spurious IRQ detection state machine.
//!
//! Rust owns polling exclusions, bad-result classification, deferred threaded
//! accounting, unhandled windows, misroute policy, stuck-IRQ thresholds, and
//! final state transitions.
//!
//! C helpers retain descriptor fields, jiffies arithmetic, diagnostics,
//! descriptor scans, descriptor disabling, and timer rearming.

use crate::{bindings, ffi::c_uint};

type Desc = bindings::irq_desc;

const IRQ_NONE: c_uint = bindings::irqreturn::IRQ_NONE.0;
const IRQ_HANDLED: c_uint = bindings::irqreturn::IRQ_HANDLED.0;
const IRQ_WAKE_THREAD: c_uint = bindings::irqreturn::IRQ_WAKE_THREAD.0;
const VALID_RESULTS: c_uint = IRQ_HANDLED | IRQ_WAKE_THREAD;
const SPURIOUS_DEFERRED: c_uint = 0x8000_0000;
const UNHANDLED_LIMIT: c_uint = 99_900;
const CHECK_INTERVAL: c_uint = 100_000;

/// Returns true when the current event must try misrouted IRQ recovery.
///
/// # Safety
///
/// `desc` must remain valid for the call.
unsafe fn try_misrouted(desc: *mut Desc, irq: c_uint, action_ret: c_uint) -> bool {
    // SAFETY: The helper only reads the module parameter.
    let fixup = unsafe { bindings::rust_irq_spurious_fixup_level() };
    if fixup == 0 {
        return false;
    }

    if action_ret == IRQ_NONE {
        return true;
    }

    if fixup < 2 {
        return false;
    }

    if irq == 0 {
        return true;
    }

    // The action can change concurrently. The helper uses READ_ONCE.
    // SAFETY: The caller guarantees a live descriptor.
    unsafe { bindings::rust_irq_spurious_action_is_irqpoll(desc) }
}

/// Handles one IRQ event for spurious and stuck-line detection.
///
/// The caller runs this without `desc->lock`, but with `IRQD_IRQ_INPROGRESS`
/// set. Hard interrupts are not reentrant, so threaded-Handled state needs no
/// additional lock.
///
/// # Safety
///
/// `desc` must remain live for the call. The caller must use the normal
/// event-completion context.
unsafe fn note(desc: *mut Desc, mut action_ret: c_uint) {
    // SAFETY: These helpers read independent polling state from a live
    // descriptor.
    // SAFETY: The helper reads polling state from a live descriptor.
    let poll_in_progress = unsafe { bindings::rust_irq_spurious_poll_in_progress(desc) };
    // SAFETY: The helper reads polling state from a live descriptor.
    let always_polled = unsafe { bindings::rust_irq_spurious_is_polled(desc) };
    if poll_in_progress || always_polled {
        return;
    }

    if action_ret > VALID_RESULTS {
        // SAFETY: The descriptor and result provide diagnostic metadata.
        unsafe { bindings::rust_irq_spurious_report_bad(desc, action_ret) };
        return;
    }

    if action_ret & IRQ_WAKE_THREAD != 0 {
        if action_ret == IRQ_WAKE_THREAD {
            // SAFETY: The caller guarantees a live descriptor.
            let mut last = unsafe { bindings::rust_irq_spurious_threads_last(desc) };

            // The first woken thread is accounted by the next hard interrupt.
            if last & SPURIOUS_DEFERRED == 0 {
                // SAFETY: The caller guarantees a live descriptor.
                unsafe {
                    bindings::rust_irq_spurious_set_threads_last(desc, last | SPURIOUS_DEFERRED)
                };
                return;
            }

            // SAFETY: The caller guarantees a live descriptor.
            let handled =
                unsafe { bindings::rust_irq_spurious_threads_handled(desc) } | SPURIOUS_DEFERRED;
            if handled != last {
                action_ret = IRQ_HANDLED;
                last = handled;
                // SAFETY: The caller guarantees a live descriptor.
                unsafe { bindings::rust_irq_spurious_set_threads_last(desc, last) };
            } else {
                action_ret = IRQ_NONE;
            }
        } else {
            // A primary handler handled the event. Keep the deferred marker
            // for the current invocation, as the C state machine does.
            // SAFETY: The caller guarantees a live descriptor.
            let last = unsafe { bindings::rust_irq_spurious_threads_last(desc) };
            // SAFETY: The caller guarantees a live descriptor.
            unsafe {
                bindings::rust_irq_spurious_set_threads_last(desc, last & !SPURIOUS_DEFERRED)
            };
        }
    }

    if action_ret == IRQ_NONE {
        // SAFETY: The caller guarantees a live descriptor.
        let mut unhandled = unsafe { bindings::rust_irq_spurious_unhandled(desc) };
        // SAFETY: The helper evaluates the kernel's wrap-safe time comparison.
        let expired = unsafe { bindings::rust_irq_spurious_window_expired(desc) };
        unhandled = if expired {
            1
        } else {
            unhandled.wrapping_add(1)
        };
        // SAFETY: The caller guarantees a live descriptor.
        unsafe { bindings::rust_irq_spurious_set_unhandled(desc, unhandled) };
        // SAFETY: The caller guarantees a live descriptor.
        unsafe { bindings::rust_irq_spurious_mark_unhandled(desc) };
    }

    // SAFETY: The helper returns the Linux IRQ number.
    let irq = unsafe { bindings::rust_irq_spurious_irq(desc) };
    // SAFETY: The descriptor remains live and the caller permits recovery.
    if unsafe { try_misrouted(desc, irq, action_ret) } {
        // SAFETY: Misroute recovery scans descriptors through the normal path.
        let found = unsafe { bindings::rust_irq_spurious_misrouted(desc) };
        if action_ret == IRQ_NONE && found {
            // SAFETY: The caller guarantees a live descriptor.
            let unhandled = unsafe { bindings::rust_irq_spurious_unhandled(desc) };
            // SAFETY: The caller guarantees a live descriptor.
            unsafe { bindings::rust_irq_spurious_set_unhandled(desc, unhandled.wrapping_sub(1)) };
        }
    }

    // SAFETY: The caller guarantees a live descriptor.
    let irqs_unhandled = unsafe { bindings::rust_irq_spurious_unhandled(desc) };
    if irqs_unhandled == 0 {
        return;
    }

    // SAFETY: The caller guarantees a live descriptor.
    let irq_count = unsafe { bindings::rust_irq_spurious_irq_count(desc) }.wrapping_add(1);
    if irq_count < CHECK_INTERVAL {
        // SAFETY: The caller guarantees a live descriptor.
        unsafe { bindings::rust_irq_spurious_set_irq_count(desc, irq_count) };
        return;
    }

    // SAFETY: The caller guarantees a live descriptor.
    unsafe { bindings::rust_irq_spurious_set_irq_count(desc, 0) };
    if irqs_unhandled > UNHANDLED_LIMIT {
        // SAFETY: The descriptor and result provide diagnostic metadata.
        unsafe { bindings::rust_irq_spurious_report_stuck(desc, action_ret) };
        // SAFETY: The caller permits final stuck-IRQ state changes.
        unsafe { bindings::rust_irq_spurious_disable(desc) };
        // SAFETY: This moves detection into the normal recovery timer.
        unsafe { bindings::rust_irq_spurious_schedule_poll() };
    }
    // SAFETY: The caller guarantees a live descriptor.
    unsafe { bindings::rust_irq_spurious_set_unhandled(desc, 0) };
}

/// Handles one IRQ event for spurious and stuck-line detection.
///
/// # Safety
///
/// `desc` must remain live. The caller must use the normal event-completion
/// context.
#[no_mangle]
unsafe extern "C" fn note_interrupt(desc: *mut Desc, action_ret: c_uint) {
    // SAFETY: The caller provides the note contract.
    unsafe { note(desc, action_ret) };
}
