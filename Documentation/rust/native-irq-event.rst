.. SPDX-License-Identifier: GPL-2.0

Native Rust IRQ event dispatch
==============================

Status and scope
----------------

The ``CONFIG_RUST_IRQ_EVENT`` option selects native Rust for the production
per-CPU IRQ event state machine. The public IRQ management API and descriptor
layout remain unchanged.

Rust owns action traversal, handler result aggregation, threaded wake policy,
duplicate wake suppression, oneshot and active-count transitions, and the
pending/in-progress lifecycle around event dispatch.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust owns:

* Traversing all actions installed on an IRQ descriptor.
* Selecting force-threaded lockdep context for eligible actions.
* Calling each primary action handler through one bounded callback helper.
* Checking that each handler returns with interrupts disabled.
* Accumulating all handler return bits.
* Handling only the exact ``IRQ_WAKE_THREAD`` result as a wake request.
* Rejecting a wake request that has no thread function.
* Rejecting a wake request for a handler thread that is exiting.
* Using ``IRQTF_RUNTHREAD`` to suppress duplicate wake requests.
* Updating oneshot masks and the active thread count before wakeup.
* Adding interrupt randomness after dispatch.
* Calling spurious-interrupt accounting when descriptor debugging is enabled.
* Clearing pending state and setting in-progress state before dispatch.
* Dropping and retaking ``desc->lock`` around action callbacks.
* Clearing in-progress state after dispatch.

C helpers retain:

* ``irq_desc`` and ``irqaction`` field access.
* Action-list first and next pointer access.
* IRQ settings and action-flag inspection.
* Handler invocation with tracepoints and optional duration diagnostics.
* Descriptor lock and interrupt-state transitions.
* Atomic thread-flag, oneshot-mask, and active-count primitives.
* Scheduler wakeup of the IRQ handler thread.
* Randomness, spurious-interrupt accounting, and diagnostics.

The C helpers do not choose wake policy, suppress duplicate wakeups, aggregate
handler results, or order the event lifecycle.

Important invariants
--------------------

``handle_irq_event()`` runs with interrupts disabled and ``desc->lock`` held.
Rust clears ``IRQS_PENDING`` and sets ``IRQD_IRQ_INPROGRESS`` before it drops
the descriptor lock. It retakes the lock and clears in-progress state after
all action callbacks return.

Primary handlers must return with interrupts disabled. A handler that enables
interrupts triggers the existing warning and Rust disables them again.

Only the exact ``IRQ_WAKE_THREAD`` value starts thread wakeup. A value that
combines ``IRQ_WAKE_THREAD`` with another result bit is accumulated but does
not start wakeup. This matches the C switch semantics.

The hard-interrupt path owns the lockless update of ``threads_oneshot``. The
IRQ handler thread serializes its matching clear operation with
``desc->lock``. The ``IRQS_INPROGRESS`` protocol keeps the hard-interrupt path
and the handler thread ordered.

``IRQTF_RUNTHREAD`` is set only for the first outstanding wake request. A
handler thread that exits without running leaves its active count to the
existing thread teardown path.

The Rust boundary uses ``u32`` for handler results. The C ``irqreturn_t`` enum
does not limit the bit patterns that a faulty handler can return, so a Rust
enum cannot safely represent this value.

Validation gate
---------------

The unit must pass these checks before integration:

* Shared action traversal and result-bit aggregation.
* Hardware handler execution with interrupts disabled.
* Pending state cleared and in-progress state set during callbacks.
* In-progress state cleared after dispatch.
* Actual threaded-handler wakeup and completion.
* Duplicate wake suppression and oneshot/active-count updates.
* Unknown handler result bits preserved without a Rust enum conversion.
* ``IRQ_WAKE_THREAD`` without a thread function.
* Native Rust builds and boots with one and four CPUs.
* Rust fallback builds with ``CONFIG_RUST_IRQ_EVENT=n``.
* C-only builds with ``CONFIG_RUST=n``.
* Native Rust with KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.
* Symbol and disassembly checks for all production Rust entry points.
* Existing IRQ resend and core Rust regression suites.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

The native, Rust fallback, C-only, KASAN, lockdep, and PREEMPT_RT kernels all
booted on one and four CPUs. Each run executed all 19 IRQ test cases. The SMP1
runs reported 18 passes, one CPU-hotplug skip, and no failures. The SMP4 runs
reported 19 passes and no failures.

The native image also passed the existing core regression suites:

* ``irq_work``: seven tests.
* ``rcuwait``: eight tests.
* ``swait``: five tests.
* ``completion``: six tests.
* ``mm_lifetime``: six tests.
* ``task_work``: six tests.

Native, KASAN, lockdep, and PREEMPT_RT symbol tables contain
``rust_irq_event_dispatch``, ``rust_irq_event_handle``,
``rust_irq_event_percpu``, and ``rust_irq_event_wake_thread``. The Rust
fallback and C-only symbol tables contain none of these symbols.

Disassembly shows direct jumps from these production C entry points:

* ``__handle_irq_event_percpu()`` to ``rust_irq_event_dispatch``.
* ``handle_irq_event_percpu()`` to ``rust_irq_event_percpu``.
* ``handle_irq_event()`` to ``rust_irq_event_handle``.
* ``__irq_wake_thread()`` to ``rust_irq_event_wake_thread``.

The source passed ``rustfmtcheck``, a full kernel build with ``CLIPPY=1``,
Rustdoc, focused Sphinx generation, strict checkpatch, and the
``git diff --check`` check. Strict checkpatch reported no findings after the
reviewed ``FILE_PATH_CHANGES`` check. Focused Sphinx reported the existing
missing ``kernel-docs`` document and undefined ``getting_llvm`` label warnings.

The no-thread-function test intentionally emits one
``IRQ_WAKE_THREAD``-without-thread diagnostic. The QEMU logs contain no KUnit
failure, BUG, unexpected WARNING, Oops, panic, KASAN report, lockdep report,
or RCU stall. The KASAN and lockdep builds emitted the existing Maple Tree
objtool ``noreturn`` note.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for IRQ descriptors, actions, hlist
  traversal, or handler callbacks.
* Handler tracing or duration diagnostics move out of the callback wrapper.
* The ``irqreturn_t`` ABI becomes a bounded flags type.
* Thread wakeup stops using ``IRQTF_RUNTHREAD``, oneshot masks, and
  ``threads_active``.
* The hard-interrupt and IRQ-thread serialization protocol changes.
* Action dispatch moves out of per-CPU hard-interrupt context.
