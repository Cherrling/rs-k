.. SPDX-License-Identifier: GPL-2.0

Native Rust Locked Signal Queue Insertion
=========================================

Status and scope
----------------

The ``CONFIG_RUST_SEND_SIGNAL`` option selects native Rust for the production
``__send_signal_locked()`` function. The original C implementation remains
available as a fallback.

This unit covers queue admission, ``prepare_signal()`` result handling, legacy
signal de-duplication, allocation policy, real-time queue insertion,
``siginfo`` selection, pending-bit publication, delayed multiprocess updates,
signal completion, and trace result selection. It excludes signal delivery and
the internal policies of ``prepare_signal()`` and ``complete_signal()``.

The Rust code owns these decisions and transitions:

1. Rejecting a PID-specific signal for an exiting task.
2. Dropping a signal rejected by ``prepare_signal()``.
3. Selecting the task-local or thread-group pending queue.
4. De-duplicating non-real-time signals that are already pending.
5. Skipping allocation for SIGKILL and kernel threads.
6. Selecting the real-time resource-limit policy.
7. Selecting and appending queue payload information.
8. Selecting overflow failure or silent information loss.
9. Publishing the signalfd and pending state.
10. Updating delayed multiprocess stop and continue state.
11. Completing delivery and emitting the generation trace result.

Entry points and ownership
--------------------------

Rust directly defines ``__send_signal_locked()``. The native build compiles the
original C implementation out of ``kernel/signal.c``. Rust owns the operation
order and every admission and allocation decision. The C fallback and C-only
configurations retain the complete implementation.

The Rust insertion state machine calls the Rust preparation and completion
state machines directly. C no longer dispatches either state machine through
a policy bridge.

Retained C boundary
-------------------

The following operations remain in C:

* Signal-queue allocation and intrusive-list insertion.
* Signal-set membership and bit operations.
* Credential, PID namespace, and ``siginfo`` formatting.
* Special ``siginfo`` pointer recognition.
* ``signalfd_notify()``.
* Delayed multiprocess list traversal.
* Signal classification and the generation tracepoint.
* The signal-handler lock assertion.

These boundaries isolate intrusive kernel data structures and low-level signal
helpers until safe Rust abstractions exist. No helper selects queue insertion
policy or changes the Rust operation order.

The caller must hold ``task->sighand->siglock`` with local interrupts disabled.
Rust preserves that requirement and does not sleep.

Ordering and invariants
-----------------------

Rust checks task eligibility before ``prepare_signal()``. Legacy
de-duplication occurs after ``prepare_signal()`` and before allocation. Rust
does not allocate a queue for SIGKILL or kernel threads. For other signals,
the allocation limit override applies only to non-real-time signals with
special or non-negative-source ``siginfo``.

If a real-time queue allocation fails for a non-special signal not sent by
``SI_USER``, Rust reports ``-EAGAIN`` and updates the trace result without
publishing pending state. Other allocation failures publish the signal while
recording information loss.

For task-local and shared queues, Rust calls ``signalfd_notify()`` before it
sets the pending bit. Group and session signals update delayed multiprocess
state after pending publication and before completion. Rust emits the
generation trace result on every exit path.

Validation record
-----------------

Each boot used 1536 MiB of guest memory. Native completed three four-vCPU
runs and one single-vCPU run. Every other configuration completed one run at
each CPU count.

.. list-table::
   :widths: 34 16 14 12 24
   :header-rows: 1

   * - Configuration
     - Boots
     - Signal groups
     - Cases
     - Result
   * - Rust signal queue insertion
     - 3 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C signal queue insertion fallback
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Mixed C insertion and Rust prepare/complete
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal queue insertion with KASAN
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal queue insertion with lockdep
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal queue insertion with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers exit rejection,
``prepare_signal()`` rejection, legacy de-duplication, private and no-info
payloads, real-time queues, SIGKILL allocation skipping, shared queues, and
delayed multiprocess stop and continue updates. Rust-only counters verify the
selected state transitions.

Every boot also ran the preparation and completion suites. Every log reported
no failed case, BUG, warning, KASAN report, lockdep complaint, oops, or panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck``, Rustdoc, and Clippy. Focused Sphinx generated this page with
only the existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The
complete change passed ``git diff --check`` and strict ``checkpatch`` with zero
errors, warnings, or checks.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``__send_signal_locked()``. Native
``kernel/signal.o`` contains an undefined reference and no implementation.
Disassembly shows direct calls from the Rust insertion state machine to the
Rust preparation and completion state machines.

The production image contains no ``rust_send_signal_locked`` wrapper, no old
preparation or completion dispatch helpers, no export table entry for the
entry, and no unresolved symbol. The image boots to the expected missing
root-filesystem panic without an earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for signal queues, signal sets, or
  intrusive lists.
* A helper starts to select queue insertion policy or operation order.
* The ``sighand->siglock`` contract changes.
* Signal queue allocation or resource-limit rules change.
* Delayed multiprocess list ordering changes.
* The native Rust completion interface changes.
