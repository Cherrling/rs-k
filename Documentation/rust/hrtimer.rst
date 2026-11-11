.. SPDX-License-Identifier: GPL-2.0

Native Rust HRTimer Queue State
===============================

Status and scope
----------------

The ``CONFIG_RUST_HRTIMER`` option selects a native Rust state machine for
starting and updating an hrtimer on its current local clock base.  The public
hrtimer API and device driver scope remain unchanged.

Rust owns these production decisions:

* Enqueueing an inactive timer on its current clock base.
* Updating an active timer in place when its new expiry remains between the
  current neighbors.
* Removing and requeueing an active timer when its new expiry crosses the next
  neighbor.
* Maintaining the clock base's cached first expiry.
* Maintaining the CPU base's active-clock-base bit.
* Publishing the lockless ``is_queued`` state.

The existing timer queue and red-black tree operations perform ordered linked
tree insertion and erasure.

Entry points and state ownership
--------------------------------

The C ``remove_and_enqueue_same_base()`` path calls
``rust_hrtimer_requeue_same_base()`` when the Rust option is enabled.  The
original C neighbor decision and update sequence are compiled out.

Rust uses the existing ``struct hrtimer``, ``struct hrtimer_clock_base``, and
``struct hrtimer_cpu_base`` layouts.  It does not call the original C
``hrtimer_can_update_in_place()`` decision.

Retained C boundary
-------------------

The following operations remain in C:

* Acquiring and releasing the CPU base lock.
* Debug object activation and deactivation.
* HRTimer tracepoint delivery.
* Clock event reprogramming and remote CPU notification.
* Timer base migration and callback execution.
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

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, generic KASAN, and lockdep KUnit runs.
* A four-vCPU PREEMPT_RT KUnit run with the Rust implementation enabled.
* A four-vCPU run with ``CONFIG_RUST_RBTREE`` disabled.
* In-place expiry updates and requeues that cross the next neighbor.
* Clock-base expiry cache maintenance and real callback delivery.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The hrtimer queue state was validated on 2026-11-11 with x86_64 ``LLVM=1``
builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed both KUnit cases.
* The four-vCPU C fallback passed both cases.
* The four-vCPU generic KASAN and lockdep images each passed both cases.
* The four-vCPU PREEMPT_RT image passed both cases with
  ``CONFIG_RUST_HRTIMER`` enabled.
* The four-vCPU image with ``CONFIG_RUST_RBTREE`` disabled passed both cases.
* The cases covered same-base in-place updates, a requeue that crosses the
  next timer, cached first-expiry maintenance, and a real expiry callback.
* No run reported a ``BUG``, warning, KASAN report, lock dependence
  diagnosis, or panic.
* The hrtimer start path calls ``rust_hrtimer_requeue_same_base()``. The
  linked vmlinux resolves the helper functions and contains no unresolved
  hrtimer migration symbols. No module references these internal symbols.
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
