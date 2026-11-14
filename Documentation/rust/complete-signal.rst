.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Completion
=============================

Status and scope
----------------

The ``CONFIG_RUST_COMPLETE_SIGNAL`` option selects native Rust for the
production ``complete_signal()`` function in ``kernel/signal.c``. The original
C implementation remains available as a fallback.

This unit covers notification-target selection, thread-group target balancing,
fatal group-exit preparation, coredump deferral, and the final signal wakeup.
It excludes signal queue insertion, signal delivery, group-stop handling, and
the surrounding ``__send_signal_locked()`` state machine.

The Rust code owns these decisions and transitions:

1. Testing the suggested task.
2. Rejecting a blocked PID-specific signal.
3. Scanning the thread group for an eligible task.
4. Updating ``signal->curr_target``.
5. Detecting when no thread needs a wakeup.
6. Recognizing fatal signals, real blocked signals, and ptrace.
7. Preparing fatal group-exit state and per-thread SIGKILL state.
8. Skipping immediate group exit for coredump signals.
9. Selecting the final fatal or non-fatal wakeup.

Entry points and ownership
--------------------------

The local ``complete_signal()`` function remains in ``kernel/signal.c``. When
the option is enabled, it calls ``rust_complete_signal()`` and the original C
state machine is compiled out.

Rust owns the complete production algorithm. C provides only signal-bit
operations, task-state inspection, thread traversal, job-control cleanup, and
the public adapter.

Retained C boundary
-------------------

The following operations remain in C:

* Signal-set membership and insertion.
* Fatal-signal and coredump classification.
* Thread-group membership and traversal.
* Task running, pending, stopped, and traced inspection.
* ``task_clear_jobctl_pending()``.
* The signal-handler lock assertion.
* The final wakeup, scheduler wakeup, process kick, and public ABI adapter.

The caller must hold ``task->sighand->siglock`` with local interrupts disabled.
Rust preserves that requirement and does not sleep.

Ordering and invariants
-----------------------

Rust first tries the suggested task. For a blocked PID-specific signal, it
returns immediately. For a process-wide signal, it scans from
``signal->curr_target`` and preserves the original target when no eligible
thread exists.

A fatal signal enters group-exit preparation only when the signal is fatal,
the selected task has not blocked it through ``real_blocked``, and the source
task is either not ptraced or the signal is SIGKILL. Coredump signals bypass
immediate group exit. Other fatal signals set ``SIGNAL_GROUP_EXIT``, clear
group-stop counters, clear job-control pending state, add SIGKILL to every
thread, and wake every thread with fatal state.

If group exit is not selected, Rust wakes only the chosen target.

Validation record
-----------------

Each configuration runs three consecutive times with four vCPUs and 1536 MiB
of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust signal completion
     - 3
     - 1 of 1
     - Pass
   * - C signal completion fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust signal completion with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust signal completion with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust signal completion with lockdep
     - 3
     - 1 of 1
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers the suggested task,
PID-specific rejection, target scan, no-candidate return, fatal group exit,
coredump deferral, and the real-blocked fatal path.

No run reported a BUG, warning, KASAN report, Oops, lockdep complaint, or
kernel panic.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The complete change
passed strict ``checkpatch`` and ``git diff --check``.

The production image disabled KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` showed direct relocations from signal senders to
``rust_complete_signal()``.

The production ``vmlinux`` contained the Rust completion entry, contained no
completion test symbols, and had no undefined symbols. The image booted to the
expected missing root-filesystem panic and reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* A helper starts to select notification policy or operation order.
* Signal classification or signal-set layout changes.
* Thread-group target balancing rules change.
* The ``sighand->siglock`` contract changes.
* The native Rust wakeup selection interface changes.
