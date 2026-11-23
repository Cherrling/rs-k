.. SPDX-License-Identifier: GPL-2.0

Native Rust IRQ work execution
==============================

Status and scope
----------------

Rust directly defines the public ``irq_work`` entry points when
``CONFIG_RUST_IRQ_WORK`` is enabled. The native implementation owns the
production state machine.

This unit owns IRQ work claim, duplicate suppression, queue selection,
callback state transitions, lockless-list draining, timer-tick processing,
and synchronization policy. It does not migrate architecture IRQ entry code,
per-CPU storage, IPI delivery, hotplug, or the PREEMPT_RT worker lifecycle.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust directly defines all seven public ``irq_work_*()`` functions. The three
historical GPL exports retain their export class. The other four entries and
the PREEMPT_RT lazy-worker bridge remain unexported.

Rust owns:

* The compare-exchange claim and duplicate-work decision.

* Local, remote, LAZY, and PREEMPT_RT queue selection.

* PENDING and BUSY transitions across callback dispatch.

* Reentrant claim handling during a callback.

* Detached lockless-list traversal and callback dispatch.

* Raised and lazy processing for the IRQ entry and timer tick.

* Fast and slow synchronization decisions.

C helpers retain:

* Per-CPU raised and lazy list storage.

* Preemption and current-CPU access.

* IPI delivery and the SMP call-single queue.

* Hotplug and PREEMPT_RT worker plumbing.

* Architecture IRQ entry and extra work registration.

* KASAN and lockdep instrumentation.

* Low-level memory barriers, RCU, ``rcuwait``, and CPU relax operations.

* Interrupt-context and sleepability checks for synchronization.

The C helpers do not retain a second claim, queue-selection, callback-state,
list-draining, or synchronization state machine.

State model
-----------

An IRQ work item uses the PENDING and BUSY bits as follows:

.. code-block:: text

   free       flags = 0
   claimed    flags = PENDING | BUSY
   callback   flags = BUSY
   busy       an in-progress callback prevents duplicate execution

The first successful claim sets PENDING and BUSY. Later claims while PENDING
is set return false. The callback consumer clears PENDING, invokes the
callback, and then clears BUSY only if no reentrant claim changed the state.

A callback may enqueue the same work again after PENDING is cleared. This
sets PENDING while BUSY remains set. The callback-end compare-exchange then
fails, preserves the new request, and leaves BUSY set for the next callback.

On PREEMPT_RT, non-HARD remote work is published on the target CPU's lazy
list. Rust then claims the target CPU's HARD wake work and submits it through
the call-single queue. HARD work uses the call-single queue directly.

Validation gate
---------------

The unit must pass these checks before integration:

* Sequential duplicate suppression and PENDING/BUSY state checks.

* LIFO local-list execution.

* Reentrant callback enqueue and a second callback.

* LAZY execution from the timer tick.

* Remote HARD work delivery on the selected CPU.

* Remote non-HARD work through the PREEMPT_RT lazy worker.

* Contended synchronization while a remote callback is active.

* Concurrent queue producers.

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_IRQ_WORK=n``.

* C-only builds with ``CONFIG_RUST=n``.

* Native Rust with KASAN.

* Native Rust with lockdep and prove-locking.

* Native Rust with PREEMPT_RT.

* A task-work regression boot because task work uses IRQ work.

* Symbol checks showing Rust defines all seven public entry points and the
  native C object defines none. Only the three historical GPL entries have
  module exports.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Sixteen final x86_64 boots passed. Five SMP1 boots covered the native,
fallback, C-only, KASAN, and lockdep configurations. Ten SMP4 boots covered
three native runs, one fallback run, one C-only run, three KASAN runs, one
lockdep run, and one PREEMPT_RT run. One additional SMP4 task-work regression
run passed.

The non-RT configurations passed seven cases on SMP4 and five cases on SMP1.
The two SMP1 skips were the remote CPU and contended synchronization cases.
PREEMPT_RT passed eight cases on SMP4, including the remote lazy-worker case.

A scan of the final logs found no failing KUnit case, BUG, WARNING, Oops,
panic, RCU stall, KASAN report, or lockdep report. The KASAN boots initialized
generic KASAN. The lockdep boots initialized the lock dependency validator.
The task-work suite passed all six cases on SMP4.

The native, KASAN, lockdep, and PREEMPT_RT ``rust/kernel.o`` objects define all
seven public entry points, while their ``kernel/irq_work.o`` objects define
none of those public entries. The PREEMPT_RT run exercises the internal
``rust_irq_work_run_lazy()`` bridge. The fallback and C-only
``kernel/irq_work.o`` objects define all seven public entries, while the
fallback Rust object defines none. All six ``Module.symvers`` files contain
only the three historical GPL exports. The generated Rust export headers
include those three names and exclude the four unexported public entries, the
lazy-worker bridge, and the old public ``rust_irq_work_*()`` adapters.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. The focused Sphinx build reported only two
pre-existing warnings in ``Documentation/rust/index.rst`` and
``Documentation/rust/quick-start.rst``. Neither page is changed by this unit.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The IRQ work flag encoding or callback contract changes.

* PREEMPT_RT queueing or worker ownership changes.

* The call-single queue stops accepting IRQ work nodes.

* Per-CPU list storage or preemption rules move into a reusable Rust
  abstraction.

* Measurements show that a C helper on the enqueue or callback path causes an
  unacceptable regression.
