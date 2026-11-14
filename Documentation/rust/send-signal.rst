.. SPDX-License-Identifier: GPL-2.0

Native Rust Locked Signal Queue Insertion
=========================================

Status and scope
----------------

The ``CONFIG_RUST_SEND_SIGNAL`` option selects native Rust for the production
locked signal queue insertion state machine in ``kernel/signal.c``. The
original C implementation remains available as a fallback.

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

The local ``__send_signal_locked()`` function remains in ``kernel/signal.c``.
When the option is enabled, its body calls ``rust_send_signal_locked()`` and
the original C queue insertion state machine is compiled out.

Rust owns the operation order and every admission and allocation decision. It
does not call the original C queue insertion state machine.

Retained C boundary
-------------------

The following operations remain in C:

* Signal-queue allocation and intrusive-list insertion.
* Signal-set membership and bit operations.
* Credential, PID namespace, and ``siginfo`` formatting.
* Special ``siginfo`` pointer recognition.
* ``signalfd_notify()``.
* The ``prepare_signal()`` calling convention. Its state machine moves to
  Rust when ``CONFIG_RUST_PREPARE_SIGNAL`` is enabled.
* ``complete_signal()``, including its native Rust completion dispatch.
* Delayed multiprocess list traversal.
* Signal classification and the generation tracepoint.
* The signal-handler lock assertion and public ABI adapters.

These boundaries isolate intrusive kernel data structures and existing signal
policy helpers until safe Rust abstractions exist. No helper selects queue
insertion policy or changes the Rust operation order.

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

Each configuration ran three consecutive times with four vCPUs and 1536 MiB of
guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust signal queue insertion
     - 3
     - 1 of 1
     - Pass
   * - C signal queue insertion fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust signal queue insertion with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust signal queue insertion with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust signal queue insertion with lockdep
     - 3
     - 1 of 1
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers exit rejection,
``prepare_signal()`` rejection, legacy de-duplication, private and no-info
payloads, real-time queues, SIGKILL allocation skipping, shared queues, and
delayed multiprocess stop and continue updates. Rust-only counters verify the
selected state transitions.

The existing native Rust ``complete_signal`` KUnit test also passed with
``CONFIG_RUST_COMPLETE_SIGNAL=y``.

No run reported a BUG, warning, KASAN report, Oops, lockdep complaint, or
kernel panic.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The complete change
passed strict ``checkpatch`` and ``git diff --check``.

The production image disabled KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` showed direct relocations from ``__send_signal_locked()``
and other signal senders to ``rust_send_signal_locked()``. The three C policy
bridges were also present.

The production ``vmlinux`` contained the Rust insertion entry and its helpers,
contained no signal insertion or completion test symbols, and had no undefined
symbols. The image booted to the expected missing root-filesystem panic and
reported no earlier failure.

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
