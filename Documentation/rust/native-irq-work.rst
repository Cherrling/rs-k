.. SPDX-License-Identifier: GPL-2.0

Native Rust IRQ work execution
==============================

Status and scope
----------------

The ``CONFIG_RUST_IRQ_WORK`` option selects native Rust for the production
``irq_work`` state machine. The C ABI entry points remain in
``kernel/irq_work.c`` and call Rust when the option is enabled.

This unit owns IRQ work claim, duplicate suppression, queue selection,
callback state transitions, lockless-list draining, timer-tick processing,
and synchronization policy. It does not migrate architecture IRQ entry code,
per-CPU storage, IPI delivery, hotplug, or the PREEMPT_RT worker lifecycle.

The complete C implementation remains available when the option is disabled.

Ownership
---------

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

* Symbol checks for every production Rust entry point.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Twenty-one final x86_64 boots passed. Six SMP1 boots covered the native,
fallback, C-only, KASAN, lockdep, and PREEMPT_RT configurations. Twelve SMP4
boots ran two complete passes of the same matrix. Two additional SMP4 lockdep
runs and one SMP4 task-work regression run also passed.

The non-RT configurations passed seven cases on SMP4 and five cases on SMP1.
The two SMP1 skips were the remote CPU and contended synchronization cases.
PREEMPT_RT passed eight cases on SMP4 and five on SMP1. Its three SMP1 skips
were the two remote cases and contended synchronization.

A scan of the final logs found no failing KUnit case, BUG, WARNING, Oops,
panic, RCU stall, KASAN report, or lockdep report. The KASAN boots initialized
generic KASAN. The lockdep boots initialized the lock dependency validator.
The task-work suite passed all six cases on SMP4.

The native non-RT ``kernel/irq_work.o`` referenced seven Rust production
entry points. The PREEMPT_RT object referenced ``rust_irq_work_run_lazy`` as
well, for all eight entry points. Fallback and C-only objects referenced none
of the Rust entry points.

Rustfmt, Clippy, and Rustdoc passed. Focused Rust Sphinx generation completed
without warnings from the new page. The full-tree strict Sphinx build remains
blocked by pre-existing errors in unrelated documentation.

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
