.. SPDX-License-Identifier: GPL-2.0

Native Rust spurious IRQ detection
==================================

Status and scope
----------------

The ``CONFIG_RUST_IRQ_SPURIOUS`` option selects native Rust for the production
spurious IRQ detection state machine. The trace, diagnostic, recovery, and
timer behavior remains compatible with the C implementation.

Rust owns polling exclusions, invalid handler-result classification, deferred
threaded-result accounting, unhandled time windows, misroute policy,
stuck-interrupt thresholds, and final state transitions.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust owns:

* Excluding events while polling is already in progress.
* Excluding interrupt descriptors marked as always polled.
* Rejecting handler results above ``IRQ_HANDLED | IRQ_WAKE_THREAD``.
* Selecting the rate-limited diagnostic for an invalid result.
* Deferring an exact ``IRQ_WAKE_THREAD`` result by one hard interrupt.
* Comparing the threaded handled count with the deferred baseline.
* Classifying a changed threaded count as handled.
* Classifying an unchanged threaded count as unhandled.
* Clearing the deferred marker after a primary handler handles the event.
* Resetting the unhandled count outside the ``HZ / 10`` window.
* Accumulating unhandled events inside the window.
* Applying the ``irqfixup`` and ``irqpoll`` misroute policies.
* Correcting the unhandled count after successful misroute recovery.
* Checking the ``99,900`` unhandled-event threshold at ``100,000`` events.
* Selecting the unlimited stuck-IRQ diagnostic.
* Requesting descriptor disable and recovery-timer rearming.

C helpers retain:

* ``irq_desc`` field access and ``READ_ONCE()`` or ``WRITE_ONCE()`` access.
* The wrap-safe jiffies comparison.
* The ``irqfixup`` module parameter.
* Diagnostic formatting and stack dumps.
* The full IRQ descriptor scan for misrouted events.
* IRQ disabling and descriptor state mutation.
* Recovery-timer rearming.

The C helpers do not classify handler results, choose polling exclusions,
account for threaded handlers, apply misroute policy, or choose the stuck-IRQ
threshold.

Important invariants
--------------------

``note_interrupt()`` runs without ``desc->lock`` while
``IRQD_IRQ_INPROGRESS`` is set. Hard interrupts are not reentrant on the same
CPU, so the hard-interrupt path owns updates to ``threads_handled_last``,
``irqs_unhandled``, and ``irq_count``.

The C ``threads_handled_last`` field is signed, but the Rust boundary uses
``u32``. This preserves bit 31 without converting an unbounded handler result
or counter into a Rust enum. Counter addition and subtraction use wrapping
arithmetic to match the C unsigned fields.

An exact ``IRQ_WAKE_THREAD`` result delays analysis by one hard interrupt. A
combined ``IRQ_HANDLED | IRQ_WAKE_THREAD`` result clears the deferred marker
before unhandled accounting. The threaded handled count remains an atomic and
is read through a C helper.

The unhandled window ends strictly after ``last_unhandled + HZ / 10``.
``window_expired()`` retains the kernel's wrap-safe ``time_after()``
comparison.

Misroute recovery runs for every unhandled result when ``irqfixup`` is nonzero.
At level two, it also runs for handled events on IRQ zero or actions marked
``IRQF_IRQPOLL``. Successful recovery subtracts one from the unhandled count.

At ``irq_count == 100,000``, the threshold is strictly
``irqs_unhandled > 99,900``. A triggered event emits the unlimited stuck-IRQ
diagnostic, marks ``IRQS_SPURIOUS_DISABLED``, increments the disable depth,
disables the descriptor, and rearms the recovery timer.

Validation gate
---------------

The unit must pass these checks before integration:

* Polling-in-progress and always-polled event exclusion.
* Invalid handler-result rejection without counter changes.
* First threaded wake deferred to the next hard interrupt.
* Unchanged threaded count classified as unhandled.
* Changed threaded count classified as handled.
* Primary handled result clears the deferred marker.
* Recent unhandled counts accumulate.
* Stale windows reset the unhandled count.
* The stuck threshold disables the descriptor and re-arms its timer.
* Misroute policy respects the ``irqfixup`` level.
* Native Rust builds and boots with one and four CPUs.
* Rust fallback builds with ``CONFIG_RUST_IRQ_SPURIOUS=n``.
* C-only builds with ``CONFIG_RUST=n``.
* Native Rust with KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.
* Symbol and disassembly checks for the production Rust entry point.
* Existing IRQ and core Rust regression suites.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

The final source passes the IRQ KUnit suite in these configurations:

* Native Rust, SMP4: 28 passed.
* Native Rust, SMP1: 27 passed and one CPU-hotplug test skipped.
* Rust fallback, SMP4: 28 passed.
* Rust fallback, SMP1: 27 passed and one CPU-hotplug test skipped.
* C only, SMP4: 28 passed.
* C only, SMP1: 27 passed and one CPU-hotplug test skipped.
* Native Rust with KASAN, SMP4: 28 passed.
* Native Rust with KASAN, SMP1: 27 passed and one CPU-hotplug test skipped.
* Native Rust with lockdep, SMP4: 28 passed.
* Native Rust with lockdep, SMP1: 27 passed and one CPU-hotplug test skipped.
* Native Rust with PREEMPT_RT, SMP4: 28 passed.
* Native Rust with PREEMPT_RT, SMP1: 27 passed and one CPU-hotplug test
  skipped.

The existing IRQ work, rcuwait, simple wait queue, completion, memory lifetime,
and task work KUnit suites pass on the final native Rust image.

Symbol inspection finds ``rust_irq_spurious_note`` in the native Rust, KASAN,
lockdep, and PREEMPT_RT images. The symbol is absent from the fallback and C-only
images. Disassembly shows that ``note_interrupt`` tail-calls
``rust_irq_spurious_note``.

The final source also passes Rustfmt, Clippy, Rustdoc, focused Rust Sphinx
generation, ``git diff --check``, and strict checkpatch. Sphinx reports only the
existing ``kernel-docs`` reference warning and the existing ``getting_llvm``
label warning. KASAN reports the existing Maple Tree objtool ``noreturn``
advisory.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for IRQ descriptors and actions.
* Handler results become a bounded flags type.
* Deferred threaded accounting stops using bit 31.
* The unhandled sampling window changes from ``HZ / 10``.
* The stuck-IRQ threshold, sample interval, or disable sequence changes.
* Misroute recovery stops using the global ``irqfixup`` policy.
* The recovery timer becomes per-descriptor or runs outside timer context.
