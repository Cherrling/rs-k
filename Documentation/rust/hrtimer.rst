.. SPDX-License-Identifier: GPL-2.0

Native Rust HRTimer Queue and Cancellation State
================================================

Status and scope
----------------

The ``CONFIG_RUST_HRTIMER`` option selects a native Rust state machine for
starting and updating an hrtimer on its current local clock base.  The
``CONFIG_RUST_HRTIMER_CANCEL`` option extends that state machine to queued
removal and cancellation decisions.  The public hrtimer API and device driver
scope remain unchanged.

Rust owns these production decisions:

* Enqueueing an inactive timer on its current clock base.
* Updating an active timer in place when its new expiry remains between the
  current neighbors.
* Removing and requeueing an active timer when its new expiry crosses the next
  neighbor.
* Maintaining the clock base's cached first expiry.
* Maintaining the CPU base's active-clock-base bit.
* Publishing the lockless ``is_queued`` state.
* Removing queued timers and clearing an empty active clock base.
* Returning the inactive, queued, and callback-running cancellation results.

The existing timer queue and red-black tree operations perform ordered linked
tree insertion and erasure.

Entry points and state ownership
--------------------------------

The C ``remove_and_enqueue_same_base()`` path calls
``rust_hrtimer_requeue_same_base()`` when the Rust option is enabled.  The
original C neighbor decision and update sequence are compiled out.

The C ``__remove_hrtimer()`` path calls ``rust_hrtimer_remove()`` when the
cancellation option is enabled.  The C ``hrtimer_try_to_cancel()`` path calls
``rust_hrtimer_try_to_cancel_locked()`` after acquiring the timer base lock.
The original C removal and cancellation decision sequences are compiled out.

Rust uses the existing ``struct hrtimer``, ``struct hrtimer_clock_base``, and
``struct hrtimer_cpu_base`` layouts.  It does not call the original C
``hrtimer_can_update_in_place()`` decision or duplicate the C removal state
machine.

Retained C boundary
-------------------

The following operations remain in C:

* Acquiring and releasing the CPU base lock.
* The lockless inactive fast path in ``hrtimer_try_to_cancel()``.
* Debug object activation and deactivation.
* HRTimer tracepoint delivery.
* Clock event reprogramming and remote CPU notification.
* Timer base migration and callback execution.
* The cancellation wait loop and callback-running synchronization.
* Expiry arithmetic and range initialization.
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

When the first timer is removed and requeued, the clock base's cached expiry
must be recomputed from the new linked-tree leftmost entry.  The CPU base
active bit remains set across a same-base dequeue and enqueue.

A queued timer returns 1 from ``hrtimer_try_to_cancel()``.  A timer whose
callback is running returns -1.  An inactive timer returns 0.  Removing the
last timer in a clock base clears its active-base bit.

Removal can retain the queued state for immediate requeue.  Cancellation
publishes the inactive state.  The queued-state write still pairs with the
lockless snapshot in ``hrtimer_is_queued()``.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, generic KASAN, and lockdep KUnit runs.
* A four-vCPU PREEMPT_RT KUnit run with the Rust implementation enabled.
* A four-vCPU run with ``CONFIG_RUST_RBTREE`` disabled.
* In-place expiry updates and requeues that cross the next neighbor.
* Queued cancellation, callback-running rejection, and inactive cancellation.
* Clock-base expiry cache maintenance and real callback delivery.
* A four-vCPU run with ``CONFIG_RUST_HRTIMER_CANCEL`` disabled.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The hrtimer queue and cancellation state was validated on 2026-11-11 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all three KUnit cases.
* The four-vCPU C removal and cancellation fallback passed all three cases
  with ``CONFIG_RUST_HRTIMER_CANCEL`` disabled.
* The four-vCPU generic inline KASAN and lockdep images each passed all three
  cases.
* The four-vCPU PREEMPT_RT image passed all three cases with both Rust options
  enabled.
* The four-vCPU image with ``CONFIG_RUST_RBTREE`` disabled passed all three
  cases.
* The cases covered same-base in-place updates, requeues that cross the next
  timer, cached first-expiry maintenance, callback-running cancellation
  rejection, queued cancellation, inactive cancellation, and a real expiry
  callback.
* No run reported a ``BUG``, warning, KASAN report, lock dependence
  diagnosis, or panic.
* The hrtimer start path calls ``rust_hrtimer_requeue_same_base()``. The
  removal and cancellation paths call ``rust_hrtimer_remove()`` and
  ``rust_hrtimer_try_to_cancel_locked()``. The linked vmlinux resolves the
  helper functions and contains no unresolved hrtimer migration symbols. No
  module references these internal symbols.
* Rustfmt and Clippy pass. Clippy uses the kernel trailing ``-D warnings``
  policy. Strict checkpatch reports no errors or checks. Its only warning
  asks whether new file paths are covered, and the added ``MAINTAINERS``
  entries cover them.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* CPU base locking or the lockless queued-state protocol changes.
* Timer queue linked ordering or first-entry maintenance changes.
* Clock event reprogramming becomes a Rust-owned primitive.
* HRTimer debug or trace hooks gain state-changing behavior.
* Base migration is included in a later Rust migration unit.
