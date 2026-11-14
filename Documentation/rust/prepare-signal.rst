.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Preparation
==============================

Status and scope
----------------

The ``CONFIG_RUST_PREPARE_SIGNAL`` option selects native Rust for the
production ``prepare_signal()`` state machine in ``kernel/signal.c``. The
original C implementation remains available as a fallback.

This unit covers process-exit rejection, kernel stop handling, SIGCONT
handling, thread-group queue cleanup, job-control state transitions, ptrace
trap selection, stopped-child notification selection, and the final delivery
decision. It excludes the internal policy of ``sig_ignored()`` and the
low-level signal delivery path.

The Rust code owns these decisions and transitions:

1. Rejecting signals while a non-coredumping process is exiting.
2. Allowing only SIGKILL during a coredumping process exit.
3. Removing pending SIGCONT state when a kernel stop signal arrives.
4. Removing pending stop signals when SIGCONT arrives.
5. Clearing stop-pending and stopped state on every thread.
6. Selecting ptrace notification or stopped-task wakeup per thread.
7. Selecting CLD_CONTINUED or CLD_STOPPED notification state.
8. Clearing group-stop count and exit code after notification selection.
9. Returning the delivery result from the ignored-signal policy.

Entry points and ownership
--------------------------

The local ``prepare_signal()`` function remains in ``kernel/signal.c`` as the
calling convention used by existing signal code. When the option is enabled,
its body calls ``rust_prepare_signal()`` and the original C state machine is
compiled out.

Rust owns the operation order and every branch and transition in signal
preparation. It does not call the original C state machine.

Retained C boundary
-------------------

The following operations remain in C:

* Signal-set mask construction.
* Queue-mask cleanup and intrusive signal-queue removal.
* Thread-group list traversal.
* ``sig_ignored()`` and its handler, blocked, ptrace, init, unkillable, and
  kernel-thread policies.
* ``task_clear_jobctl_pending()``.
* Stopped-task wakeup.
* Ptrace trap scheduling and ptrace wakeup selection.
* Stop-flag bit replacement.
* The signal-handler lock assertion and public ABI adapters.

These boundaries isolate intrusive queues, task and ptrace state, scheduler
wakeup, and the existing ignored-signal policy. No helper selects the
stop/continue branch or changes the Rust operation order.

The caller must hold ``task->sighand->siglock``. Rust preserves that
requirement and does not sleep.

Ordering and invariants
-----------------------

Rust checks process-exit state before any signal classification. During a
coredump, only SIGKILL passes the exit-state filter. A kernel stop signal
clears queued SIGCONT state before the final ignored-signal decision. SIGCONT
clears queued stop signals and updates every thread before selecting parent
notification state.

Rust completes the per-thread update before it updates the process-wide stop
flags. It updates process-wide stop flags and clears group-stop state before
the final ignored-signal decision. This preserves the ordering of the C
implementation.

Validation record
-----------------

Each configuration runs with four vCPUs and 1536 MiB of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust signal preparation
     - 3
     - 1 of 1
     - Pass
   * - C signal preparation fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust signal preparation with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust signal preparation with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust signal preparation with lockdep
     - 3
     - 1 of 1
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers both group-exit
paths, stop cleanup, continue cleanup, thread job-control state, ptrace trap
selection, ignored signals, blocked ignored signals, and parent notification
state. Rust-only counters verify the selected transitions.

The existing Rust ``complete_signal`` and locked queue insertion KUnit tests
also run with ``CONFIG_RUST_PREPARE_SIGNAL=y``.

Build and production checks
---------------------------

The Rust source passes Clippy and ``rustfmtcheck``. The Rust documentation
builds with Sphinx. The complete change passes ``git diff --check`` and strict
``checkpatch`` with its generic new-file warning ignored. ``MAINTAINERS`` lists
the new files.

The production image disables KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` shows direct calls to ``rust_prepare_signal()`` from the
production signal paths where the local ``prepare_signal()`` adapter is
inlined. The production ``vmlinux`` contains no preparation test symbols and
has no undefined symbols. The image boots to the expected missing
root-filesystem panic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for signal sets or intrusive queues.
* A helper starts to select stop, continue, notification, or wakeup policy.
* ``sig_ignored()`` moves into native Rust.
* The ``sighand->siglock`` contract changes.
* Thread-group traversal or ptrace trap ordering changes.
* The stop-flag or task job-control state model changes.
