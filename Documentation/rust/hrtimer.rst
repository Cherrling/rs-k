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
``CONFIG_RUST_HRTIMER_ACTIVE`` selects Rust for the lockless active-state
query and its seqcount retry loop.
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
removal and cancellation decisions.  ``CONFIG_RUST_HRTIMER_TRY_TO_CANCEL``
selects Rust for the lockless entry and lock handoff around those decisions.
``CONFIG_RUST_HRTIMER_CANCEL_WAIT`` selects Rust for the cancellation retry
loop.
``CONFIG_RUST_HRTIMER_FORWARD`` selects Rust for periodic expiry advancement.
``CONFIG_RUST_HRTIMER_ENTRY`` selects Rust for the production public entry
points. The public C ABI remains unchanged, and the complete C entry-point
implementation remains available when this option is disabled.

Rust owns these production decisions:

* Converting hard, soft, RT, and lazy modes, applying relative realtime
  clock mapping, selecting the per-CPU clock base, clearing the timer,
  initializing its queue node, and installing the callback.
* Classifying a timer as queued, callback-running, inactive, or requiring a
  retry after a seqcount or clock-base change.
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
* Returning inactive cancellation without taking the clock base lock,
  requesting the lock for an active timer, and delivering the trace event
  before unlocking after queued removal.
* Waiting and retrying cancellation until the timer is inactive.
* Calculating overruns and advancing inactive timer expiries.

The existing timer queue and red-black tree operations perform ordered linked
tree insertion and erasure.

Direct entry points and state ownership
---------------------------------------

``CONFIG_RUST_HRTIMER_ENTRY`` requires every hrtimer state-machine option.
When it is enabled, Rust directly defines these public GPL entry points:

* ``hrtimer_setup()`` and ``hrtimer_setup_on_stack()``.
* ``hrtimer_update_function()``, ``hrtimer_active()``, and
  ``hrtimer_forward()``.
* ``hrtimer_start_range_ns()`` and ``hrtimer_start_range_ns_user()``.
* ``hrtimer_try_to_cancel()`` and ``hrtimer_cancel()``.
* ``__hrtimer_get_remaining()`` and ``hrtimer_cb_get_time()``.
* ``hrtimer_setup_sleeper_on_stack()`` and
  ``hrtimer_sleeper_start_expires()``.

Rust also directly defines the unexported ``hrtimer_get_next_event()``,
``hrtimer_next_event_without()``, ``hrtimer_interrupt()``,
``hrtimer_run_queues()``, and ``__hrtimer_rearm_deferred()`` entries.
``destroy_hrtimer_on_stack()`` is defined in Rust when debug objects are
enabled. ``hrtimer_cancel_wait_running()`` is defined in Rust on PREEMPT_RT.
The native C object does not define these entries.

Rust owns public entry sequencing, lockless checks, lock handoff, error and
warning decisions, state transitions, event selection, and return values.
C and assembly retain architecture and shared boundaries: raw locks, debug
objects, tracepoints, clock sampling, opaque clock event device fields, task
state, callback invocation, timekeeping bridges, CPU hotplug, and sleep
entry points. Direct calls from Rust into other production hrtimer entries
remain in Rust.

The interrupt entry reads the current CPU base and preserves the original
fatal high-resolution prerequisite in Rust. Rust increments the timer event
counter, clears the opaque device fields through a narrow C helper, acquires
the CPU base lock, runs the Rust interrupt state machine, and releases the
lock. The C clock-event driver calls this Rust symbol directly.

When deferred rearm is enabled, Rust defines ``__hrtimer_rearm_deferred()``.
Rust checks the CPU-local pending flag, acquires the CPU base lock without
changing the caller's interrupt state, selects the cached or recomputed
expiry, updates the deadline, clears the pending state, applies the hang
delay, and selects immediate or deferred device programming. The original C
definition is compiled out.

Timer setup calls the C debug and trace boundary before Rust clears the timer,
selects the clock base, initializes the linked queue node, and installs the
callback. Sleeper setup passes the caller's original mode to the debug trace
before applying the PREEMPT_RT delivery policy. Sleeper startup uses the
original soft and hard expiry difference and clears the task and current
state when the timer is already expired.

Startup and user start acquire the current timer base, run the Rust state
machine, retain the returned base lock, select the Rust reprogram path, and
release that lock. User start validates an expired timer before reprogramming
and delivers the existing trace after removal. Cancellation performs the
lockless inactive check, requests the timer base lock, runs the locked
cancellation decision, requests trace delivery, and releases the lock.

The next-event and queue entries run their complete Rust searches and loops.
The run-queue entry checks high-resolution mode, requests a possible tick
transition, acquires the CPU base lock, activates a due softirq, executes the
Rust hard-timer loop, and releases the lock. The low-resolution next-event
entry returns ``KTIME_MAX`` before taking the lock when high-resolution mode
is active.

Rust uses the existing ``struct hrtimer``, ``struct hrtimer_clock_base``, and
``struct hrtimer_cpu_base`` layouts. Partial Rust configurations retain the C
public wrappers around the selected Rust state machines. The complete C
implementation remains when ``CONFIG_RUST_HRTIMER_ENTRY`` is disabled.

Retained C boundary
-------------------

The following operations remain in C:

* Acquiring and releasing the CPU base lock, preserving the caller's
  interrupt state for deferred rearm, and asserting lock ownership.
* The current CPU base lookup, relative clock sampling, and the soft or hard
  mode warning in the public startup path.
* Raw ``READ_ONCE`` access, seqcount acquire, seqcount retry, and clock-base
  reload operations used by the Rust active-state loop.
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
  clearing, and clearing fields in the opaque clock event device.
* The timer base lock acquisition and release helpers, tracepoint delivery,
  and PREEMPT_RT callback wait primitive with its expiry-lock
  synchronization.
* Checked ktime division, saturated expiry arithmetic, and range
  initialization.
* The trivial local-base retention decision in non-SMP and non-NOHZ
  configurations.
* Narrow fallbacks for startup when insertion, cancellation, local-base
  retention, or base migration are independently disabled.
* The complete C entry-point implementation used when
  ``CONFIG_RUST_HRTIMER_ENTRY`` is disabled.

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

A timer is active when its queued state is set or its clock base currently
identifies it as the running callback. An active observation returns
immediately even if a write race follows because a false negative is
forbidden. An inactive observation retries when the clock base sequence
changed or the timer moved to a different base.

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

An inactive timer returns without acquiring its clock base lock. An active
observation requests the lock once, then repeats the queued and
callback-running decision under that lock. A removed timer requests trace
delivery before the lock is released.

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

* Native Rust, Rust state with C public wrappers, and C-only x86_64 builds.
* Native, fallback, C-only, KASAN, lockdep, debug-object, and PREEMPT_RT
  hrtimer KUnit runs with one and four vCPUs.
* Repeated four-vCPU native KUnit runs.
* Adjacent timer queue and interval timer KUnit runs.
* KASAN, lockdep, debug-object, and PREEMPT_RT x86_64 builds.
* Native, fallback, and C-only GPL module builds that reference all thirteen
  ordinary exported hrtimer entry points.
* Object and final-image symbol ownership proves that Rust defines the direct
  entries and the native C object does not.
* ``Module.symvers`` contains the expected thirteen GPL exports and no
  ``rust_hrtimer_*`` adapter or unexported entry.
* The final image has no unresolved hrtimer migration symbol.
* A production configuration without KUnit builds completely with Clippy and
  the kernel ``-D warnings`` policy.
* Rustfmt, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation evidence
-------------------

The complete entry-point unit was validated on 2026-11-24 with x86_64
``LLVM=1`` builds, Clang and LLD 21.1.8, and QEMU 11.1.3.

The native Rust, Rust state with C public wrappers, KASAN, lockdep,
debug-object, and PREEMPT_RT configurations each passed 25 of 25 KUnit cases
with one and four vCPUs. The C-only configuration passed five of five cases
with one and four vCPUs. The native four-vCPU test passed 25 of 25 cases in
three consecutive runs. The adjacent timer queue tests passed four of four
cases, and the adjacent interval timer tests passed six of six cases, with
both one and four vCPUs. No run reported an ODEBUG diagnostic, warning, BUG,
KASAN report, Oops, or panic.

The object and export audit covered native, fallback, C-only, KASAN, lockdep,
debug-object, and PREEMPT_RT builds. In native, KASAN, lockdep,
debug-object, and PREEMPT_RT builds, ``rust/kernel.o`` defined all thirteen
ordinary public entries and ``kernel/time/hrtimer.o`` defined none. Each
build had thirteen ``EXPORT_SYMBOL_GPL`` entries in ``Module.symvers`` and
thirteen matching entries in the generated Rust export header. In fallback
and C-only builds, the C object defined all thirteen entries and Rust defined
none.

The internal interrupt, queue, next-event, and deferred-rearm entries and all
``rust_hrtimer_*`` adapters had no exports. ``hrtimer_resolution`` remained
C-defined and GPL-exported in every build. With debug objects enabled, Rust
defined ``destroy_hrtimer_on_stack()`` as a GPL export and C did not define
it. On PREEMPT_RT, Rust defined ``hrtimer_cancel_wait_running()`` and C did
not define it.

An external GPL module linked separately against the native, fallback, and
C-only builds. Each module had exactly the thirteen ordinary public hrtimer
symbols as undefined references. The modules were not loaded.

The production configuration without KUnit built a complete image with
``CLIPPY=1`` under the kernel ``-D warnings`` policy. Rustdoc generation also
passed. ``make rustfmtcheck``, strict checkpatch, ``git diff --check``, and the
focused Rust Sphinx build passed. The Sphinx build reported only two existing
cross-reference warnings from documents excluded by the focused build.

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
* The timer base lock helper context, raw spinlock acquisition and release, or
  trace-before-unlock ordering changes.
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
