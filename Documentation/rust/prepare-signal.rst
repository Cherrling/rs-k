.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Preparation
==============================

Status and scope
----------------

The ``CONFIG_RUST_PREPARE_SIGNAL`` option selects native Rust for the
production ``prepare_signal()`` function. The original C implementation
remains available as a fallback.

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

Rust directly defines ``prepare_signal()``. The native build compiles the
original C implementation out of ``kernel/signal.c``. Rust owns the operation
order and every branch and transition in signal preparation. The C fallback
and C-only configurations retain the complete implementation.

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
* The signal-handler lock contract.

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
   * - Rust signal preparation
     - 3 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C signal preparation fallback
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Mixed C insertion with Rust preparation
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal preparation with KASAN
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal preparation with lockdep
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal preparation with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers both group-exit
paths, stop cleanup, continue cleanup, thread job-control state, ptrace trap
selection, ignored signals, blocked ignored signals, and parent notification
state. Rust-only counters verify the selected transitions.

Every boot also ran the completion and locked queue insertion suites. Every
log reported no failed case, BUG, warning, KASAN report, lockdep complaint,
oops, or panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck``, Rustdoc, and Clippy. Focused Sphinx generated this page with
only the existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The
complete change passed ``git diff --check`` and strict ``checkpatch`` with zero
errors, warnings, or checks.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``prepare_signal()``. Native ``kernel/signal.o``
contains an undefined reference and no implementation. The production image
contains no ``rust_prepare_signal`` wrapper, no export table entry, and no
unresolved symbol. The image boots to the expected missing root-filesystem
panic without an earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for signal sets or intrusive queues.
* A helper starts to select stop, continue, notification, or wakeup policy.
* ``sig_ignored()`` moves into native Rust.
* The ``sighand->siglock`` contract changes.
* Thread-group traversal or ptrace trap ordering changes.
* The stop-flag or task job-control state model changes.
