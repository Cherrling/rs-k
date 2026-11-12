.. SPDX-License-Identifier: GPL-2.0

Native Rust HRTimer State
=========================

Status and scope
----------------

The ``CONFIG_RUST_HRTIMER`` option enables native Rust state support for
high-resolution timers.  The ``CONFIG_RUST_HRTIMER_START`` option selects
Rust for the production startup state machine.  That state machine converts
the expiry, updates or moves the timer, inserts it, handles deferred rearm,
and selects the clock event action.
``CONFIG_RUST_HRTIMER_USER_EXPIRY`` selects Rust for validating and removing
an already expired user-controlled timer.
``CONFIG_RUST_HRTIMER_SETUP`` selects Rust for timer initialization, clock
base selection, and callback installation.
``CONFIG_RUST_HRTIMER_ENQUEUE`` selects Rust for inserting a timer into its
selected clock base.  The
``CONFIG_RUST_HRTIMER_CALLBACK`` option selects Rust for the state transitions
around an expiry callback.  The
``CONFIG_RUST_HRTIMER_RUN_QUEUES`` option selects Rust for the expired clock
base and timer loops.  ``CONFIG_RUST_HRTIMER_INTERRUPT`` selects Rust for the
high-resolution timer interrupt state machine.  ``CONFIG_RUST_HRTIMER_REARM``
selects Rust for deferred rearm coordination and clock event rearm state.  The
``CONFIG_RUST_HRTIMER_BASE_SELECTION`` option selects Rust for the decision
that accepts or rejects a selected remote clock base.
``CONFIG_RUST_HRTIMER_TARGET_BASE`` selects Rust for the target CPU base
decision.
``CONFIG_RUST_HRTIMER_KEEP_BASE`` selects Rust for deciding whether an online
SMP NOHZ timer can remain on its local clock base.
``CONFIG_RUST_HRTIMER_BASE_MIGRATION`` selects Rust for moving a timer between
per-CPU clock bases.
``CONFIG_RUST_HRTIMER_NEXT_EVENT`` selects Rust for the search across active
hard and soft clock bases.
``CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT`` selects Rust for combining those
results and selecting the event used for reprogramming.
``CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI`` selects Rust for deciding whether a
clock change requires remote timer reevaluation.
``CONFIG_RUST_HRTIMER_REPROGRAM`` selects Rust for deciding whether a newly
queued timer changes the soft-timer caches or requires clock event
reprogramming.
``CONFIG_RUST_HRTIMER_FORCE_REPROGRAM`` selects Rust for reevaluating all clock
bases, applying the force-reprogram equality shortcut, and selecting whether
the shared clock event deadline is updated or programmed.
The ``CONFIG_RUST_HRTIMER_CANCEL`` option extends that state machine to queued
removal and cancellation decisions.  ``CONFIG_RUST_HRTIMER_CANCEL_WAIT``
selects Rust for the cancellation retry loop.
``CONFIG_RUST_HRTIMER_FORWARD`` selects Rust for periodic expiry advancement.
The public hrtimer API and device driver scope remain unchanged.

Rust owns these production decisions:

* Converting hard, soft, RT, and lazy modes, applying relative realtime
  clock mapping, selecting the per-CPU clock base, clearing the timer,
  initializing its queue node, and installing the callback.
* Inserting a timer into an active clock base, publishing the lockless queued
  state, setting the active-clock-base bit, and updating the cached first
  expiry.
* Publishing the callback-running timer, removing it from its queue without
  clock event reprogramming, and queuing an automatic restart.
* Separating the queued, callback-running, and post-callback state sections
  and clearing the callback-running marker.
* Taking a stable active-clock-base snapshot, traversing it in clock base
  order, applying each clock offset, selecting the leftmost soft-expired
  timer, and repeating the per-base loop after each callback.
* Rechecking dropped-lock expiry progress, limiting interrupt retries,
  activating a due softirq, detecting a repeated expired event as a hang,
  updating hang counters, and selecting immediate or deferred rearm.
* Checking for pending deferred rearm, selecting the cached interrupt expiry
  or recomputing the next event after a queue change, updating the CPU base
  deadline, clearing the pending state, applying the hang delay, and
  selecting immediate or deferred clock event programming.
* Reevaluating all clock bases for a forced reprogram, suppressing an equal
  deadline when requested, updating ``expires_next``, and respecting the
  high-resolution and hang-recovery gates before programming the device.
* Enqueueing an inactive timer on its current clock base.
* Updating an active timer in place when its new expiry remains between the
  current neighbors.
* Removing and requeueing an active timer when its new expiry crosses the next
  neighbor.
* Accepting a local target, an offline-CPU fallback, or a remote target whose
  next expiry can meet the timer deadline.
* Selecting an offline housekeeping target, preserving a pinned timer's base,
  or applying NOHZ timer migration.
* Keeping a running callback on its current base and applying migration,
  first-timer, pinned-timer, housekeeping, stopped-tick, and rescheduling
  conditions to local-base retention.
* Migrating a timer between CPU clock bases, keeping a running callback on its
  current base, and retrying on the local CPU when a remote target cannot meet
  the timer deadline.
* Converting relative expiries, applying low-resolution granularity, and
  publishing the low-resolution relative-timer state.
* Selecting local-base retention, then removing and requeueing the timer or
  migrating it before insertion.
* Preserving the previous first-timer and armed state across remove and
  enqueue, handling a deferred rearm, and selecting the final reprogram
  action.
* Waking an offline current CPU's remote target when needed and retaining an
  already armed lazy event when its new expiry is later.
* Converting a user timer's soft expiry to monotonic time, avoiding an
  unnecessary clock sample when it cannot be first, and removing it when it
  is already expired.
* Selecting the earliest active hard or soft clock base and updating the
  corresponding next-timer pointers.
* Combining hard and soft next events, caching the soft expiry, and selecting
  the event used for clock event reprogramming.
* Deciding whether a clock-change sequence requires a remote CPU timer
  interrupt.
* Normalizing the deadline of a newly queued timer, updating hard and soft
  timer caches, and deciding whether clock event reprogramming is required.
* Maintaining the clock base's cached first expiry.
* Maintaining the CPU base's active-clock-base bit.
* Publishing the lockless ``is_queued`` state.
* Removing queued timers and clearing an empty active clock base.
* Returning the inactive, queued, and callback-running cancellation results.
* Waiting and retrying cancellation until the timer is inactive.
* Calculating overruns and advancing inactive timer expiries.

The existing timer queue and red-black tree operations perform ordered linked
tree insertion and erasure.

Entry points and state ownership
--------------------------------

The C ``hrtimer_interrupt()`` path calls ``rust_hrtimer_interrupt()`` when the
interrupt option is enabled.  The original C clock-base update, retry loop,
softirq-deadline decision, hang detection, counters, and final rearm selection
are compiled out.  C keeps the timer device counters, CPU base lock, and
interrupt entry and exit.

The Rust kernel build currently exports every non-inline symbol from the
``kernel`` crate with ``EXPORT_SYMBOL_GPL``.  The interrupt entry, rearm
coordinator, and device test hooks therefore follow this existing build-wide
policy.  Narrowing their module visibility requires a separate Rust build
change and proof that all required code generation remains available.  Review
this boundary when the Rust compiler provides a stable internal-linkage
mechanism or the kernel changes its symbol export policy.

The C ``__hrtimer_rearm_deferred()`` path first calls
``rust_hrtimer_rearm_pending()``.  When deferred work is pending, C takes the
CPU base lock and calls ``rust_hrtimer_rearm_deferred_locked()``.  Rust chooses
between the interrupt's cached expiry and a fresh clock and next-event update.
It then updates ``expires_next``, clears ``deferred_rearm``, applies any hang
delay, and calls the narrow clock event programming adapter.  The original C
deferred rearm and clock event rearm state sequences are compiled out.

The C ``__hrtimer_setup()`` path calls ``rust_hrtimer_setup()`` when the
initialization option is enabled.  Rust clears the timer, converts its mode,
maps the clock ID to a hard or soft clock base, initializes the linked queue
node, and installs the callback.  C retains debug object initialization,
tracepoints, missing-callback and invalid-clock diagnostics, the current CPU
base lookup, and the public API adapters.

The C ``hrtimer_start_range_ns_common()`` path calls
``rust_hrtimer_start_range_ns()`` when the startup option is enabled.  The
original C ``__hrtimer_start_range_ns()`` state machine is compiled out.  Both
the normal and user start entry points use this common path.

Rust owns relative expiry conversion, low-resolution adjustment, local-base
retention, removal and same-base requeue ordering, base migration, insertion,
deferred-rearm recording, remote wake selection, lazy-event retention, and the
final ``NONE``, ``REPROGRAM``, or ``FORCE`` action.  Production startup
returns only those three actions to C.  Rust performs a remote asynchronous
wakeup or deferred-rearm update before returning when required.

The C ``hrtimer_check_user_timer()`` path calls
``rust_hrtimer_check_user_timer()`` when the user-expiry option is enabled.
The original C soft-expiry conversion, next-timer comparison, future-time
validation, and expired-removal sequence are compiled out.  C emits the
existing start-expired tracepoint after Rust reports that the timer was
removed.

The C ``remove_and_enqueue_same_base()`` path calls
``rust_hrtimer_requeue_same_base()`` when the Rust option is enabled.  The
original C neighbor decision and update sequence are compiled out.

The C ``enqueue_hrtimer()`` path calls ``rust_hrtimer_enqueue()`` when the
insertion option is enabled.  The original C active-base accounting,
queued-state publication, linked insertion, and first-expiry cache update are
compiled out.

The C ``__run_hrtimer()`` path calls ``rust_hrtimer_callback_enter()`` and
``rust_hrtimer_callback_exit()`` when the callback option is enabled.  The
original C running-state publication, queue removal, time-low-res cleanup,
restart decision, automatic enqueue, sequence barriers, and running-state
clearing are compiled out.  C retains lock dropping and reacquisition, the
callback invocation, tracing, and lockdep context tracking.

The C ``__hrtimer_run_queues()`` path calls
``rust_hrtimer_run_queues()`` when the expiry queue option is enabled.  The
original C active-base snapshot, clock-base traversal, clock offset
application, soft-expiry comparison, and timer loop are compiled out.  Rust
calls a narrow C helper for each selected timer.  That helper invokes
``__run_hrtimer()`` and performs the softirq waiter synchronization.

The C ``__remove_hrtimer()`` path calls ``rust_hrtimer_remove()`` when the
cancellation option is enabled.  The C ``hrtimer_try_to_cancel()`` path calls
``rust_hrtimer_try_to_cancel_locked()`` after acquiring the timer base lock.
The original C removal and cancellation decision sequences are compiled out.

The C ``hrtimer_suitable_target()`` path calls
``rust_hrtimer_suitable_target()`` when the base-selection option is enabled.
The original C suitability decision is compiled out.

The C ``get_target_base()`` path calls ``rust_hrtimer_target_base()`` when the
target-selection option is enabled.  The original C offline, pinned, and NOHZ
decision sequence is compiled out.  C supplies CPU masks, housekeeping policy,
and per-CPU base addresses.

The C ``hrtimer_keep_base()`` path calls ``rust_hrtimer_keep_base()`` when the
local-base retention option is enabled.  The original C callback-running,
migration, first-timer, pinned-timer, housekeeping, stopped-tick, and
rescheduling decision sequence is compiled out.  C supplies read-only
migration, housekeeping, tick, and scheduling state.

The C ``switch_hrtimer_base()`` path calls ``rust_hrtimer_switch_base()``
when the base-migration option is enabled.  The original C migration loop,
callback-running check, suitability retry, and migration commit are compiled
out.  Narrow C helpers select the per-CPU target and perform the raw old-to-new
CPU base lock handoff.

The C ``__hrtimer_get_next_event()`` path calls
``rust_hrtimer_get_next_event()`` when the next-event option is enabled. The
original C active-base search and pointer updates are compiled out.

The C ``hrtimer_update_next_event()`` path calls
``rust_hrtimer_update_next_event()`` when the update option is enabled. The
original C hard/soft merge and cached soft-expiry update are compiled out.

After updating clock offsets, the C ``update_needs_ipi()`` path calls
``rust_hrtimer_update_needs_ipi()`` when the IPI decision option is enabled.
The original C sequence, deferred-rearm, and active-base checks are compiled
out.

The C ``hrtimer_reprogram()`` path calls ``rust_hrtimer_reprogram()`` when the
reprogramming option is enabled.  The original C soft-timer cache updates,
remote-CPU check, deadline comparison, deferred-rearm check, and
``next_timer`` update are compiled out.

The C ``hrtimer_force_reprogram()`` path calls
``rust_hrtimer_force_reprogram()`` when the force-reprogram option is enabled.
Rust reevaluates the next event through a narrow C helper, applies the
skip-equal rule, updates ``expires_next``, and checks ``hres_active`` and
``hang_detected`` before programming the clock event.  The C
``__hrtimer_reprogram()`` adapter also calls
``rust_hrtimer_reprogram_device()``, so direct reprogram calls share the Rust
device state before reaching the hardware adapter.

The C ``hrtimer_cancel()`` entry point calls ``rust_hrtimer_cancel()`` when
the cancellation-wait option is enabled.  The original C retry loop is
compiled out.

The C ``hrtimer_forward()`` entry point calls ``rust_hrtimer_forward()`` when
the forwarding option is enabled.  The original C overrun and expiry decision
sequence is compiled out.

Rust uses the existing ``struct hrtimer``, ``struct hrtimer_clock_base``, and
``struct hrtimer_cpu_base`` layouts.  It does not call the original C
``hrtimer_can_update_in_place()`` decision or duplicate the C removal state
machine.

Retained C boundary
-------------------

The following operations remain in C:

* Acquiring and releasing the CPU base lock and asserting lock ownership.
* The current CPU base lookup, relative clock sampling, and the soft or hard
  mode assertion in the public startup common path.
* The lockless inactive fast path in ``hrtimer_try_to_cancel()``.
* Debug object activation and deactivation.
* Debug object setup, setup tracing, missing-callback warnings, invalid clock
  ID warnings, and the current CPU base pointer used during initialization.
* HRTimer tracepoint delivery.
* The start-expired tracepoint adapter and the C removal fallback used when
  the cancellation state machine is independently disabled.
* Clock event reprogramming and the asynchronous remote CPU wakeup
  primitive.
* Clock offset updates and clock-was-set sequence advancement.
* CPU mask and housekeeping policy, NOHZ target computation, per-CPU address
  lookup, and target-online checks.
* The migration static branch, housekeeping CPU query, stopped-tick state, and
  current task rescheduling state used by local-base retention.
* Raw CPU base lock handoff, migration-base publication, and callback
  execution.
* Expired callback invocation, lock dropping and reacquisition, tracepoints,
  lockdep context tracking, and softirq waiter synchronization.
* Clock sampling and offset updates, clock event tracing and programming,
  current-CPU base lookup, deferred-rearm thread-flag publication and
  clearing, interrupt-context locking, and raw spinlock acquisition.
* The lockless try-to-cancel entry point and PREEMPT_RT callback wait
  primitive with its expiry-lock synchronization.
* Checked ktime division, saturated expiry arithmetic, and range
  initialization.
* The trivial local-base retention decision in non-SMP and non-NOHZ
  configurations.
* Narrow fallbacks for startup when insertion, cancellation, local-base
  retention, or base migration are independently disabled.
* Public hrtimer ABI adapters.

Thin C helpers preserve ``READ_ONCE()`` and ``WRITE_ONCE()`` semantics for
``timer->is_queued`` and deliver debug and trace hooks.  They do not decide
queue state transitions.

Important invariants
--------------------

All queue state transitions are serialized by ``cpu_base->lock``.  The
lockless queued snapshot pairs its read with the Rust-controlled write.

Initialization clears the complete timer before publishing any state.  A
relative realtime timer maps to the monotonic clock base.  On PREEMPT_RT, a
timer without explicit hard mode uses the soft clock base.  The selected
clock base covers the hard or soft half of the current CPU base's clock array.
An initialized queue node has a self-parent color value and null neighbor
links.

Startup samples the current clock before converting a relative expiry.  The
conversion uses saturated ktime arithmetic.  A low-resolution system records
whether the timer was relative and adds one timer resolution only for a
relative timer.

Startup snapshots whether the timer was first before it changes the queue.
If local retention is selected, Rust updates or removes and requeues the timer
on the same clock base.  Otherwise, Rust preserves the armed state, sets the
new expiry range, migrates the timer, and inserts it on the selected base.

A pending deferred rearm has priority over every clock event action.  Startup
sets ``deferred_needs_update`` and returns no reprogram action.  Without a
deferred rearm, an online current CPU requests reprogramming only when the
new timer is first.  An offline current CPU wakes the remote target only when
the new timer is first.

When the timer was first and remains on the same CPU base, a lazy timer with
an expiry no earlier than ``expires_next`` keeps the existing armed event.
Otherwise, startup requests a forced reprogram because removal suppressed the
normal reprogram.

User-expiry validation uses ``_softexpires`` instead of the slack-adjusted
hard expiry.  A timer whose converted soft expiry is not earlier than
``expires_next`` is accepted without sampling the current time.  Otherwise,
the timer is expired when its soft expiry is less than or equal to the current
time.  Expired removal publishes the inactive queued state and performs no
clock event reprogramming.

An in-place update is valid only when the new expiry does not pass the next
neighbor.  A first timer may move later within that bound.  A non-first timer
must not move ahead of its previous neighbor.

Inserting a timer publishes its queued state before linked insertion.  The
clock base's active bit remains set after a non-first insertion.  The cached
first expiry changes only when the inserted timer becomes the leftmost entry.

An expiry callback marks its timer as running before removing it from the
queue.  The callback can rearm the timer and queue it concurrently after the
CPU base lock is dropped.  A requested restart queues the timer only when the
callback did not already do so.  The running marker remains set until after
the restart state is published and is the last state cleared.

When the first timer is removed and requeued, the clock base's cached expiry
must be recomputed from the new linked-tree leftmost entry.  The CPU base
active bit remains set across a same-base dequeue and enqueue.

The expired queue loop snapshots active clock bases at entry.  It visits those
bases in index order and processes the leftmost timer while its soft expiry is
less than or equal to the base-local time.  A base is processed until its
first timer is not expired.  A clock base that becomes active during a callback
is not added to the current snapshot.

A high-resolution interrupt publishes a pending rearm before running hard
timers.  A softirq deadline at or before the sampled time activates the
softirq and clears its cached deadline.  After hard-timer execution, Rust
updates the clock and next event.  An expired next event permits two more
passes.  A third expired observation records the elapsed hang time, increments
the hang count, sets the hang state, and rearms with a delay from zero to 100
milliseconds.

With immediate rearm, Rust updates ``expires_next`` and clears the pending
rearm before programming the device.  A detected hang replaces the programmed
deadline with the capped delay.  With deferred rearm, Rust caches the selected
expiry, marks it unchanged, and requests thread-work execution.

Force reprogram reevaluates all clock bases even when no timer queue changed.
The equal-deadline shortcut suppresses both the CPU base update and hardware
programming.  A different deadline is always stored in ``expires_next``.
Hardware programming occurs only when high-resolution mode is active and no
hang recovery delay remains in effect.

Deferred rearm consumption uses the cached expiry when no timer was added or
removed.  A pending update flag requires Rust to refresh clock offsets and
search the active bases before programming the device.  Consumption updates
the CPU base deadline and clears the pending marker before calling the clock
event adapter.

A queued timer returns 1 from ``hrtimer_try_to_cancel()``.  A timer whose
callback is running returns -1.  An inactive timer returns 0.  Removing the
last timer in a clock base clears its active-base bit.

An offline timer base selects an online housekeeping CPU.  An online unpinned
timer can use the NOHZ timer target only when timer migration is enabled.  A
pinned timer remains on its current CPU base.

A timer whose callback is running keeps its current base.  With migration
disabled, local retention is accepted only for a local base.  With migration
enabled, a local first or pinned timer stays local.  Other local timers leave
a non-housekeeping CPU and leave a housekeeping CPU only when its tick has
stopped and rescheduling is not pending.

Base migration publishes the special migration base before dropping the old
CPU base lock.  The timer remains locked while Rust acquires the target CPU
base lock.  A running callback keeps the timer on its current base.  An
unsuitable remote target causes a lock rollback to the original base and a
retry on the current CPU.  A suitable target publishes the new clock base
before the caller enqueues the timer.

``hrtimer_cancel()`` retries after -1 until the callback completes and the
timer becomes inactive.  It returns 1 when it removes a queued timer and 0
when the timer was already inactive.

Removal can retain the queued state for immediate requeue.  Cancellation
publishes the inactive state.  The queued-state write still pairs with the
lockless snapshot in ``hrtimer_is_queued()``.

Forwarding a timer that is already past ``now`` returns 0 without changing
the expiry.  The interval is clamped to the timer resolution.  Exact interval
alignment advances one additional interval so that the result is strictly in
the future.  A queued timer produces the existing queue-state warning.

Next-event selection searches only the active clock bases selected by the
hard or soft mask.  A pending softirq suppresses the soft search.  The result
is the minimum normalized expiry across the searched bases and is clamped to
zero.  When hard and soft expiries are equal, the soft result remains selected.

Next-event updates refresh ``softirq_expires_next`` unless a softirq is
already active.  The hard result becomes the programming event unless a soft
event is earlier.  An equal hard and soft deadline keeps the hard timer
selected, while an earlier soft deadline copies ``softirq_next_timer`` to
``next_timer``.

Remote IPI decisions require a changed clock-was-set sequence.  A pending
deferred rearm records that the remote CPU must reevaluate and suppresses the
IPI.  Otherwise, an affected hard event before ``expires_next`` requires an
IPI.  A soft event can also require one through ``softirq_expires_next``
unless the softirq is already active.

Reprogramming decisions normalize ``timer->node.expires`` by subtracting the
clock base offset and clamping a negative result to zero.  A soft timer first
updates its timer CPU base's soft-timer cache.  The current CPU is programmed
only when the normalized deadline is strictly earlier than ``expires_next``,
the timer belongs to the current CPU base, and no deferred rearm is pending.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Repeated four-vCPU Rust KUnit runs.
* Repeated four-vCPU C startup fallback runs with
  ``CONFIG_RUST_HRTIMER_START`` disabled.
* Repeated four-vCPU C startup fallback runs with Rust user expiry enabled.
* Repeated four-vCPU C interrupt fallback runs with
  ``CONFIG_RUST_HRTIMER_INTERRUPT`` disabled.
* Repeated four-vCPU interrupt runs with
  ``CONFIG_RUST_HRTIMER_RUN_QUEUES`` and
  ``CONFIG_RUST_HRTIMER_NEXT_EVENT`` disabled.  Disabling next-event selection
  also disables the dependent ``CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT`` and
  ``CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI`` options.
* Repeated four-vCPU startup runs with insertion, cancellation, local-base
  retention, and base migration independently disabled.
* Repeated four-vCPU Rust user-expiry runs and C fallback runs with
  ``CONFIG_RUST_HRTIMER_USER_EXPIRY`` disabled.
* User-expiry runs with the Rust cancellation state machine independently
  disabled.
* A four-vCPU C base-selection fallback KUnit run.
* A four-vCPU C target-selection fallback KUnit run.
* A four-vCPU C local-base retention fallback KUnit run.
* A four-vCPU run with target selection disabled while local-base retention
  remains enabled.
* A four-vCPU C expiry queue-selection fallback KUnit run.
* A four-vCPU run with Rust callback and next-event paths disabled while
  expiry queue selection remains enabled.
* A four-vCPU C base-migration fallback KUnit run.
* A four-vCPU C next-event fallback KUnit run.
* A four-vCPU C next-event update fallback KUnit run.
* A four-vCPU C IPI-decision fallback KUnit run.
* A four-vCPU C cancellation-wait fallback KUnit run.
* Repeated four-vCPU generic inline KASAN and lockdep KUnit runs.
* Repeated four-vCPU PREEMPT_RT KUnit runs with the Rust implementation
  enabled.
* Repeated four-vCPU runs with ``CONFIG_RUST_RBTREE`` disabled.
* Absolute and relative expiry conversion, same-base startup, migration
  startup, deferred rearm, local and remote first timers, offline current CPU,
  lazy armed-event retention, and forced reprogram decisions.
* User-expiry offset conversion, next-timer short-circuiting, future and
  exactly expired deadlines, expired removal, and a successful restart.
* Local, offline-CPU, earlier remote, equal-deadline, and later remote base
  selection decisions.
* Online target retention, NOHZ migration, pinned target retention, and
  offline housekeeping fallback decisions.
* Successful base migration, callback-running base retention, and remote
  target rollback when the local CPU must own an earlier timer.
* Callback-running retention, disabled migration, remote rejection,
  first-timer retention, pinned retention, non-housekeeping rejection, tick
  activity, stopped tick, and pending rescheduling decisions.
* Hard, soft, combined, suppressed-softirq, equal-expiry, empty, offset, and
  negative next-event selection decisions.
* Soft-first, hard-first, equal-expiry, suppressed-softirq, and empty-event
  update decisions.
* Unchanged and changed clock sequences, hard and soft IPI decisions,
  deferred-rearm recording, offsets, active filters, and softirq suppression.
* Normalized hard and soft reprogramming deadlines, strict deadline
  comparisons, remote timer rejection, deferred rearm, suppressed softirq,
  and local and remote soft-timer cache updates.
* First and non-first linked insertions, insertion before the current first
  timer, active-base accounting, queued-state publication, and first-expiry
  cache maintenance.
* Callback entry state, queue removal, automatic restart, callback-side
  rearming, no-restart completion, active-base accounting, sequence barriers,
  and running-state clearing.
* Empty, future, equal-expiry, past-expiry, and next-entry queue selection.
* Real hard and soft hrtimer delivery through the Rust expiry loop, including
  softirq waiter synchronization.
* Future, equal, and expired interrupt rechecks, the two-retry limit, third
  observation hang detection, softirq deadline equality, and hang-delay
  capping.
* In-place expiry updates and requeues that cross the next neighbor.
* Queued cancellation, callback-running rejection, and inactive cancellation.
* Cancellation retry after a callback-running rejection.
* Forwarding a future timer, partial overruns, and exact interval alignment.
* Clock-base expiry cache maintenance and real callback delivery.
* Four-vCPU runs with the cancellation wait and forwarding fallbacks disabled
  independently.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The hrtimer startup, user-expiry, queue, base-selection, target-selection,
base-migration, next-event, reprogramming, insertion, callback lifecycle,
cancellation, and forwarding state was validated on 2026-11-11 and 2026-11-12
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust user-expiry runs passed all nineteen KUnit
  cases.
* Three consecutive four-vCPU C user-expiry fallback runs passed all eighteen
  applicable cases with ``CONFIG_RUST_HRTIMER_USER_EXPIRY`` disabled.
* Three consecutive four-vCPU Rust user-expiry runs passed all eighteen
  applicable cases with ``CONFIG_RUST_HRTIMER_CANCEL`` disabled.
* Three consecutive four-vCPU runs with C startup fallback and Rust user
  expiry enabled passed all eighteen applicable cases.
* Three consecutive four-vCPU Rust startup runs passed all eighteen KUnit
  cases.
* Three consecutive four-vCPU C startup fallback runs passed all seventeen
  applicable cases with ``CONFIG_RUST_HRTIMER_START`` disabled.
* Three consecutive four-vCPU Rust startup runs passed all fourteen applicable
  cases with insertion, cancellation, local-base retention, and base migration
  disabled.
* Three consecutive four-vCPU Rust runs passed all nine KUnit cases.
* Three consecutive four-vCPU Rust reprogramming runs passed all ten KUnit
  cases.
* Three consecutive four-vCPU Rust insertion runs passed all eleven KUnit
  cases.
* Three consecutive four-vCPU Rust callback-lifecycle runs passed all twelve
  KUnit cases.
* Three consecutive four-vCPU Rust base-migration runs passed all thirteen
  KUnit cases.
* Three consecutive four-vCPU Rust target-selection runs passed all fourteen
  KUnit cases.
* Three consecutive four-vCPU Rust local-base retention runs passed all
  fifteen KUnit cases.
* Three consecutive four-vCPU Rust expiry queue-selection runs passed all
  seventeen KUnit cases.
* The four-vCPU C base-selection fallback passed all five applicable cases
  with ``CONFIG_RUST_HRTIMER_BASE_SELECTION`` disabled.
* Three consecutive four-vCPU C target-selection fallback runs passed all
  thirteen applicable cases with ``CONFIG_RUST_HRTIMER_TARGET_BASE``
  disabled.
* Three consecutive four-vCPU C local-base retention fallback runs passed all
  fourteen applicable cases with ``CONFIG_RUST_HRTIMER_KEEP_BASE`` disabled.
* Three consecutive four-vCPU runs with
  ``CONFIG_RUST_HRTIMER_TARGET_BASE`` disabled and
  ``CONFIG_RUST_HRTIMER_KEEP_BASE`` enabled passed all fourteen applicable
  cases.
* Three consecutive four-vCPU C expiry queue-selection fallback runs passed
  all fifteen applicable cases with
  ``CONFIG_RUST_HRTIMER_RUN_QUEUES`` disabled.
* Three consecutive four-vCPU runs with
  ``CONFIG_RUST_HRTIMER_CALLBACK`` and
  ``CONFIG_RUST_HRTIMER_NEXT_EVENT`` disabled passed all thirteen applicable
  cases while expiry queue selection remained enabled.
* Three consecutive four-vCPU C base-migration fallback runs passed all
  twelve applicable cases with ``CONFIG_RUST_HRTIMER_BASE_MIGRATION``
  disabled.
* Three consecutive four-vCPU C next-event fallback runs passed all six
  applicable cases with ``CONFIG_RUST_HRTIMER_NEXT_EVENT`` disabled.
* Three consecutive four-vCPU C next-event update fallback runs passed all
  seven applicable cases with
  ``CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT`` disabled.
* Three consecutive four-vCPU C IPI-decision fallback runs passed all eight
  applicable cases with ``CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI`` disabled.
* Three consecutive four-vCPU C reprogramming fallback runs passed all nine
  applicable cases with ``CONFIG_RUST_HRTIMER_REPROGRAM`` disabled.
* Three consecutive four-vCPU C insertion fallback runs passed all ten
  applicable cases with ``CONFIG_RUST_HRTIMER_ENQUEUE`` disabled.
* The four-vCPU C cancellation-wait fallback passed all five cases with
  ``CONFIG_RUST_HRTIMER_CANCEL_WAIT`` disabled.
* The four-vCPU C forwarding fallback passed all five cases with
  ``CONFIG_RUST_HRTIMER_FORWARD`` disabled.
* Three consecutive four-vCPU runs on the final integrated Rust image passed
  all twenty cases.
* Three consecutive four-vCPU runs on the final generic inline KASAN image
  passed all twenty cases.
* Three consecutive four-vCPU runs on the final lockdep image passed all
  twenty cases.
* Three consecutive four-vCPU runs on the final PREEMPT_RT image passed all
  twenty cases.
* Three consecutive four-vCPU images with ``CONFIG_RUST_RBTREE`` disabled
  passed all twenty cases.
* Three consecutive four-vCPU C interrupt fallback runs passed all nineteen
  applicable cases with ``CONFIG_RUST_HRTIMER_INTERRUPT`` disabled.
* Three consecutive four-vCPU interrupt runs passed all fifteen applicable
  cases with ``CONFIG_RUST_HRTIMER_RUN_QUEUES`` and
  ``CONFIG_RUST_HRTIMER_NEXT_EVENT`` disabled.  Kconfig disabled the dependent
  next-event update and IPI options.
* The user-expiry cases covered soft-expiry offset conversion, the next-timer
  short-circuit, a future candidate, equality with the current time,
  expired removal, and a successful restart.
* The startup cases covered absolute and relative expiry preparation,
  same-base startup, migration startup, deferred rearm, local and remote first
  timers, offline current CPU, lazy armed-event retention, and forced
  reprogram selection.
* The cases covered local, offline-CPU, earlier remote, equal-deadline, and
  later remote base selection, hard, soft, combined, suppressed-softirq,
  equal-expiry, empty, offset, and negative next-event selection,
  soft-first, hard-first, equal-expiry, suppressed-softirq, and empty-event
  updates, unchanged and changed clock sequences, hard and soft IPI
  decisions, deferred-rearm recording, offset and active-mask filtering,
  normalized hard and soft reprogramming deadlines, strict deadline
  comparisons, remote timer rejection, suppressed softirq, deferred rearm,
  and local and remote soft-timer cache updates,
  first and non-first linked insertions, insertion before the current first
  timer, active-base accounting, queued-state publication, and first-expiry
  cache maintenance,
  callback-running publication, queue removal, automatic restart,
  callback-side rearming, no-restart completion, sequence barriers, and
  running-state clearing,
  empty, future, equal-expiry, past-expiry, and next-entry timer selection,
  real hard and soft hrtimer callback delivery, and softirq waiter
  synchronization,
  online target retention, NOHZ migration, pinned target retention, and
  offline housekeeping fallback,
  callback-running retention, disabled migration, remote rejection,
  first-timer retention, pinned retention, non-housekeeping rejection, active
  tick retention, stopped-tick migration, and pending-rescheduling retention,
  successful base migration, callback-running base retention, remote target
  rollback, and lock-safe migration completion,
  same-base in-place updates, requeues that cross the next timer, cached
  first-expiry maintenance, callback-running cancellation rejection, queued
  cancellation, inactive cancellation, a real expiry callback, partial
  forwarding overruns, exact interval alignment, and resolution clamping.
* No run reported a ``BUG``, warning, KASAN report, lock dependence
  diagnosis, or panic.
* The high-resolution interrupt path calls ``rust_hrtimer_interrupt()``. The
  hrtimer expiry path calls ``rust_hrtimer_run_queues()``. Its selected timer
  path calls ``rust_helper_hrtimer_run_expired()``, which invokes
  ``__run_hrtimer()``. The hrtimer insertion path calls
  ``rust_hrtimer_enqueue()``. The hrtimer start path calls
  ``rust_hrtimer_start_range_ns()``. Its same-base path calls
  ``rust_hrtimer_requeue_same_base()``. The base-selection path calls
  ``rust_hrtimer_suitable_target()``. The callback path calls
  ``rust_hrtimer_callback_enter()`` and ``rust_hrtimer_callback_exit()``.
  The target-selection path calls ``rust_hrtimer_target_base()``.
  The local-base retention path calls ``rust_hrtimer_keep_base()``.
  The base-migration path calls ``rust_hrtimer_switch_base()``. The
  next-event path calls ``rust_hrtimer_get_next_event()``. The
  next-event update path calls ``rust_hrtimer_update_next_event()``. The
  IPI decision path calls ``rust_hrtimer_update_needs_ipi()``. The
  reprogramming path calls ``rust_hrtimer_reprogram()``. The
  removal and cancellation paths call ``rust_hrtimer_remove()`` and
  ``rust_hrtimer_try_to_cancel_locked()``. The cancellation wait path calls
  ``rust_hrtimer_cancel()``. The forwarding path calls
  ``rust_hrtimer_forward()``. The user-expiry path calls
  ``rust_hrtimer_check_user_timer()``. The linked vmlinux resolves these
  functions and the base-selection, target-selection, base-retention,
  base-migration,
  callback, expiry-selection, and cancellation helpers. The call paths
  resolve from ``hrtimer_start_range_ns_common()`` to
  ``rust_hrtimer_start_range_ns()``, from ``hrtimer_keep_base()`` to
  ``rust_hrtimer_keep_base()``, from ``hrtimer_start_range_ns_user()`` to
  ``rust_hrtimer_check_user_timer()``, and from ``__hrtimer_run_queues()``
  to ``rust_hrtimer_run_queues()``. It contains no unresolved hrtimer
  migration symbols. No module references these internal symbols.
* Rustfmt and Clippy pass. Clippy uses the kernel trailing ``-D warnings``
  policy. Strict checkpatch reports no errors, warnings, or checks.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* CPU base locking or the lockless queued-state protocol changes.
* Public startup mode validation, relative clock sampling, low-resolution
  granularity, or deferred-rearm ownership changes.
* Remove and requeue ordering, first-timer snapshots, lazy-event retention,
  or final reprogram action semantics change.
* User soft-expiry ownership, the strict current-time comparison, or the
  no-reprogram expired-removal rule changes.
* The PREEMPT_RT callback wait primitive or expiry-lock protocol changes.
* Timer queue linked ordering or first-entry maintenance changes.
* The stable active-base snapshot, clock-base order, offset application, or
  soft-expiry comparison changes.
* Active-base mask or pending-softirq next-event semantics change.
* Hard/soft merge precedence or cached soft-expiry ownership changes.
* Clock-was-set sequence or deferred-rearm protocol changes.
* Soft-timer cache ownership, reprogramming deadline normalization, or strict
  hard/soft event precedence changes.
* Clock event reprogramming becomes a Rust-owned primitive.
* HRTimer debug or trace hooks gain state-changing behavior.
* Raw CPU base lock handoff, CPU mask policy, housekeeping policy, or NOHZ
  target computation changes.
* Migration static-branch policy, housekeeping CPU classification, NOHZ
  stopped-tick state, or rescheduling semantics change.
