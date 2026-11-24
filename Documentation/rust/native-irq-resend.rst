.. SPDX-License-Identifier: GPL-2.0

Native Rust IRQ resend
======================

Status and scope
----------------

Rust directly defines the production IRQ resend entry points when
``CONFIG_RUST_IRQ_RESEND`` is enabled. The public IRQ management API and
descriptor layout remain unchanged.

Rust owns level, replay, and pending decisions, hardware retrigger fallback,
nested parent selection, software queue insertion policy, tasklet drain
ordering, and final replay-state transitions.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust directly defines all four internal production entry points:
``check_irq_resend()``, ``clear_irq_resend()``, ``irq_resend_init()``, and
the ``resend_irqs()`` tasklet callback. They remain unexported.

Rust owns:

* Retiring pending level interrupts with ``-EINVAL``.
* Rejecting a second replay with ``-EBUSY``.
* Returning success when no pending interrupt exists and injection is off.
* Clearing pending state before retrigger or software resend.
* Selecting a direct chip retrigger before hierarchical retrigger.
* Falling back to the software queue when hardware returns zero.
* Rejecting interrupt-context-enforced descriptors on the software path.
* Resolving a nested-thread interrupt to its valid parent.
* Inserting each descriptor into the resend queue at most once.
* Scheduling the resend tasklet.
* Draining descriptors in queue-head order.
* Setting ``IRQS_REPLAY`` only after a successful resend attempt.

C helpers retain:

* ``irq_desc`` field access.
* The raw resend lock.
* The IRQ-safe resend-lock wrapper for tasklet queue consumption.
* hlist insertion, removal, hashing, and extraction.
* Tasklet scheduling and test-only drain control.
* Direct and hierarchical irqchip retrigger callbacks.
* IRQ-number lookup.
* Flow-handler invocation.

The C helpers do not choose a state transition, select the software fallback,
deduplicate queue entries, or order tasklet callbacks.

Important invariants
--------------------

``check_irq_resend()`` runs with interrupts disabled and ``desc->lock`` held.
Level interrupts clear only ``IRQS_PENDING``. A descriptor marked
``IRQS_REPLAY`` cannot start another replay until its flow handler clears the
bit.

The irqchip retrigger ABI uses one for success. Zero selects software resend.
A nested-thread descriptor queues its parent descriptor and the parent is
deduplicated by the embedded hlist node.

The resend tasklet owns queue consumption. The tasklet keeps hardware
interrupts disabled. Rust holds the resend lock for each head test and removal,
drops the lock while the flow handler runs, and retakes the lock before
checking the next entry.

Validation gate
---------------

The unit must pass these checks before integration:

* Level-interrupt retirement and preservation of unrelated state.
* Replay-busy rejection without changing pending state.
* No-pending success with injection disabled.
* Direct hardware retrigger success without software dispatch.
* Hierarchical hardware retrigger success without software dispatch.
* Hardware retrigger failure followed by software dispatch.
* Nested-parent software resend.
* Duplicate suppression for two nested children that share a parent.
* Software queue-head dispatch order.
* Software flow-handler dispatch with hardware interrupts disabled.
* Interrupt-context enforcement and missing-parent rejection.
* Injection that forces software resend.
* Native Rust builds and boots with one and four CPUs.
* Rust fallback builds with ``CONFIG_RUST_IRQ_RESEND=n``.
* C-only builds with ``CONFIG_RUST=n``.
* Native Rust with KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.
* Symbol checks showing Rust defines all four production entry points and the
  native C object defines none. The four entries remain unexported.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Eighteen final x86_64 IRQ KUnit boots passed. Native passed 28 cases in four
SMP4 runs and 27 cases with one expected CPU-hotplug skip in two SMP1 runs.
Rust fallback, C-only, KASAN, lockdep, and PREEMPT_RT each passed 28 cases on
SMP4 and 27 cases with the same expected skip on SMP1. Three of the four KASAN
runs used four CPUs. The suite includes nine IRQ resend cases.

The native image also passed the adjacent core regression suites:

* ``irq_work``: seven tests.
* ``rcuwait``: eight tests.
* ``swait``: five tests.
* ``completion``: six tests.
* ``mm_lifetime``: six tests.
* ``task_work``: six tests.

A scan of all runtime logs found no failing KUnit case, BUG, WARNING, Oops,
panic, RCU stall, KASAN report, or lockdep report. The event tests emitted
their expected no-thread, bogus-result, misroute, and stuck-IRQ diagnostics.

The native, KASAN, lockdep, and PREEMPT_RT ``rust/kernel.o`` objects define all
four production entry points, while their ``kernel/irq/resend.o`` objects
define none. The fallback and C-only ``kernel/irq/resend.o`` objects define all
four, including a local tasklet callback, while the fallback Rust object
defines none. All six ``Module.symvers`` files and generated Rust export
headers exclude the four entries. No build contains the old
``rust_irq_resend_*`` adapters.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx reported two existing
cross-reference warnings because the build includes only the Rust directory.
The warnings reference the missing ``kernel-docs`` document and the undefined
``getting_llvm`` label. Neither warning references this unit.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for IRQ descriptors, chip callbacks, or
  hlist queues.
* The resend queue stops using a single tasklet or embedded hlist node.
* The irqchip retrigger return convention changes.
* Nested-thread resend stops targeting ``parent_irq``.
* Flow handlers require a different lock-drop sequence.
* The resend tasklet is replaced by a threaded, per-CPU, or workqueue design.
