// SPDX-License-Identifier: GPL-2.0

//! Native hrtimer queue state transitions.
//!
//! Rust owns timer insertion, local-base enqueue, in-place expiry updates,
//! requeue decisions, queued removal, cancellation results, the clock-base
//! next-expiry cache, expiry forwarding, next-event selection, and the
//! cancellation retry loop. It also owns the state transitions immediately
//! before and after an expiry callback.
//! Rust also selects the target CPU base and owns timer base migration.
//! It also owns the local-base retention decision for the SMP NOHZ path.
//! It also selects and iterates expired timer queues.
//! It also owns the high-resolution timer interrupt state machine.
//! It also owns deferred rearm coordination and clock event rearm state.
//! It also owns force-reprogram and clock event reprogramming state.
//! It also owns the first-timer startup state machine.
//! It also validates and removes already expired user timers.
//! Locking, clock event reprogramming, ktime arithmetic, callback completion
//! waits, debug objects, and tracepoint delivery remain outside this module.

use crate::bindings;
use core::ptr;
use macros::export;

type Hrtimer = bindings::hrtimer;
type ClockBase = bindings::hrtimer_clock_base;
#[cfg(any(
    CONFIG_RUST_HRTIMER_BASE_MIGRATION,
    CONFIG_RUST_HRTIMER_BASE_SELECTION,
    CONFIG_RUST_HRTIMER_CALLBACK,
    CONFIG_RUST_HRTIMER_ENQUEUE,
    CONFIG_RUST_HRTIMER_FORCE_REPROGRAM,
    CONFIG_RUST_HRTIMER_INTERRUPT,
    CONFIG_RUST_HRTIMER_NEXT_EVENT,
    CONFIG_RUST_HRTIMER_REARM,
    CONFIG_RUST_HRTIMER_REPROGRAM,
    CONFIG_RUST_HRTIMER_RUN_QUEUES,
    CONFIG_RUST_HRTIMER_START,
    CONFIG_RUST_HRTIMER_TARGET_BASE,
    CONFIG_RUST_HRTIMER_USER_EXPIRY
))]
type CpuBase = bindings::hrtimer_cpu_base;
type TimerLinkedNode = bindings::timerqueue_linked_node;
type RbNodeLinked = bindings::rb_node_linked;

#[cfg(any(CONFIG_RUST_HRTIMER_NEXT_EVENT, CONFIG_RUST_HRTIMER_RUN_QUEUES))]
const HRTIMER_ACTIVE_MASK_SHIFT: u32 = bindings::hrtimer_base_type_HRTIMER_BASE_MONOTONIC_SOFT;
#[cfg(any(CONFIG_RUST_HRTIMER_NEXT_EVENT, CONFIG_RUST_HRTIMER_RUN_QUEUES))]
const HRTIMER_ACTIVE_HARD: u32 = (1_u32 << HRTIMER_ACTIVE_MASK_SHIFT) - 1;
#[cfg(any(CONFIG_RUST_HRTIMER_NEXT_EVENT, CONFIG_RUST_HRTIMER_RUN_QUEUES))]
const HRTIMER_ACTIVE_SOFT: u32 = HRTIMER_ACTIVE_HARD << HRTIMER_ACTIVE_MASK_SHIFT;

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

/// Selects the target CPU base for a timer.
///
/// An offline timer base uses the online housekeeping target. An unpinned
/// timer can use the NOHZ timer target when migration is enabled. All other
/// timers stay on their current base.
#[cfg(CONFIG_RUST_HRTIMER_TARGET_BASE)]
fn select_target_base(
    base: *mut CpuBase,
    pinned: bool,
    offline: *mut CpuBase,
    nohz: *mut CpuBase,
    online: bool,
    migration_enabled: bool,
) -> *mut CpuBase {
    if !online {
        offline
    } else if migration_enabled && !pinned {
        nohz
    } else {
        base
    }
}

/// Selects the target CPU base for a timer.
///
/// Rust owns the offline, pinned, and NOHZ migration decisions. C owns CPU
/// mask policy and per-CPU base lookup.
///
/// # Safety
///
/// `base` must identify a live CPU base. CPU hotplug and NOHZ state must
/// remain stable for the duration of the selection.
#[cfg(CONFIG_RUST_HRTIMER_TARGET_BASE)]
#[export]
unsafe extern "C" fn rust_hrtimer_target_base(base: *mut CpuBase, pinned: bool) -> *mut CpuBase {
    // SAFETY: The caller guarantees that the supplied CPU base is live.
    let online = unsafe { bindings::rust_helper_hrtimer_base_is_online(base) };
    if !online {
        // SAFETY: CPU hotplug state defines a valid housekeeping target.
        let cpu = unsafe { bindings::rust_helper_hrtimer_offline_target_cpu() };
        // SAFETY: The selected CPU owns a live per-CPU timer base.
        let offline = unsafe { bindings::rust_helper_hrtimer_percpu_base(cpu) };
        return select_target_base(base, pinned, offline, base, false, false);
    }

    #[cfg(CONFIG_NO_HZ_COMMON)]
    {
        // SAFETY: The caller keeps the timer base state stable.
        let migration_enabled =
            !pinned && unsafe { bindings::rust_helper_hrtimer_migration_enabled() };
        let nohz = if migration_enabled {
            // SAFETY: NOHZ migration selects a live per-CPU timer base.
            let cpu = unsafe { bindings::rust_helper_hrtimer_nohz_target_cpu() };
            // SAFETY: The selected CPU owns a live per-CPU timer base.
            unsafe { bindings::rust_helper_hrtimer_percpu_base(cpu) }
        } else {
            base
        };

        select_target_base(base, pinned, base, nohz, true, migration_enabled)
    }

    #[cfg(not(CONFIG_NO_HZ_COMMON))]
    {
        select_target_base(base, pinned, base, base, true, false)
    }
}

/// Runs the target-base decision with explicit test inputs.
///
/// # Safety
///
/// Each CPU base pointer must remain valid for the duration of the call.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_TARGET_BASE))]
#[export]
unsafe extern "C" fn rust_hrtimer_target_base_select(
    base: *mut CpuBase,
    pinned: bool,
    offline: *mut CpuBase,
    nohz: *mut CpuBase,
    online: bool,
    migration_enabled: bool,
) -> *mut CpuBase {
    select_target_base(base, pinned, offline, nohz, online, migration_enabled)
}

/// Returns whether an active timer can keep its local clock base.
///
/// Migration can move an unpinned timer away from a local non-housekeeping
/// CPU after the tick stops. A first or pinned timer remains local. When the
/// timer migration static branch is disabled, the base is retained only when
/// it is already local.
#[cfg(CONFIG_RUST_HRTIMER_KEEP_BASE)]
fn prefer_local(
    migration_enabled: bool,
    is_local: bool,
    is_first: bool,
    is_pinned: bool,
    housekeeping: bool,
    tick_stopped: bool,
    need_resched: bool,
) -> bool {
    if !migration_enabled {
        return is_local;
    }
    if !is_local {
        return false;
    }
    if is_first || is_pinned {
        return true;
    }
    if !housekeeping {
        return false;
    }

    !tick_stopped || need_resched
}

/// Keeps a running callback on its current base and selects local retention.
///
/// Rust owns the callback-running check, migration policy, first and pinned
/// timer retention, and NOHZ decision. C supplies the migration static
/// branch, housekeeping state, stopped-tick state, and scheduling state.
///
/// # Safety
///
/// `timer` must be live and its current clock base must remain stable for the
/// duration of the call.
#[cfg(CONFIG_RUST_HRTIMER_KEEP_BASE)]
#[export]
unsafe extern "C" fn rust_hrtimer_keep_base(
    timer: *mut Hrtimer,
    is_local: bool,
    is_first: bool,
    is_pinned: bool,
) -> bool {
    // SAFETY: The caller guarantees that the timer and base are live.
    if unsafe { bindings::rust_helper_hrtimer_callback_running(timer) } {
        return true;
    }

    // SAFETY: The caller keeps the timer migration policy stable for the call.
    let migration_enabled = unsafe { bindings::rust_helper_hrtimer_migration_enabled() };
    if !migration_enabled {
        return is_local;
    }
    if !is_local {
        return false;
    }
    if is_first || is_pinned {
        return true;
    }

    // SAFETY: The helper reads the current CPU housekeeping state.
    if !unsafe { bindings::rust_helper_hrtimer_local_housekeeping() } {
        return false;
    }

    // SAFETY: The helper reads the current CPU NOHZ tick state.
    !unsafe { bindings::rust_helper_hrtimer_tick_stopped() }
        // SAFETY: The helper reads the current task rescheduling state.
        || unsafe { bindings::rust_helper_hrtimer_need_resched() }
}

/// Runs the local-base preference decision with explicit test inputs.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_KEEP_BASE))]
#[export]
extern "C" fn rust_hrtimer_prefer_local_select(
    migration_enabled: bool,
    is_local: bool,
    is_first: bool,
    is_pinned: bool,
    housekeeping: bool,
    tick_stopped: bool,
    need_resched: bool,
) -> bool {
    prefer_local(
        migration_enabled,
        is_local,
        is_first,
        is_pinned,
        housekeeping,
        tick_stopped,
        need_resched,
    )
}

#[cfg(CONFIG_RUST_HRTIMER_START)]
const START_REPROGRAM_NONE: i32 = 0;
#[cfg(CONFIG_RUST_HRTIMER_START)]
const START_REPROGRAM: i32 = 1;
#[cfg(CONFIG_RUST_HRTIMER_START)]
const START_REPROGRAM_FORCE: i32 = 2;
#[cfg(CONFIG_RUST_HRTIMER_START)]
const START_REMOTE_WAKE: i32 = 3;
#[cfg(CONFIG_RUST_HRTIMER_START)]
const START_REPROGRAM_DEFERRED: i32 = 4;

#[cfg(CONFIG_RUST_HRTIMER_START)]
#[derive(Clone, Copy, Eq, PartialEq)]
enum StartAction {
    None,
    Reprogram,
    Force,
    RemoteWake,
    Deferred,
}

/// Converts an expiry to absolute time and applies low-resolution policy.
///
/// Relative timers use the supplied clock sample. A low-resolution system
/// records the relative mode on the timer and adds one timer resolution.
///
/// # Safety
///
/// `timer` must be live for the duration of the call.
#[cfg(CONFIG_RUST_HRTIMER_START)]
unsafe fn prepare_expiry(
    _timer: *mut Hrtimer,
    tim: bindings::ktime_t,
    mode: u32,
    relative_now: bindings::ktime_t,
) -> bindings::ktime_t {
    let mut expires = tim;
    let is_relative = mode & bindings::hrtimer_mode_HRTIMER_MODE_REL != 0;
    if is_relative {
        // SAFETY: The safe add wrapper preserves the kernel saturation rule.
        expires = unsafe { bindings::ktime_add_safe(expires, relative_now) };
    }

    #[cfg(CONFIG_TIME_LOW_RES)]
    {
        // SAFETY: The caller guarantees that the timer is live.
        unsafe { (*_timer).is_rel = is_relative };
        if is_relative {
            // SAFETY: The helper returns the configured timer resolution.
            let resolution = unsafe { bindings::rust_helper_hrtimer_lowres_resolution() };
            // SAFETY: The safe add wrapper preserves saturation.
            expires = unsafe { bindings::ktime_add_safe(expires, resolution) };
        }
    }

    expires
}

/// Selects the clock-event action after a timer startup.
///
/// A deferred rearm records that the remote CPU must reevaluate. A local or
/// online remote startup requests reprogramming only when the timer becomes
/// first. An offline current CPU wakes the selected remote CPU. A lazy timer
/// that remains behind the armed event avoids reprogramming.
#[cfg(CONFIG_RUST_HRTIMER_START)]
fn select_start_action(
    deferred: bool,
    was_first: bool,
    same_cpu: bool,
    this_cpu_online: bool,
    first: bool,
    lazy_keep_armed: bool,
) -> StartAction {
    if deferred {
        return StartAction::Deferred;
    }

    if !was_first || !same_cpu {
        if this_cpu_online {
            return if first {
                StartAction::Reprogram
            } else {
                StartAction::None
            };
        }

        return if first {
            StartAction::RemoteWake
        } else {
            StartAction::None
        };
    }

    if lazy_keep_armed {
        return StartAction::None;
    }

    StartAction::Force
}

/// Returns the local-base retention decision.
///
/// # Safety
///
/// `timer` must be live and its current base must remain stable.
#[cfg(CONFIG_RUST_HRTIMER_START)]
unsafe fn start_keep_base(
    timer: *mut Hrtimer,
    is_local: bool,
    is_first: bool,
    is_pinned: bool,
) -> bool {
    #[cfg(CONFIG_RUST_HRTIMER_KEEP_BASE)]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { rust_hrtimer_keep_base(timer, is_local, is_first, is_pinned) }
    }

    #[cfg(not(CONFIG_RUST_HRTIMER_KEEP_BASE))]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { bindings::rust_helper_hrtimer_keep_base(timer, is_local, is_first, is_pinned) }
    }
}

/// Removes a timer that is about to change its base.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`.
#[cfg(CONFIG_RUST_HRTIMER_START)]
unsafe fn start_remove_timer(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    _this_cpu_base: *mut CpuBase,
) -> bool {
    #[cfg(CONFIG_RUST_HRTIMER_CANCEL)]
    {
        // SAFETY: The caller guarantees that the timer is live.
        if !unsafe { bindings::rust_helper_hrtimer_is_queued(timer) } {
            return false;
        }

        // SAFETY: The caller guarantees that the timer is live.
        unsafe { bindings::rust_helper_hrtimer_debug_deactivate(timer) };
        // SAFETY: The caller guarantees that both CPU bases are live.
        let reprogram = unsafe { (*base).cpu_base == _this_cpu_base };
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { rust_hrtimer_remove(timer, base, true, reprogram) };
        true
    }

    #[cfg(not(CONFIG_RUST_HRTIMER_CANCEL))]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { bindings::rust_helper_hrtimer_remove_timer(timer, base, true) }
    }
}

/// Selects the timer base for startup.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold the base lock.
/// The function returns with the selected base lock held.
#[cfg(CONFIG_RUST_HRTIMER_START)]
unsafe fn start_switch_base(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    pinned: bool,
) -> *mut ClockBase {
    #[cfg(CONFIG_RUST_HRTIMER_BASE_MIGRATION)]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { rust_hrtimer_switch_base(timer, base, pinned) }
    }

    #[cfg(not(CONFIG_RUST_HRTIMER_BASE_MIGRATION))]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { bindings::rust_helper_hrtimer_switch_base(timer, base, pinned) }
    }
}

/// Inserts a timer during startup.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold the base lock.
#[cfg(CONFIG_RUST_HRTIMER_START)]
unsafe fn start_enqueue_timer(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    mode: u32,
    was_armed: bool,
) -> bool {
    #[cfg(CONFIG_RUST_HRTIMER_ENQUEUE)]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { rust_hrtimer_enqueue(timer, base, mode, was_armed) }
    }

    #[cfg(not(CONFIG_RUST_HRTIMER_ENQUEUE))]
    {
        // SAFETY: The caller satisfies the lower-unit requirements.
        unsafe { bindings::rust_helper_hrtimer_enqueue_timer(timer, base, mode, was_armed) }
    }
}

/// Implements the hrtimer startup state machine.
///
/// Rust converts and prepares the expiry, chooses local retention, removes or
/// requeues the timer, selects its final base, inserts it, and returns the
/// clock-event action for C to execute.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. `base` must match `timer->base`. The function may
/// release the old base lock and return with the selected base lock held.
#[cfg(CONFIG_RUST_HRTIMER_START)]
#[export]
unsafe extern "C" fn rust_hrtimer_start_range_ns(
    timer: *mut Hrtimer,
    tim: bindings::ktime_t,
    delta_ns: u64,
    mode: u32,
    mut base: *mut ClockBase,
) -> i32 {
    // SAFETY: The helper returns the live current CPU base.
    let this_cpu_base = unsafe { bindings::rust_helper_hrtimer_this_cpu_base() };
    // SAFETY: The caller guarantees that the initial base is live.
    let mut cpu_base = unsafe { (*base).cpu_base };
    // SAFETY: The caller guarantees that the current CPU base is live.
    let was_first = unsafe { (*cpu_base).next_timer == timer };
    let pinned = mode & bindings::hrtimer_mode_HRTIMER_MODE_PINNED != 0;
    let mut keep_base = false;

    // SAFETY: The caller guarantees that the current CPU base is live.
    if unsafe { (*this_cpu_base).online } {
        let is_local = cpu_base == this_cpu_base;
        // SAFETY: The timer remains locked for this decision.
        keep_base = unsafe { start_keep_base(timer, is_local, was_first, pinned) };
    }

    let relative = mode & bindings::hrtimer_mode_HRTIMER_MODE_REL != 0;
    let relative_now = if relative {
        // SAFETY: The timer belongs to the locked clock base.
        unsafe { bindings::hrtimer_cb_get_time(timer) }
    } else {
        0
    };
    // SAFETY: The caller guarantees that the timer is live.
    let tim = unsafe { prepare_expiry(timer, tim, mode, relative_now) };

    let first;
    if keep_base {
        // SAFETY: The timer remains on the locked base.
        first = unsafe { rust_hrtimer_requeue_same_base(timer, base, mode, tim, delta_ns) };
    } else {
        // SAFETY: The timer is locked while it is removed from its base.
        let was_armed = unsafe { start_remove_timer(timer, base, this_cpu_base) };
        // SAFETY: The caller guarantees that the timer is live.
        unsafe { bindings::rust_helper_hrtimer_set_expires_range_ns(timer, tim, delta_ns) };
        // SAFETY: The caller remains responsible for the returned base lock.
        base = unsafe { start_switch_base(timer, base, pinned) };
        // SAFETY: The selected clock base is live.
        cpu_base = unsafe { (*base).cpu_base };
        // SAFETY: The timer is unlocked from any queue and ready to insert.
        first = unsafe { start_enqueue_timer(timer, base, mode, was_armed) };
    }

    // SAFETY: The selected CPU base is live and locked.
    let deferred = unsafe { (*cpu_base).deferred_rearm };
    // SAFETY: The helper reads the state of the live current CPU base.
    let this_cpu_online = unsafe { bindings::rust_helper_hrtimer_base_is_online(this_cpu_base) };
    // SAFETY: The timer and selected CPU base are live and locked.
    let lazy_keep_armed =
        unsafe { (*timer).is_lazy && (*cpu_base).expires_next <= (*timer).node.expires };
    let action = select_start_action(
        deferred,
        was_first,
        cpu_base == this_cpu_base,
        this_cpu_online,
        first,
        lazy_keep_armed,
    );

    match action {
        StartAction::None => START_REPROGRAM_NONE,
        StartAction::Reprogram => START_REPROGRAM,
        StartAction::Force => START_REPROGRAM_FORCE,
        StartAction::RemoteWake => {
            // SAFETY: The selected CPU base is live and owns its callback data.
            unsafe { bindings::rust_helper_hrtimer_remote_reprogram(cpu_base) };
            START_REPROGRAM_NONE
        }
        StartAction::Deferred => {
            // SAFETY: The caller holds the selected CPU base lock.
            unsafe { (*cpu_base).deferred_needs_update = true };
            START_REPROGRAM_NONE
        }
    }
}

/// Runs expiry preparation with explicit test inputs.
///
/// # Safety
///
/// `timer` must be live for the duration of the call.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_START))]
#[export]
unsafe extern "C" fn rust_hrtimer_prepare_expiry(
    timer: *mut Hrtimer,
    tim: bindings::ktime_t,
    mode: u32,
    relative_now: bindings::ktime_t,
) -> bindings::ktime_t {
    // SAFETY: The caller guarantees that the timer is live.
    unsafe { prepare_expiry(timer, tim, mode, relative_now) }
}

/// Runs the post-startup clock-event action decision with explicit inputs.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_START))]
#[export]
extern "C" fn rust_hrtimer_start_reprogram_select(
    deferred: bool,
    was_first: bool,
    same_cpu: bool,
    this_cpu_online: bool,
    first: bool,
    lazy_keep_armed: bool,
) -> i32 {
    match select_start_action(
        deferred,
        was_first,
        same_cpu,
        this_cpu_online,
        first,
        lazy_keep_armed,
    ) {
        StartAction::None => START_REPROGRAM_NONE,
        StartAction::Reprogram => START_REPROGRAM,
        StartAction::Force => START_REPROGRAM_FORCE,
        StartAction::RemoteWake => START_REMOTE_WAKE,
        StartAction::Deferred => START_REPROGRAM_DEFERRED,
    }
}

/// Returns the user timer deadline when it can be the next expired timer.
///
/// The supplied soft expiry is the original user deadline. It is converted to
/// monotonic time. A deadline at or after the next armed timer does not need a
/// current-time check.
#[cfg(CONFIG_RUST_HRTIMER_USER_EXPIRY)]
fn user_expiry_candidate(
    soft_expires: bindings::ktime_t,
    offset: bindings::ktime_t,
    expires_next: bindings::ktime_t,
) -> Option<bindings::ktime_t> {
    let expires = soft_expires.wrapping_sub(offset);

    (expires < expires_next).then_some(expires)
}

/// Returns whether a candidate user deadline remains in the future.
#[cfg(CONFIG_RUST_HRTIMER_USER_EXPIRY)]
fn user_expiry_is_future(expires: bindings::ktime_t, now: bindings::ktime_t) -> bool {
    expires > now
}

/// Removes an expired user timer without programming the clock event.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`.
#[cfg(CONFIG_RUST_HRTIMER_USER_EXPIRY)]
unsafe fn remove_expired_user_timer(timer: *mut Hrtimer, base: *mut ClockBase) {
    #[cfg(CONFIG_RUST_HRTIMER_CANCEL)]
    {
        // SAFETY: The caller guarantees that the timer is live.
        unsafe { bindings::rust_helper_hrtimer_debug_deactivate(timer) };
        // SAFETY: The caller satisfies the removal requirements.
        unsafe { rust_hrtimer_remove(timer, base, false, false) };
    }

    #[cfg(not(CONFIG_RUST_HRTIMER_CANCEL))]
    {
        // SAFETY: The helper deactivates and removes a queued timer.
        let _ = unsafe { bindings::rust_helper_hrtimer_remove_timer(timer, base, false) };
    }
}

/// Validates and removes an already expired user timer.
///
/// Rust uses the timer's soft expiry, converts it to monotonic time, avoids a
/// clock sample when the timer cannot be first, and compares the remaining
/// candidate with the current time. An expired candidate is removed and the
/// caller identifies it with the existing start-expired tracepoint.
///
/// # Safety
///
/// `timer` must be live, and the caller must hold
/// `timer->base->cpu_base->lock`. `timer->base` must remain stable for the
/// duration of the call.
#[cfg(CONFIG_RUST_HRTIMER_USER_EXPIRY)]
#[export]
unsafe extern "C" fn rust_hrtimer_check_user_timer(timer: *mut Hrtimer) -> bool {
    // SAFETY: The caller guarantees that the timer and clock base are live.
    let base = unsafe { (*timer).base };
    // SAFETY: The caller guarantees that the clock and CPU bases are live.
    let cpu_base = unsafe { (*base).cpu_base };
    // SAFETY: The caller guarantees that the timer and clock base are live.
    let expires = match user_expiry_candidate(
        unsafe { (*timer)._softexpires },
        unsafe { (*base).offset },
        // SAFETY: The caller guarantees that the CPU base is live.
        unsafe { (*cpu_base).expires_next },
    ) {
        Some(expires) => expires,
        None => return true,
    };

    // SAFETY: It is safe to sample monotonic time in this context.
    let now = unsafe { bindings::ktime_get() };

    if user_expiry_is_future(expires, now) {
        return true;
    }

    // SAFETY: The caller satisfies the removal requirements.
    unsafe { remove_expired_user_timer(timer, base) };
    false
}

/// Runs the user-expiry decision with explicit test inputs.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_USER_EXPIRY))]
#[export]
extern "C" fn rust_hrtimer_user_expiry_select(
    soft_expires: bindings::ktime_t,
    offset: bindings::ktime_t,
    expires_next: bindings::ktime_t,
    now: bindings::ktime_t,
) -> bool {
    match user_expiry_candidate(soft_expires, offset, expires_next) {
        Some(expires) => user_expiry_is_future(expires, now),
        None => true,
    }
}

/// Returns whether a selected clock base can meet the timer deadline.
///
/// A local target is always suitable. An offline current CPU keeps the
/// elected remote target because that target can be reprogrammed by IPI.
/// Otherwise, the remote target is suitable only when it expires no later
/// than the timer.
///
/// # Safety
///
/// `timer`, `new_base`, `new_cpu_base`, and `this_cpu_base` must be live.
/// `new_base` must belong to `new_cpu_base`, and the caller must hold
/// `new_cpu_base->lock`.
#[cfg(CONFIG_RUST_HRTIMER_BASE_SELECTION)]
#[export]
unsafe extern "C" fn rust_hrtimer_suitable_target(
    timer: *mut Hrtimer,
    new_base: *mut ClockBase,
    new_cpu_base: *mut CpuBase,
    this_cpu_base: *mut CpuBase,
) -> bool {
    if new_cpu_base == this_cpu_base {
        return true;
    }

    // SAFETY: The caller guarantees that the current CPU base is live.
    if !unsafe { bindings::rust_helper_hrtimer_base_is_online(this_cpu_base) } {
        return true;
    }

    // SAFETY: The caller guarantees that the timer and clock base are live.
    let expires = unsafe { (*timer).node.expires.wrapping_sub((*new_base).offset) };
    // SAFETY: The caller holds the selected CPU base lock.
    expires >= unsafe { (*new_cpu_base).expires_next }
}

/// Moves a timer to a selected per-CPU clock base.
///
/// The timer remains locked across the old and new CPU base locks. A running
/// callback keeps the timer on its current base. An unsuitable remote target
/// returns the migration to the current CPU.
///
/// # Safety
///
/// `timer` and `base` must be live. The caller must hold
/// `base->cpu_base->lock`, and `base` must match `timer->base`. The timer must
/// not be queued. `this_cpu_base` and `new_cpu_base` must be live CPU bases.
/// The function returns with the lock held for the returned clock base.
#[cfg(CONFIG_RUST_HRTIMER_BASE_MIGRATION)]
unsafe fn switch_base_to(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    this_cpu_base: *mut CpuBase,
    mut new_cpu_base: *mut CpuBase,
) -> *mut ClockBase {
    // SAFETY: The caller guarantees that the current clock base is live.
    let basenum = unsafe { (*base).index } as usize;

    loop {
        // SAFETY: The caller guarantees that both CPU bases are live.
        let new_base = unsafe { ptr::addr_of_mut!((*new_cpu_base).clock_base[basenum]) };

        if new_base != base {
            // SAFETY: The caller guarantees that the timer is live and locked.
            if unsafe { bindings::rust_helper_hrtimer_callback_running(timer) } {
                return base;
            }

            // SAFETY: Both clock bases and their CPU base locks are live.
            unsafe { bindings::rust_helper_hrtimer_switch_locks(timer, base, new_base) };

            // SAFETY: The helper acquired the selected CPU base lock.
            if !unsafe {
                bindings::rust_helper_hrtimer_suitable_target(
                    timer,
                    new_base,
                    new_cpu_base,
                    this_cpu_base,
                )
            } {
                // SAFETY: The unselected target is locked and the timer is not
                // yet published on it.
                unsafe { bindings::rust_helper_hrtimer_switch_rollback(timer, base, new_base) };
                new_cpu_base = this_cpu_base;
                continue;
            }

            // SAFETY: The selected target is locked and suitable.
            unsafe { bindings::rust_helper_hrtimer_switch_commit(timer, new_base) };
        } else {
            // SAFETY: The current CPU base and selected clock base are locked.
            if !unsafe {
                bindings::rust_helper_hrtimer_suitable_target(
                    timer,
                    new_base,
                    new_cpu_base,
                    this_cpu_base,
                )
            } {
                new_cpu_base = this_cpu_base;
                continue;
            }
        }

        return new_base;
    }
}

/// Moves a timer to the selected per-CPU clock base.
///
/// Rust owns target migration, callback pinning, deadline suitability retries,
/// and migration commit or rollback. C owns per-CPU base lookup and the raw
/// spinlock handoff between the old and new CPU bases.
///
/// # Safety
///
/// The timer must be live and unlocked from any timer queue. The caller must
/// hold `base->cpu_base->lock`, and `base` must match `timer->base`. The
/// function returns with the lock held for the returned clock base.
#[cfg(CONFIG_RUST_HRTIMER_BASE_MIGRATION)]
#[export]
unsafe extern "C" fn rust_hrtimer_switch_base(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    pinned: bool,
) -> *mut ClockBase {
    // SAFETY: The caller guarantees that the timer is live and locked.
    let this_cpu_base = unsafe { bindings::rust_helper_hrtimer_this_cpu_base() };
    #[cfg(CONFIG_RUST_HRTIMER_TARGET_BASE)]
    // SAFETY: The caller guarantees that the current CPU base is live.
    let new_cpu_base = unsafe { rust_hrtimer_target_base(this_cpu_base, pinned) };
    #[cfg(not(CONFIG_RUST_HRTIMER_TARGET_BASE))]
    // SAFETY: The caller guarantees that the current CPU base is live.
    let new_cpu_base =
        unsafe { bindings::rust_helper_hrtimer_target_cpu_base(this_cpu_base, pinned) };

    // SAFETY: The caller satisfies the migration safety requirements.
    unsafe { switch_base_to(timer, base, this_cpu_base, new_cpu_base) }
}

/// Runs the base migration state machine with explicit test CPU bases.
///
/// # Safety
///
/// The timer, clock bases, and CPU bases must be live. The caller must hold
/// `base->cpu_base->lock`, and `base` must match `timer->base`. The timer must
/// not be queued. The function returns with one selected CPU base lock held.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_BASE_MIGRATION))]
#[export]
unsafe extern "C" fn rust_hrtimer_switch_base_to(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    this_cpu_base: *mut CpuBase,
    new_cpu_base: *mut CpuBase,
) -> *mut ClockBase {
    // SAFETY: The caller satisfies all migration safety requirements.
    unsafe { switch_base_to(timer, base, this_cpu_base, new_cpu_base) }
}

/// Returns the first timer on a clock base when its soft expiry has passed.
///
/// The clock base must be protected by its CPU base lock. A missing first
/// timer or a soft expiry after `basenow` returns a null pointer.
///
/// # Safety
///
/// `base` must be live and protected by its CPU base lock. A nonempty active
/// queue must contain live hrtimer nodes.
#[cfg(CONFIG_RUST_HRTIMER_RUN_QUEUES)]
unsafe fn clock_base_next_expired(
    base: *mut ClockBase,
    basenow: bindings::ktime_t,
) -> *mut Hrtimer {
    // SAFETY: The caller guarantees that the clock base is live.
    let root = unsafe { ptr::addr_of_mut!((*base).active.rb_root) };
    // SAFETY: The caller holds the CPU base lock that protects the queue.
    let node = unsafe { (*root).rb_leftmost };
    if node.is_null() {
        return ptr::null_mut();
    }

    // SAFETY: A nonempty linked tree has a live leftmost timer node.
    let timer_node = unsafe { timer_linked_node(node) };
    // SAFETY: A timerqueue node is embedded in its owning timer.
    let timer = unsafe { crate::container_of!(timer_node, Hrtimer, node) };
    // SAFETY: The caller guarantees that the selected timer is live.
    if basenow < unsafe { (*timer)._softexpires } {
        return ptr::null_mut();
    }

    timer
}

/// Runs the expired timer loops across a stable active-base snapshot.
///
/// Rust owns base traversal, clock offset application, soft-expiry checks,
/// and repeated selection after each callback. C executes the callback and
/// performs the required lock and softirq waiter handling.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock. Every active
/// bit selected at entry must identify a linked timer queue that remains
/// valid across the run.
#[cfg(CONFIG_RUST_HRTIMER_RUN_QUEUES)]
#[export]
unsafe extern "C" fn rust_hrtimer_run_queues(
    cpu_base: *mut CpuBase,
    now: bindings::ktime_t,
    flags: usize,
    active_mask: u32,
) {
    // SAFETY: The caller guarantees that the CPU base is live.
    let active = unsafe { (*cpu_base).active_bases } & active_mask;
    let max_bases = bindings::hrtimer_base_type_HRTIMER_MAX_CLOCK_BASES as usize;

    for index in 0..max_bases {
        if active & (1_u32 << index) == 0 {
            continue;
        }

        // SAFETY: The entry snapshot only selects live clock bases.
        let base = unsafe { ptr::addr_of_mut!((*cpu_base).clock_base[index]) };
        // SAFETY: The clock offset remains valid while the CPU lock is held.
        let basenow = now.wrapping_add(unsafe { (*base).offset });

        loop {
            // SAFETY: The active queue is protected by the CPU base lock.
            let timer = unsafe { clock_base_next_expired(base, basenow) };
            if timer.is_null() {
                break;
            }

            // SAFETY: The selected timer is queued on this live clock base,
            // and the CPU base lock protects both objects.
            unsafe {
                bindings::rust_helper_hrtimer_run_expired(
                    cpu_base,
                    base,
                    timer,
                    basenow,
                    flags,
                    active_mask == HRTIMER_ACTIVE_SOFT,
                )
            };
        }
    }
}

/// Runs the per-base expired timer decision with explicit test input.
///
/// # Safety
///
/// `base` must be live and protected by its CPU base lock. A nonempty active
/// queue must contain live hrtimer nodes.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_RUN_QUEUES))]
#[export]
unsafe extern "C" fn rust_hrtimer_clock_base_next_expired(
    base: *mut ClockBase,
    basenow: bindings::ktime_t,
) -> *mut Hrtimer {
    // SAFETY: The caller satisfies the queue and lock requirements.
    unsafe { clock_base_next_expired(base, basenow) }
}

/// The maximum number of interrupt rechecks after a callback drops the lock.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
const HRTIMER_INTERRUPT_RETRY_LIMIT: u32 = 3;

/// The maximum hang delay applied directly by an hrtimer interrupt.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
const HRTIMER_MAX_HANG_DELAY_NS: u32 = 100 * 1000 * 1000;

/// The next action after a callback has dropped and reacquired the base lock.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
enum InterruptProgress {
    Done,
    Retry,
    Hang,
}

/// The selected interrupt progress and the resulting retry count.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
struct InterruptDecision {
    progress: InterruptProgress,
    retries: u32,
}

/// Selects between completion, another pass, and hang detection.
///
/// The timer code permits two rechecks after a hardirq callback. The third
/// observation of an already expired next event is a hang.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
fn interrupt_progress(
    expires_next: bindings::ktime_t,
    now: bindings::ktime_t,
    retries: u32,
) -> InterruptDecision {
    if expires_next >= now {
        return InterruptDecision {
            progress: InterruptProgress::Done,
            retries,
        };
    }

    let retries = retries + 1;
    let progress = if retries < HRTIMER_INTERRUPT_RETRY_LIMIT {
        InterruptProgress::Retry
    } else {
        InterruptProgress::Hang
    };

    InterruptDecision { progress, retries }
}

/// Applies an interrupt progress decision to the CPU base state.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
unsafe fn advance_interrupt(
    cpu_base: *mut CpuBase,
    expires_next: bindings::ktime_t,
    now: bindings::ktime_t,
    entry_time: bindings::ktime_t,
    retries: u32,
) -> InterruptDecision {
    // SAFETY: The caller holds the CPU base lock.
    unsafe { (*cpu_base).hang_detected = false };
    let decision = interrupt_progress(expires_next, now, retries);

    match decision.progress {
        InterruptProgress::Done => {}
        InterruptProgress::Retry => {
            // SAFETY: The caller holds the CPU base lock.
            unsafe {
                (*cpu_base).nr_retries = (*cpu_base).nr_retries.wrapping_add(1);
            }
        }
        InterruptProgress::Hang => {
            // SAFETY: The caller holds the CPU base lock.
            unsafe {
                let delta = now.wrapping_sub(entry_time) as u32;
                (*cpu_base).max_hang_time = (*cpu_base).max_hang_time.max(delta);
                (*cpu_base).nr_hangs = (*cpu_base).nr_hangs.wrapping_add(1);
                (*cpu_base).hang_detected = true;
            }
        }
    }

    decision
}

/// Returns whether the pending softirq deadline has been reached.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
fn softirq_due(now: bindings::ktime_t, softirq_expires_next: bindings::ktime_t) -> bool {
    now >= softirq_expires_next
}

/// Returns the direct hang-delay expiry, capped at 100 milliseconds.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
fn hang_expiry(now: bindings::ktime_t, max_hang_time: u32) -> bindings::ktime_t {
    let delay = bindings::ktime_t::from(max_hang_time.min(HRTIMER_MAX_HANG_DELAY_NS));

    now.wrapping_add(delay)
}

/// Programs the selected high-resolution timer rearm.
///
/// Rust updates the CPU base deadline, clears the pending state, applies the
/// hang delay, and calls the narrow clock event programming adapter.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
unsafe fn rearm(cpu_base: *mut CpuBase, expires_next: bindings::ktime_t, deferred: bool) {
    // SAFETY: The caller holds the CPU base lock.
    let hang_detected = unsafe { (*cpu_base).hang_detected };
    // SAFETY: The caller holds the CPU base lock.
    unsafe {
        (*cpu_base).expires_next = expires_next;
        (*cpu_base).deferred_rearm = false;
    }

    let programmed = if hang_detected {
        // SAFETY: The caller holds the CPU base lock.
        let max_hang_time = unsafe { (*cpu_base).max_hang_time };
        // SAFETY: It is safe to sample monotonic time in this context.
        let now = unsafe { bindings::ktime_get() };
        hang_expiry(now, max_hang_time)
    } else {
        expires_next
    };

    // SAFETY: The caller holds the CPU base lock and owns the event rearm.
    unsafe { bindings::rust_helper_hrtimer_interrupt_program_event(programmed, deferred) };
}

/// Publishes the selected interrupt rearm.
///
/// Immediate rearm programs the selected deadline. Deferred rearm caches the
/// deadline and requests thread-work execution.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
unsafe fn finalize_interrupt_rearm(
    cpu_base: *mut CpuBase,
    expires_next: bindings::ktime_t,
    deferred_rearm: bool,
) {
    if deferred_rearm {
        // SAFETY: The caller holds the CPU base lock.
        unsafe {
            (*cpu_base).deferred_needs_update = false;
            (*cpu_base).deferred_expires_next = expires_next;
        }
        // SAFETY: The current task can request deferred rearm execution.
        unsafe { bindings::rust_helper_hrtimer_interrupt_defer_rearm() };
    } else {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { rearm(cpu_base, expires_next, false) };
    }
}

/// Selects the expiry used to consume a pending deferred rearm.
///
/// Queue changes set `deferred_needs_update`, which requires a fresh clock
/// update and next-event search. Without a queue change, the interrupt's
/// cached expiry remains valid.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_REARM)]
unsafe fn select_deferred_rearm_expiry(cpu_base: *mut CpuBase) -> bindings::ktime_t {
    // SAFETY: The caller holds the CPU base lock.
    if unsafe { (*cpu_base).deferred_needs_update } {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { bindings::rust_helper_hrtimer_interrupt_update_base(cpu_base) };
        // SAFETY: The caller holds the CPU base lock.
        unsafe { bindings::rust_helper_hrtimer_interrupt_update_next_event(cpu_base) }
    } else {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).deferred_expires_next }
    }
}

/// Returns whether a deferred rearm is pending.
///
/// The caller invokes this CPU-local fast-path check before acquiring the CPU
/// base lock. The state is published by the current CPU, so the volatile read
/// samples the latest value without claiming cross-CPU synchronization.
///
/// # Safety
///
/// `cpu_base` must point to a live current-CPU base.
#[cfg(CONFIG_RUST_HRTIMER_REARM)]
#[export]
unsafe extern "C" fn rust_hrtimer_rearm_pending(cpu_base: *mut CpuBase) -> bool {
    // SAFETY: The caller guarantees that the current-CPU base is live.
    unsafe { ptr::read_volatile(ptr::addr_of!((*cpu_base).deferred_rearm)) }
}

/// Consumes a pending deferred rearm under the CPU base lock.
///
/// Rust selects the cached or re-evaluated expiry, then updates and programs
/// the clock event through the common rearm state machine.
///
/// # Safety
///
/// `cpu_base` must be the live current-CPU base and must be locked.
#[cfg(CONFIG_RUST_HRTIMER_REARM)]
#[export]
unsafe extern "C" fn rust_hrtimer_rearm_deferred_locked(cpu_base: *mut CpuBase) {
    // SAFETY: The caller holds the CPU base lock.
    let expires_next = unsafe { select_deferred_rearm_expiry(cpu_base) };
    // SAFETY: The caller holds the CPU base lock.
    unsafe { rearm(cpu_base, expires_next, true) };
}

/// Selects the deferred rearm expiry with an explicit test CPU base.
///
/// # Safety
///
/// `cpu_base` must be live and isolated from concurrent users.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_REARM))]
#[export]
unsafe extern "C" fn rust_hrtimer_rearm_deferred_select(
    cpu_base: *mut CpuBase,
) -> bindings::ktime_t {
    // SAFETY: The caller guarantees exclusive access to the CPU base.
    unsafe { select_deferred_rearm_expiry(cpu_base) }
}

/// Runs the high-resolution timer interrupt state machine.
///
/// Rust owns retry selection, softirq activation, hang detection, counters,
/// and the final immediate or deferred rearm choice. C updates clock offsets,
/// runs the hard timer queues, programs the clock event device, and retains
/// interrupt-context locking.
///
/// # Safety
///
/// `cpu_base` must be live and locked by the caller in interrupt context.
/// `flags` must be the saved interrupt state for that lock. The caller keeps
/// interrupts disabled across the call.
#[cfg(CONFIG_RUST_HRTIMER_INTERRUPT)]
#[export]
unsafe extern "C" fn rust_hrtimer_interrupt(cpu_base: *mut CpuBase, flags: usize) {
    // SAFETY: The caller guarantees that the CPU base is live and locked.
    let entry_time = unsafe { bindings::rust_helper_hrtimer_interrupt_update_base(cpu_base) };
    // SAFETY: The helper reports the compile-time rearm configuration.
    let deferred_rearm = unsafe { bindings::rust_helper_hrtimer_interrupt_deferred_rearm() };
    let mut now = entry_time;
    let mut retries = 0;

    loop {
        // SAFETY: The caller holds the CPU base lock.
        unsafe {
            (*cpu_base).deferred_rearm = true;
            (*cpu_base).expires_next = bindings::ktime_t::MAX;
        }

        // SAFETY: The caller holds the CPU base lock.
        let softirq_expires_next = unsafe { (*cpu_base).softirq_expires_next };
        if softirq_due(now, softirq_expires_next) {
            // SAFETY: The caller holds the CPU base lock.
            unsafe {
                (*cpu_base).softirq_expires_next = bindings::ktime_t::MAX;
                (*cpu_base).softirq_activated = true;
                bindings::rust_helper_hrtimer_interrupt_raise_softirq();
            }
        }

        // SAFETY: Queue execution preserves the CPU base lock protocol.
        unsafe { bindings::rust_helper_hrtimer_interrupt_run_queues(cpu_base, now, flags) };

        // SAFETY: Queue execution returns with the CPU base lock held.
        now = unsafe { bindings::rust_helper_hrtimer_interrupt_update_base(cpu_base) };
        // SAFETY: The caller holds the CPU base lock.
        let expires_next =
            unsafe { bindings::rust_helper_hrtimer_interrupt_update_next_event(cpu_base) };

        // SAFETY: The caller holds the CPU base lock.
        let decision =
            unsafe { advance_interrupt(cpu_base, expires_next, now, entry_time, retries) };
        retries = decision.retries;
        match decision.progress {
            InterruptProgress::Done => {
                // SAFETY: The caller satisfies the final rearm requirements.
                unsafe { finalize_interrupt_rearm(cpu_base, expires_next, deferred_rearm) };
                return;
            }
            InterruptProgress::Retry => {}
            InterruptProgress::Hang => {
                // SAFETY: The caller satisfies the final rearm requirements.
                unsafe { finalize_interrupt_rearm(cpu_base, expires_next, deferred_rearm) };
                return;
            }
        }
    }
}

/// Runs the interrupt retry decision with explicit test inputs.
///
/// # Safety
///
/// `next_retries` must be writable for the duration of the call.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_INTERRUPT))]
#[export]
unsafe extern "C" fn rust_hrtimer_interrupt_select(
    expires_next: bindings::ktime_t,
    now: bindings::ktime_t,
    retries: u32,
    next_retries: *mut u32,
) -> i32 {
    let decision = interrupt_progress(expires_next, now, retries);
    // SAFETY: The caller guarantees that the output is writable.
    unsafe { *next_retries = decision.retries };

    match decision.progress {
        InterruptProgress::Done => 0,
        InterruptProgress::Retry => 1,
        InterruptProgress::Hang => 2,
    }
}

/// Applies the interrupt progress and counters with explicit test inputs.
///
/// # Safety
///
/// `cpu_base` must be live and isolated from concurrent users.
/// `next_retries` must be writable for the duration of the call.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_INTERRUPT))]
#[export]
unsafe extern "C" fn rust_hrtimer_interrupt_advance(
    cpu_base: *mut CpuBase,
    expires_next: bindings::ktime_t,
    now: bindings::ktime_t,
    entry_time: bindings::ktime_t,
    retries: u32,
    next_retries: *mut u32,
) -> i32 {
    // SAFETY: The caller guarantees exclusive access to the CPU base.
    let decision = unsafe { advance_interrupt(cpu_base, expires_next, now, entry_time, retries) };
    // SAFETY: The caller guarantees that the output is writable.
    unsafe { *next_retries = decision.retries };

    match decision.progress {
        InterruptProgress::Done => 0,
        InterruptProgress::Retry => 1,
        InterruptProgress::Hang => 2,
    }
}

/// Returns whether the softirq deadline has passed.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_INTERRUPT))]
#[export]
extern "C" fn rust_hrtimer_interrupt_softirq_due(
    now: bindings::ktime_t,
    softirq_expires_next: bindings::ktime_t,
) -> bool {
    softirq_due(now, softirq_expires_next)
}

/// Returns the direct hang-delay expiry with explicit test inputs.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_INTERRUPT))]
#[export]
extern "C" fn rust_hrtimer_interrupt_hang_expiry(
    now: bindings::ktime_t,
    max_hang_time: u32,
) -> bindings::ktime_t {
    hang_expiry(now, max_hang_time)
}

/// Updates the earliest expiry and timer across the active clock bases.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock. Each bit in
/// `active` must identify a live, nonempty linked timer queue.
#[cfg(CONFIG_RUST_HRTIMER_NEXT_EVENT)]
unsafe fn update_next_event(
    cpu_base: *mut CpuBase,
    active: u32,
    expires_next: &mut bindings::ktime_t,
    next_timer: &mut *mut Hrtimer,
) {
    for index in 0..bindings::hrtimer_base_type_HRTIMER_MAX_CLOCK_BASES as usize {
        if active & (1_u32 << index) == 0 {
            continue;
        }

        // SAFETY: The active bit identifies a live clock base.
        let base = unsafe { ptr::addr_of_mut!((*cpu_base).clock_base[index]) };
        // SAFETY: The caller guarantees that the clock base is live.
        let expires = unsafe { (*base).expires_next.wrapping_sub((*base).offset) };
        if expires >= *expires_next {
            continue;
        }

        // SAFETY: The caller guarantees that this active queue is nonempty.
        let root = unsafe { ptr::addr_of_mut!((*base).active.rb_root) };
        // SAFETY: A nonempty linked tree has a live leftmost node.
        let node = unsafe { (*root).rb_leftmost };
        // SAFETY: The active queue contains only live timer nodes.
        let timer_node = unsafe { timer_linked_node(node) };
        // SAFETY: A timerqueue node is embedded in its owning timer.
        *next_timer = unsafe { crate::container_of!(timer_node, Hrtimer, node) };
        *expires_next = expires;
    }
}

/// Recomputes the earliest hard or soft event for a CPU base.
///
/// Rust searches the selected active clock bases, maintains the hard and soft
/// next-timer pointers, and normalizes a negative result to zero. Clock event
/// reprogramming and softirq execution remain outside this function.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock. `active_mask`
/// must select hard bases, soft bases, or both. Each selected active bit must
/// identify a nonempty linked timer queue.
#[cfg(CONFIG_RUST_HRTIMER_NEXT_EVENT)]
#[export]
unsafe extern "C" fn rust_hrtimer_get_next_event(
    cpu_base: *mut CpuBase,
    active_mask: u32,
) -> bindings::ktime_t {
    let mut expires_next = bindings::ktime_t::MAX;
    let mut next_timer = ptr::null_mut();
    // SAFETY: The caller guarantees that the CPU base is live.
    let softirq_activated = unsafe { (*cpu_base).softirq_activated };

    if !softirq_activated && active_mask & HRTIMER_ACTIVE_SOFT != 0 {
        // SAFETY: The caller guarantees that the CPU base is live.
        let active = unsafe { (*cpu_base).active_bases } & HRTIMER_ACTIVE_SOFT;
        if active != 0 {
            // SAFETY: The caller guarantees that active bits have live queues.
            unsafe { update_next_event(cpu_base, active, &mut expires_next, &mut next_timer) };
        }
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).softirq_next_timer = next_timer };
    }

    if active_mask & HRTIMER_ACTIVE_HARD != 0 {
        // SAFETY: The caller guarantees that the CPU base is live.
        let active = unsafe { (*cpu_base).active_bases } & HRTIMER_ACTIVE_HARD;
        if active != 0 {
            // SAFETY: The caller guarantees that active bits have live queues.
            unsafe { update_next_event(cpu_base, active, &mut expires_next, &mut next_timer) };
        }
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).next_timer = next_timer };
    }

    expires_next.max(0)
}

/// Combines hard and soft next events for clock event reprogramming.
///
/// Rust refreshes the soft expiry unless a softirq is already active, selects
/// the hard next event, and replaces it with an earlier soft event. The result
/// does not program the clock event device.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock. Active clock
/// base bits must identify nonempty linked timer queues.
#[cfg(CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT)]
#[export]
unsafe extern "C" fn rust_hrtimer_update_next_event(cpu_base: *mut CpuBase) -> bindings::ktime_t {
    let mut soft = bindings::ktime_t::MAX;
    // SAFETY: The caller guarantees that the CPU base is live.
    let softirq_activated = unsafe { (*cpu_base).softirq_activated };

    if !softirq_activated {
        // SAFETY: The caller guarantees that active bits have live queues.
        soft = unsafe { rust_hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_SOFT) };
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).softirq_expires_next = soft };
    }

    // SAFETY: The caller guarantees that active bits have live queues.
    let mut expires_next = unsafe { rust_hrtimer_get_next_event(cpu_base, HRTIMER_ACTIVE_HARD) };
    if expires_next > soft {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).next_timer = (*cpu_base).softirq_next_timer };
        expires_next = soft;
    }

    expires_next
}

/// The clock event action selected after reevaluating the next timer.
#[cfg(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)]
enum ForceReprogramAction {
    Skip,
    Update,
    Program,
}

/// Selects the force-reprogram action without touching the clock device.
#[cfg(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)]
fn force_reprogram_action(
    expires_next: bindings::ktime_t,
    current_expires: bindings::ktime_t,
    skip_equal: bool,
    hres_active: bool,
    hang_detected: bool,
) -> ForceReprogramAction {
    if skip_equal && expires_next == current_expires {
        return ForceReprogramAction::Skip;
    }

    if hres_active && !hang_detected {
        ForceReprogramAction::Program
    } else {
        ForceReprogramAction::Update
    }
}

/// Applies a force-reprogram action to the CPU base.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)]
unsafe fn apply_force_reprogram(
    cpu_base: *mut CpuBase,
    expires_next: bindings::ktime_t,
    action: ForceReprogramAction,
) {
    if matches!(action, ForceReprogramAction::Skip) {
        return;
    }

    // SAFETY: The caller holds the CPU base lock.
    unsafe { (*cpu_base).expires_next = expires_next };

    if matches!(action, ForceReprogramAction::Program) {
        // SAFETY: The caller holds the CPU base lock and owns the event update.
        unsafe { bindings::rust_helper_hrtimer_interrupt_program_event(expires_next, false) };
    }
}

/// Updates the shared clock event state after a caller selected a new expiry.
///
/// Rust records the expiry, leaves the active hardware deadline unchanged
/// while high-resolution mode is inactive or a hang is being recovered, and
/// programs the clock event device only when those conditions permit.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)]
#[export]
unsafe extern "C" fn rust_hrtimer_reprogram_device(
    cpu_base: *mut CpuBase,
    expires_next: bindings::ktime_t,
) {
    // SAFETY: The caller holds the CPU base lock.
    let hres_active = unsafe { (*cpu_base).hres_active };
    // SAFETY: The caller holds the CPU base lock.
    let hang_detected = unsafe { (*cpu_base).hang_detected };
    let action = force_reprogram_action(
        expires_next,
        expires_next,
        false,
        hres_active,
        hang_detected,
    );

    // SAFETY: The caller holds the CPU base lock.
    unsafe { apply_force_reprogram(cpu_base, expires_next, action) };
}

/// Reevaluates all clock bases and reprograms the clock event device.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock. Active clock
/// base bits must identify nonempty linked timer queues.
#[cfg(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)]
#[export]
unsafe extern "C" fn rust_hrtimer_force_reprogram(cpu_base: *mut CpuBase, skip_equal: bool) {
    // SAFETY: The caller holds the CPU base lock.
    let expires_next =
        unsafe { bindings::rust_helper_hrtimer_force_reprogram_next_event(cpu_base) };
    // SAFETY: The caller holds the CPU base lock.
    let current_expires = unsafe { (*cpu_base).expires_next };
    // SAFETY: The caller holds the CPU base lock.
    let hres_active = unsafe { (*cpu_base).hres_active };
    // SAFETY: The caller holds the CPU base lock.
    let hang_detected = unsafe { (*cpu_base).hang_detected };
    let action = force_reprogram_action(
        expires_next,
        current_expires,
        skip_equal,
        hres_active,
        hang_detected,
    );

    // SAFETY: The caller holds the CPU base lock.
    unsafe { apply_force_reprogram(cpu_base, expires_next, action) };
}

/// Selects a force-reprogram action with explicit test inputs.
#[cfg(all(CONFIG_KUNIT, CONFIG_RUST_HRTIMER_FORCE_REPROGRAM))]
#[export]
extern "C" fn rust_hrtimer_force_reprogram_select(
    expires_next: bindings::ktime_t,
    current_expires: bindings::ktime_t,
    skip_equal: bool,
    hres_active: bool,
    hang_detected: bool,
) -> u32 {
    match force_reprogram_action(
        expires_next,
        current_expires,
        skip_equal,
        hres_active,
        hang_detected,
    ) {
        ForceReprogramAction::Skip => 0,
        ForceReprogramAction::Update => 1,
        ForceReprogramAction::Program => 2,
    }
}

/// Returns whether a clock change requires a remote CPU timer interrupt.
///
/// The caller updates clock offsets before this decision. Rust checks whether
/// the clock-change sequence moved, records deferred-rearm work, and walks
/// the affected active clock bases for a hard or soft event that now precedes
/// the programmed event.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock after the clock
/// offset update. Active bits must identify nonempty linked timer queues.
#[cfg(CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI)]
#[export]
unsafe extern "C" fn rust_hrtimer_update_needs_ipi(
    cpu_base: *mut CpuBase,
    active: u32,
    seq: u32,
) -> bool {
    // SAFETY: The caller guarantees that the CPU base is live.
    if seq == unsafe { (*cpu_base).clock_was_set_seq } {
        return false;
    }

    // SAFETY: The caller guarantees that the CPU base is live.
    if unsafe { (*cpu_base).deferred_rearm } {
        // SAFETY: The caller holds the CPU base lock.
        unsafe { (*cpu_base).deferred_needs_update = true };
        return false;
    }

    // SAFETY: The caller guarantees that the CPU base is live.
    let active = active & unsafe { (*cpu_base).active_bases };
    // SAFETY: The caller guarantees that the CPU base is live.
    let expires_next = unsafe { (*cpu_base).expires_next };
    // SAFETY: The caller guarantees that the CPU base is live.
    let softirq_activated = unsafe { (*cpu_base).softirq_activated };

    for index in 0..bindings::hrtimer_base_type_HRTIMER_MAX_CLOCK_BASES as usize {
        if active & (1_u32 << index) == 0 {
            continue;
        }

        // SAFETY: The active bit identifies a live clock base.
        let base = unsafe { ptr::addr_of_mut!((*cpu_base).clock_base[index]) };
        // SAFETY: The caller guarantees that this active queue is nonempty.
        let root = unsafe { ptr::addr_of_mut!((*base).active.rb_root) };
        // SAFETY: A nonempty linked tree has a live leftmost node.
        let node = unsafe { (*root).rb_leftmost };
        // SAFETY: The active queue contains only live timer nodes.
        let timer_node = unsafe { timer_linked_node(node) };
        // SAFETY: The timer node is live and belongs to this clock base.
        let expires = unsafe { (*timer_node).expires.wrapping_sub((*base).offset) };

        if expires < expires_next {
            return true;
        }

        if index < HRTIMER_ACTIVE_MASK_SHIFT as usize || softirq_activated {
            continue;
        }

        // SAFETY: The caller guarantees that the CPU base is live.
        if expires < unsafe { (*cpu_base).softirq_expires_next } {
            return true;
        }
    }

    false
}

/// Decides whether a newly queued timer changes clock event programming.
///
/// Rust normalizes the timer expiry, updates a remote or local soft-timer
/// cache, rejects events that cannot improve the programmed deadline, and
/// publishes the timer as `next_timer` when reprogramming is required. The
/// caller programs the clock event device with the returned deadline.
///
/// # Safety
///
/// `timer` and `cpu_base` must be live, and `timer->base` must identify a live
/// clock base protected by its CPU base lock. `program_device` must be writable
/// and must not overlap either input.
#[cfg(CONFIG_RUST_HRTIMER_REPROGRAM)]
#[export]
unsafe extern "C" fn rust_hrtimer_reprogram(
    timer: *mut Hrtimer,
    cpu_base: *mut CpuBase,
    reprogram: bool,
    program_device: *mut bool,
) -> bindings::ktime_t {
    // SAFETY: The caller guarantees that the output is writable.
    unsafe { *program_device = false };

    // SAFETY: The caller guarantees that the timer and its base are live.
    let base = unsafe { (*timer).base };
    // SAFETY: The caller guarantees that the timer is live.
    let expires = unsafe { (*timer).node.expires };
    // SAFETY: The caller guarantees that the clock base is live.
    let expires = expires.wrapping_sub(unsafe { (*base).offset }).max(0);

    // SAFETY: The caller guarantees that the timer is live.
    if unsafe { (*timer).is_soft } {
        // SAFETY: The caller guarantees that the clock base is live.
        let timer_cpu_base = unsafe { (*base).cpu_base };

        // SAFETY: The caller guarantees that the timer CPU base is live.
        if unsafe { (*timer_cpu_base).softirq_activated } {
            return expires;
        }

        // SAFETY: The caller guarantees that the timer CPU base is live.
        if expires >= unsafe { (*timer_cpu_base).softirq_expires_next } {
            return expires;
        }

        // SAFETY: The caller holds the timer CPU base lock.
        unsafe {
            (*timer_cpu_base).softirq_next_timer = timer;
            (*timer_cpu_base).softirq_expires_next = expires;
        }

        // SAFETY: The caller guarantees that the timer CPU base is live.
        if !reprogram || expires >= unsafe { (*timer_cpu_base).expires_next } {
            return expires;
        }
    }

    // SAFETY: The caller guarantees that both CPU bases are live.
    if unsafe { (*base).cpu_base } != cpu_base {
        return expires;
    }

    // SAFETY: The caller guarantees that the current CPU base is live.
    if expires >= unsafe { (*cpu_base).expires_next } {
        return expires;
    }

    // SAFETY: The caller guarantees that the current CPU base is live.
    if unsafe { (*cpu_base).deferred_rearm } {
        return expires;
    }

    // SAFETY: The caller holds the current CPU base lock.
    unsafe {
        (*cpu_base).next_timer = timer;
        *program_device = true;
    }

    expires
}

/// Inserts a timer into a clock base and publishes its queued state.
///
/// Rust performs the debug and trace adapter calls, marks the CPU base active,
/// publishes the lockless queued state, inserts the linked timer entry, and
/// updates the cached first expiry when the timer becomes the leftmost entry.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. The timer must belong to `base` and must not already
/// be linked into its timer queue.
#[cfg(CONFIG_RUST_HRTIMER_ENQUEUE)]
#[export]
unsafe extern "C" fn rust_hrtimer_enqueue(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    mode: u32,
    was_armed: bool,
) -> bool {
    // SAFETY: The caller guarantees that the timer is live.
    unsafe { bindings::rust_helper_hrtimer_debug_activate(timer, mode, was_armed) };

    // SAFETY: The caller holds the CPU base lock.
    let cpu_base = unsafe { (*base).cpu_base };
    // SAFETY: The caller guarantees that the CPU base is live.
    unsafe { bindings::rust_helper_hrtimer_enqueue_online(cpu_base) };

    // SAFETY: The caller holds the CPU base lock.
    let active_bit = 1_u32 << unsafe { (*base).index };
    // SAFETY: The caller holds the CPU base lock.
    unsafe { (*cpu_base).active_bases |= active_bit };
    // SAFETY: The caller holds the CPU base lock.
    unsafe { bindings::rust_helper_hrtimer_set_queued(timer, true) };

    // SAFETY: The caller guarantees the queue and timer belong to this clock base.
    if !unsafe {
        bindings::timerqueue_linked_add(
            ptr::addr_of_mut!((*base).active),
            ptr::addr_of_mut!((*timer).node),
        )
    } {
        return false;
    }

    // SAFETY: The inserted timer is live and is the first queue entry.
    unsafe { (*base).expires_next = (*timer).node.expires };
    true
}

/// Enters the callback-running state and removes the expired timer.
///
/// Rust publishes the running timer, separates the running and queued state
/// sections, removes the timer without clock event reprogramming, and clears
/// the low-resolution relative flag.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. The timer must be queued on `base`.
#[cfg(CONFIG_RUST_HRTIMER_CALLBACK)]
#[export]
unsafe extern "C" fn rust_hrtimer_callback_enter(timer: *mut Hrtimer, base: *mut ClockBase) {
    // SAFETY: The caller holds the clock base lock.
    unsafe { (*base).running = timer };
    // SAFETY: The caller holds the clock base lock.
    unsafe { bindings::rust_helper_hrtimer_callback_barrier(base) };
    // SAFETY: The caller satisfies the requirements of `remove_locked`.
    unsafe { remove_locked(timer, base, false, false) };

    #[cfg(CONFIG_TIME_LOW_RES)]
    // SAFETY: The caller guarantees that the timer is live.
    unsafe {
        (*timer).is_rel = false
    };
}

/// Completes the callback-running state and queues a restarting timer.
///
/// Rust queues the timer when the callback requested a restart and another
/// context did not already queue it. Rust then separates the callback and
/// post-callback state sections, validates the running timer, and clears the
/// running marker.
///
/// # Safety
///
/// `timer` and `base` must be live, and the caller must hold
/// `base->cpu_base->lock`. `base->running` must identify `timer`.
#[cfg(CONFIG_RUST_HRTIMER_CALLBACK)]
#[export]
unsafe extern "C" fn rust_hrtimer_callback_exit(
    timer: *mut Hrtimer,
    base: *mut ClockBase,
    restart: u32,
) {
    // SAFETY: The caller guarantees that the timer is live and locked.
    let queued = unsafe { bindings::rust_helper_hrtimer_is_queued(timer) };

    if restart == bindings::hrtimer_restart::HRTIMER_RESTART.0 && !queued {
        // SAFETY: The callback cannot migrate the running timer.
        unsafe {
            rust_hrtimer_enqueue(timer, base, bindings::hrtimer_mode_HRTIMER_MODE_ABS, false)
        };
    }

    // SAFETY: The caller holds the clock base lock.
    unsafe { bindings::rust_helper_hrtimer_callback_barrier(base) };
    // SAFETY: The caller holds the clock base lock.
    let _ = unsafe { bindings::rust_helper_hrtimer_callback_running_matches(base, timer) };
    // SAFETY: The caller holds the clock base lock.
    unsafe { (*base).running = ptr::null_mut() };
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

/// Reprograms the clock event after timer removal with the Rust force state.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(all(CONFIG_RUST_HRTIMER_CANCEL, CONFIG_RUST_HRTIMER_FORCE_REPROGRAM))]
unsafe fn force_reprogram_after_remove(cpu_base: *mut CpuBase) {
    // SAFETY: The caller holds the CPU base lock.
    unsafe { bindings::rust_hrtimer_force_reprogram(cpu_base, true) };
}

/// Reprograms the clock event after timer removal with the C fallback.
///
/// # Safety
///
/// `cpu_base` must be live and protected by its CPU base lock.
#[cfg(all(CONFIG_RUST_HRTIMER_CANCEL, not(CONFIG_RUST_HRTIMER_FORCE_REPROGRAM)))]
unsafe fn force_reprogram_after_remove(cpu_base: *mut CpuBase) {
    // SAFETY: The helper updates the clock event under the CPU base lock.
    unsafe { bindings::rust_helper_hrtimer_force_reprogram(cpu_base, true) };
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
        // SAFETY: The caller holds the CPU base lock.
        unsafe { force_reprogram_after_remove(cpu_base) };
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

/// Cancels a timer and waits for a running callback to finish.
///
/// This implements the production retry loop in `hrtimer_cancel()`.
///
/// # Safety
///
/// `timer` must be a live timer for the duration of this call.
#[cfg(CONFIG_RUST_HRTIMER_CANCEL_WAIT)]
#[export]
unsafe extern "C" fn rust_hrtimer_cancel(timer: *mut Hrtimer) -> i32 {
    loop {
        // SAFETY: The caller guarantees that the timer is live.
        let result = unsafe { bindings::rust_helper_hrtimer_try_to_cancel(timer) };
        if result >= 0 {
            return result;
        }

        // SAFETY: The caller guarantees that the timer remains live. The helper
        // performs the context-specific wait for the running callback.
        unsafe { bindings::rust_helper_hrtimer_cancel_wait_running(timer) };
    }
}
