.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Completion
=============================

Status and scope
----------------

The ``CONFIG_RUST_COMPLETE_SIGNAL`` option selects native Rust for the
production ``complete_signal()`` function. The original C implementation
remains available as a fallback.

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

Rust directly defines ``complete_signal()``. The native build compiles the
original C implementation out of ``kernel/signal.c``. Rust owns the complete
production algorithm. C provides only signal-bit operations, task-state
inspection, thread traversal, and job-control cleanup. The C fallback and
C-only configurations retain the complete implementation.

Retained C boundary
-------------------

The following operations remain in C:

* Signal-set membership and insertion.
* Fatal-signal and coredump classification.
* Thread-group membership and traversal.
* Task running, pending, stopped, and traced inspection.
* ``task_clear_jobctl_pending()``.
* The signal-handler lock assertion.
* The final wakeup, scheduler wakeup, and process kick.

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
   * - Rust signal completion
     - 3 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C signal completion fallback
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Mixed C insertion with Rust completion
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal completion with KASAN
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal completion with lockdep
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal completion with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass

The integration case creates a real shared thread group and calls the
production entry under the signal-handler lock. It covers the suggested task,
PID-specific rejection, target scan, no-candidate return, fatal group exit,
coredump deferral, and the real-blocked fatal path.

Every boot also ran preparation and locked queue insertion suites. Every log
reported no failed case, BUG, warning, KASAN report, lockdep complaint, oops,
or panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck``, Rustdoc, and Clippy. Focused Sphinx generated this page with
only the existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The
complete change passed ``git diff --check`` and strict ``checkpatch`` with zero
errors, warnings, or checks.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``complete_signal()``. Native ``kernel/signal.o``
contains an undefined reference and no implementation. The Rust locked queue
insertion path calls the internal completion state machine directly. The
production image contains no ``rust_complete_signal`` wrapper, no export table
entry, and no unresolved symbol. The image boots to the expected missing
root-filesystem panic without an earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* A helper starts to select notification policy or operation order.
* Signal classification or signal-set layout changes.
* Thread-group target balancing rules change.
* The ``sighand->siglock`` contract changes.
* The native Rust wakeup selection interface changes.
