.. SPDX-License-Identifier: GPL-2.0

Native Rust HRTimer State
=========================

Status and scope
----------------

The ``CONFIG_RUST_HRTIMER`` option selects a native Rust state machine for
starting and updating an hrtimer on its current local clock base.
``CONFIG_RUST_HRTIMER_ENQUEUE`` selects Rust for inserting a timer into its
selected clock base.  The
``CONFIG_RUST_HRTIMER_CALLBACK`` option selects Rust for the state transitions
around an expiry callback.  The
``CONFIG_RUST_HRTIMER_BASE_SELECTION`` option selects Rust for the decision
that accepts or rejects a selected remote clock base.
``CONFIG_RUST_HRTIMER_NEXT_EVENT`` selects Rust for the search across active
hard and soft clock bases.
``CONFIG_RUST_HRTIMER_UPDATE_NEXT_EVENT`` selects Rust for combining those
results and selecting the event used for reprogramming.
``CONFIG_RUST_HRTIMER_UPDATE_NEEDS_IPI`` selects Rust for deciding whether a
clock change requires remote timer reevaluation.
``CONFIG_RUST_HRTIMER_REPROGRAM`` selects Rust for deciding whether a newly
queued timer changes the soft-timer caches or requires clock event
reprogramming.
The ``CONFIG_RUST_HRTIMER_CANCEL`` option extends that state machine to queued
removal and cancellation decisions.  ``CONFIG_RUST_HRTIMER_CANCEL_WAIT``
selects Rust for the cancellation retry loop.
``CONFIG_RUST_HRTIMER_FORWARD`` selects Rust for periodic expiry advancement.
The public hrtimer API and device driver scope remain unchanged.

Rust owns these production decisions:

* Inserting a timer into an active clock base, publishing the lockless queued
  state, setting the active-clock-base bit, and updating the cached first
  expiry.
* Publishing the callback-running timer, removing it from its queue without
  clock event reprogramming, and queuing an automatic restart.
* Separating the queued, callback-running, and post-callback state sections
  and clearing the callback-running marker.
* Enqueueing an inactive timer on its current clock base.
* Updating an active timer in place when its new expiry remains between the
  current neighbors.
* Removing and requeueing an active timer when its new expiry crosses the next
  neighbor.
* Accepting a local target, an offline-CPU fallback, or a remote target whose
  next expiry can meet the timer deadline.
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

The C ``__remove_hrtimer()`` path calls ``rust_hrtimer_remove()`` when the
cancellation option is enabled.  The C ``hrtimer_try_to_cancel()`` path calls
``rust_hrtimer_try_to_cancel_locked()`` after acquiring the timer base lock.
The original C removal and cancellation decision sequences are compiled out.

The C ``hrtimer_suitable_target()`` path calls
``rust_hrtimer_suitable_target()`` when the base-selection option is enabled.
The original C suitability decision is compiled out.

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
* The lockless inactive fast path in ``hrtimer_try_to_cancel()``.
* Debug object activation and deactivation.
* HRTimer tracepoint delivery.
* Clock event reprogramming and remote CPU notification.
* Clock offset updates and clock-was-set sequence advancement.
* Target CPU selection, migration-base locking, timer base migration, and
  callback execution.
* The lockless try-to-cancel entry point and PREEMPT_RT callback wait
  primitive with its expiry-lock synchronization.
* Checked ktime division, saturated expiry arithmetic, and range
  initialization.
* Public hrtimer ABI adapters.

Thin C helpers preserve ``READ_ONCE()`` and ``WRITE_ONCE()`` semantics for
``timer->is_queued`` and deliver debug and trace hooks.  They do not decide
queue state transitions.

Important invariants
--------------------

All queue state transitions are serialized by ``cpu_base->lock``.  The
lockless queued snapshot pairs its read with the Rust-controlled write.

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

A queued timer returns 1 from ``hrtimer_try_to_cancel()``.  A timer whose
callback is running returns -1.  An inactive timer returns 0.  Removing the
last timer in a clock base clears its active-base bit.

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
* A four-vCPU C base-selection fallback KUnit run.
* A four-vCPU C next-event fallback KUnit run.
* A four-vCPU C next-event update fallback KUnit run.
* A four-vCPU C IPI-decision fallback KUnit run.
* A four-vCPU C cancellation-wait fallback KUnit run.
* Four-vCPU generic inline KASAN and lockdep KUnit runs.
* A four-vCPU PREEMPT_RT KUnit run with the Rust implementation enabled.
* A four-vCPU run with ``CONFIG_RUST_RBTREE`` disabled.
* Local, offline-CPU, earlier remote, equal-deadline, and later remote base
  selection decisions.
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

The hrtimer queue, base-selection, next-event, reprogramming, insertion,
callback lifecycle, cancellation, and forwarding state was validated on
2026-11-11 and 2026-11-12 with x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all nine KUnit cases.
* Three consecutive four-vCPU Rust reprogramming runs passed all ten KUnit
  cases.
* Three consecutive four-vCPU Rust insertion runs passed all eleven KUnit
  cases.
* Three consecutive four-vCPU Rust callback-lifecycle runs passed all twelve
  KUnit cases.
* The four-vCPU C base-selection fallback passed all five applicable cases
  with ``CONFIG_RUST_HRTIMER_BASE_SELECTION`` disabled.
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
* Three consecutive four-vCPU runs on each generic inline KASAN, lockdep, and
  PREEMPT_RT image passed all twelve cases with the Rust implementation
  enabled.
* Three consecutive four-vCPU images with ``CONFIG_RUST_RBTREE`` disabled
  passed all twelve cases.
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
  same-base in-place updates, requeues that cross the next timer, cached
  first-expiry maintenance, callback-running cancellation rejection, queued
  cancellation, inactive cancellation, a real expiry callback, partial
  forwarding overruns, exact interval alignment, and resolution clamping.
* No run reported a ``BUG``, warning, KASAN report, lock dependence
  diagnosis, or panic.
* The hrtimer insertion path calls ``rust_hrtimer_enqueue()``. The hrtimer
  start path calls ``rust_hrtimer_requeue_same_base()``. The base-selection
  path calls ``rust_hrtimer_suitable_target()``. The callback path calls
  ``rust_hrtimer_callback_enter()`` and ``rust_hrtimer_callback_exit()``.
  The next-event path calls ``rust_hrtimer_get_next_event()``. The
  next-event update path calls ``rust_hrtimer_update_next_event()``. The
  IPI decision path calls ``rust_hrtimer_update_needs_ipi()``. The
  reprogramming path calls ``rust_hrtimer_reprogram()``. The
  removal and cancellation paths call ``rust_hrtimer_remove()`` and
  ``rust_hrtimer_try_to_cancel_locked()``. The cancellation wait path calls
  ``rust_hrtimer_cancel()``. The forwarding path calls
  ``rust_hrtimer_forward()``. The linked vmlinux resolves these functions
  and the base-selection, callback, and cancellation helpers. It contains no
  unresolved hrtimer migration symbols. No module references these internal
  symbols.
* Rustfmt and Clippy pass. Clippy uses the kernel trailing ``-D warnings``
  policy. Strict checkpatch reports no errors, warnings, or checks.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* CPU base locking or the lockless queued-state protocol changes.
* The PREEMPT_RT callback wait primitive or expiry-lock protocol changes.
* Timer queue linked ordering or first-entry maintenance changes.
* Active-base mask or pending-softirq next-event semantics change.
* Hard/soft merge precedence or cached soft-expiry ownership changes.
* Clock-was-set sequence or deferred-rearm protocol changes.
* Soft-timer cache ownership, reprogramming deadline normalization, or strict
  hard/soft event precedence changes.
* Clock event reprogramming becomes a Rust-owned primitive.
* HRTimer debug or trace hooks gain state-changing behavior.
* Base migration is included in a later Rust migration unit.
