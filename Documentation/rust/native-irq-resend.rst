.. SPDX-License-Identifier: GPL-2.0

Native Rust IRQ resend
======================

Status and scope
----------------

The ``CONFIG_RUST_IRQ_RESEND`` option selects native Rust for the production
IRQ resend state machine when ``CONFIG_HARDIRQS_SW_RESEND`` is enabled. The
public IRQ management API and descriptor layout remain unchanged.

Rust owns level, replay, and pending decisions, hardware retrigger fallback,
nested parent selection, software queue insertion policy, tasklet drain
ordering, and final replay-state transitions.

The complete C implementation remains available when the option is disabled.

Ownership
---------

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
* Symbol and disassembly checks for the production Rust entry points.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

The implementation passed the complete matrix with clang 21.1.8, LLD 21.1.8,
Rust 1.97.1, and QEMU.

The native, Rust fallback, C-only, KASAN, lockdep, and PREEMPT_RT kernels all
booted on one and four CPUs. Each run executed all 14 IRQ test cases. The SMP1
runs reported 13 passes, one CPU-hotplug skip, and no failures. The SMP4 runs
reported 14 passes and no failures.

The final native image also passed the existing core regression suites:

* ``irq_work``: seven tests.
* ``rcuwait``: eight tests.
* ``swait``: five tests.
* ``completion``: six tests.
* ``mm_lifetime``: six tests.
* ``task_work``: six tests.

Native, KASAN, lockdep, and PREEMPT_RT symbol tables contain
``rust_irq_resend_check``, ``rust_irq_resend_clear``,
``rust_irq_resend_init``, and ``rust_irq_resend_tasklet``. The fallback and
pure C symbol tables contain none of these symbols. Disassembly shows that the
native ``check_irq_resend()`` wrapper jumps directly to
``rust_irq_resend_check``.

The final source passed ``rustfmtcheck``, a full kernel build with
``CLIPPY=1``, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check``. Strict checkpatch reported no findings after the
reviewed ``FILE_PATH_CHANGES`` check, because ``MAINTAINERS`` covers all new
files. Focused Sphinx reported two existing cross-reference warnings because
the build includes only the Rust directory. The warnings reference the missing
``kernel-docs`` document and the undefined ``getting_llvm`` label. Neither
warning references this unit.

The final QEMU logs contain no KUnit failure, BUG, WARNING, Oops, panic, KASAN
report, lockdep report, or RCU stall. The KASAN and lockdep builds emitted one
existing Maple Tree objtool ``noreturn`` note. That note does not reference
this unit.

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
