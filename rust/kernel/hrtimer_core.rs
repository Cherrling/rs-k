// SPDX-License-Identifier: GPL-2.0

//! Native hrtimer queue state transitions.
//!
//! Rust owns local-base enqueue, in-place expiry updates, requeue decisions,
//! queued removal, cancellation results, the clock-base next-expiry cache, and
//! expiry forwarding. Locking, clock event reprogramming, ktime arithmetic,
//! debug objects, and tracepoint delivery remain outside this module.

use crate::bindings;
use core::ptr;
use macros::export;

type Hrtimer = bindings::hrtimer;
type ClockBase = bindings::hrtimer_clock_base;
type TimerLinkedNode = bindings::timerqueue_linked_node;
type RbNodeLinked = bindings::rb_node_linked;

/// Returns the timer queue entry that owns a linked-tree node.
///
/// # Safety
///
/// `node` must point to the linked-tree node of a live timer queue entry.
#[inline]
unsafe fn timer_linked_node(node: *mut RbNodeLinked) -> *mut TimerLinkedNode {
    // SAFETY: The caller guarantees that the containing entry is live.
    unsafe { crate::container_of!(node, TimerLinkedNode, node) }
}

/// Updates the cached first expiry of a clock base.
///
/// # Safety
///
/// `base` must be a live clock base protected by its CPU base lock.
#[inline]
unsafe fn update_cached_expiry(base: *mut ClockBase) {
    // SAFETY: The caller guarantees that the clock base is live.
    let root = unsafe { ptr::addr_of_mut!((*base).active.rb_root) };
    // SAFETY: The caller guarantees that the clock base is live.
    let first = unsafe { (*root).rb_leftmost };

    // SAFETY: The caller guarantees that the clock base is live.
    unsafe {
        (*base).expires_next = if first.is_null() {
            bindings::ktime_t::MAX
        } else {
            (*timer_linked_node(first)).expires
        }
    };
}

/// Returns whether the new expiry fits between the queued timer's neighbors.
///
/// # Safety
///
/// `timer` must be queued and protected by its CPU base lock.
#[inline]
unsafe fn can_update_in_place(timer: *mut Hrtimer, expires: bindings::ktime_t) -> bool {
    // SAFETY: The caller guarantees that the timer is queued.
    let node = unsafe { ptr::addr_of_mut!((*timer).node.node) };
    // SAFETY: The caller guarantees that the timer is queued.
    let next = unsafe { (*node).next };

    if !next.is_null() {
        // SAFETY: A queued neighbor points to a live linked timer entry.
        if expires > unsafe { (*timer_linked_node(next)).expires } {
            return false;
        }
    }

    // SAFETY: The caller guarantees that the timer is queued.
    let prev = unsafe { (*node).prev };
    if prev.is_null() {
        return true;
    }

    // SAFETY: A queued neighbor points to a live linked timer entry.
    expires >= unsafe { (*timer_linked_node(prev)).expires }
}

/// Starts or requeues a timer on its current local clock base.
///
/// This implements the production state machine behind
/// `remove_and_enqueue_same_base()`.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. The timer must already belong to the supplied base.
#[export]
unsafe extern "C" fn rust_hrtimer_requeue_same_base(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    mode: u32,
    expires: bindings::ktime_t,
    delta_ns: u64,
) -> bool {
    // SAFETY: The caller guarantees that the timer and base are live.
    unsafe { bindings::rust_helper_hrtimer_set_expires_range_ns(timer, expires, delta_ns) };
    // SAFETY: The helper initialized the timer expiry.
    let expires = unsafe { (*timer).node.expires };
    // SAFETY: The caller guarantees that the timer is live.
    let was_queued = unsafe { bindings::rust_helper_hrtimer_is_queued(timer) };
    let mut was_first = false;

    if was_queued {
        // SAFETY: A queued timer has a live linked-tree node.
        was_first = unsafe { (*timer).node.node.prev }.is_null();

        // SAFETY: The caller guarantees that the timer is queued.
        if unsafe { can_update_in_place(timer, expires) } {
            // SAFETY: The caller guarantees that the timer is live.
            unsafe { bindings::rust_helper_hrtimer_trace_start(timer, mode, true) };
            if was_first {
                // SAFETY: The caller holds the clock base lock.
                unsafe { (*base).expires_next = expires };
            }
            return was_first;
        }

        // SAFETY: The caller guarantees that the timer is live.
        unsafe { bindings::rust_helper_hrtimer_debug_deactivate(timer) };
        // SAFETY: The timer is queued in this clock base.
        unsafe {
            bindings::rb_erase_linked(
                ptr::addr_of_mut!((*timer).node.node),
                ptr::addr_of_mut!((*base).active.rb_root),
            )
        };
    }

    // SAFETY: The caller guarantees that the timer is live.
    unsafe { bindings::rust_helper_hrtimer_debug_activate(timer, mode, was_queued) };
    // SAFETY: The caller holds the CPU base lock.
    let cpu_base = unsafe { (*base).cpu_base };
    // SAFETY: The caller holds the CPU base lock.
    let active_bit = 1_u32 << unsafe { (*base).index };
    // SAFETY: The caller holds the CPU base lock.
    unsafe { (*cpu_base).active_bases |= active_bit };
    // SAFETY: The caller holds the CPU base lock for this state transition.
    unsafe { bindings::rust_helper_hrtimer_set_queued(timer, true) };

    // SAFETY: The timer belongs to this clock base and is ready for insertion.
    let first = unsafe {
        bindings::timerqueue_linked_add(
            ptr::addr_of_mut!((*base).active),
            ptr::addr_of_mut!((*timer).node),
        )
    };

    if first {
        // SAFETY: The caller holds the clock base lock.
        unsafe { (*base).expires_next = expires };
    } else if was_first {
        // SAFETY: The removed timer was the cached first entry.
        unsafe { update_cached_expiry(base) };
    }
    first
}

/// Removes a queued timer and maintains the clock-base and CPU-base state.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. `timer` must belong to `base`.
#[cfg(CONFIG_RUST_HRTIMER_CANCEL)]
unsafe fn remove_locked(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    newstate: bool,
    reprogram: bool,
) {
    // SAFETY: The caller guarantees that the timer is live.
    if !unsafe { bindings::rust_helper_hrtimer_is_queued(timer) } {
        return;
    }

    // SAFETY: The caller guarantees that the timer is live.
    unsafe { bindings::rust_helper_hrtimer_set_queued(timer, newstate) };
    // SAFETY: A queued timer has a live linked-tree node.
    let was_first = unsafe { (*timer).node.node.prev }.is_null();
    // SAFETY: The timer is queued in the supplied clock base.
    let active_base_empty = !unsafe {
        bindings::rb_erase_linked(
            ptr::addr_of_mut!((*timer).node.node),
            ptr::addr_of_mut!((*base).active.rb_root),
        )
    };
    // SAFETY: The caller holds the CPU base lock.
    let cpu_base = unsafe { (*base).cpu_base };

    if active_base_empty {
        // SAFETY: The caller holds the CPU base lock.
        let active_bit = 1_u32 << unsafe { (*base).index };
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).active_bases &= !active_bit };
    }

    if !was_first {
        return;
    }

    // SAFETY: The caller holds the CPU base lock.
    unsafe { update_cached_expiry(base) };

    // SAFETY: The caller holds the CPU base lock.
    if !reprogram || timer != unsafe { (*cpu_base).next_timer } {
        return;
    }

    // SAFETY: The caller holds the CPU base lock.
    if unsafe { (*timer).is_lazy } {
        return;
    }

    // SAFETY: The caller holds the CPU base lock.
    if unsafe { (*cpu_base).deferred_rearm } {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).deferred_needs_update = true };
    } else {
        // SAFETY: The helper updates the clock event under the CPU base lock.
        unsafe { bindings::rust_helper_hrtimer_force_reprogram(cpu_base, true) };
    }
}

/// Removes a queued timer from a clock base.
///
/// This implements the production state machine behind `__remove_hrtimer()`.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. `timer` must belong to `base`.
#[cfg(CONFIG_RUST_HRTIMER_CANCEL)]
#[export]
unsafe extern "C" fn rust_hrtimer_remove(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    newstate: bool,
    reprogram: bool,
) {
    // SAFETY: The caller satisfies the requirements of `remove_locked`.
    unsafe { remove_locked(timer, base, newstate, reprogram) };
}

/// Attempts to cancel a timer while its clock base is locked.
///
/// This implements the state decisions in `hrtimer_try_to_cancel()`.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. `base` must be the timer's current base.
#[cfg(CONFIG_RUST_HRTIMER_CANCEL)]
#[export]
unsafe extern "C" fn rust_hrtimer_try_to_cancel_locked(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
) -> i32 {
    // SAFETY: The caller guarantees that the timer is live.
    if unsafe { bindings::rust_helper_hrtimer_callback_running(timer) } {
        return -1;
    }

    // SAFETY: The caller guarantees that the timer is live.
    if !unsafe { bindings::rust_helper_hrtimer_is_queued(timer) } {
        return 0;
    }

    // SAFETY: The caller guarantees that the timer is live.
    unsafe { bindings::rust_helper_hrtimer_debug_deactivate(timer) };
    // SAFETY: The caller holds the timer's clock base lock.
    let reprogram = unsafe { bindings::rust_helper_hrtimer_base_is_local(base) };
    // SAFETY: The caller satisfies the requirements of `remove_locked`.
    unsafe { remove_locked(timer, base, false, reprogram) };
    1
}

/// Advances an inactive timer past a reference time.
///
/// This implements the production overrun and expiry calculation in
/// `hrtimer_forward()`.
///
/// # Safety
///
/// `timer` must be a live timer, and the caller must provide the serialization
/// required by `hrtimer_forward()`.
#[cfg(CONFIG_RUST_HRTIMER_FORWARD)]
#[export]
unsafe extern "C" fn rust_hrtimer_forward(
    timer: *mut Hrtimer,
    now: bindings::ktime_t,
    interval: bindings::ktime_t,
) -> u64 {
    // SAFETY: The caller guarantees that the timer is live.
    let delta = now.wrapping_sub(unsafe { (*timer).node.expires });

    if delta < 0 {
        return 0;
    }

    // SAFETY: The caller guarantees that the timer is live.
    if unsafe { bindings::rust_helper_hrtimer_forward_warn_queued(timer) } {
        return 0;
    }

    // SAFETY: The timekeeping core initializes this read-mostly value.
    let resolution = bindings::ktime_t::from(unsafe { bindings::hrtimer_resolution });
    let interval = interval.max(resolution);
    let mut overruns = 1;

    if delta >= interval {
        let increment = interval;
        // SAFETY: The interval is positive and fits a signed ktime.
        overruns = unsafe { bindings::rust_helper_hrtimer_forward_divns(delta, increment) } as u64;
        // SAFETY: The quotient times the interval does not exceed delta.
        unsafe {
            bindings::rust_helper_hrtimer_forward_add_ns(
                timer,
                overruns.wrapping_mul(increment as u64),
            )
        };

        // SAFETY: The caller guarantees that the timer is live.
        if unsafe { (*timer).node.expires } > now {
            return overruns;
        }

        overruns = overruns.wrapping_add(1);
    }

    // SAFETY: The caller guarantees that the timer is live.
    unsafe { bindings::rust_helper_hrtimer_forward_add(timer, interval) };
    overruns
}
