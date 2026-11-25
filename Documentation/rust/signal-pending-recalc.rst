.. SPDX-License-Identifier: GPL-2.0

Native Rust Pending Signal Recalculation
========================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_PENDING_RECALC`` option selects native Rust for the
production pending-signal recalculation state machine. The original C logic
remains available as a fallback.

This unit covers ``recalc_sigpending_tsk()``, ``recalc_sigpending()``, and
``calculate_sigpending()``. It includes job-control pending and freeze traps,
local pending signals, shared pending signals, blocked signals, cgroup
freezer state, PM and cgroup1 freezer state, setting ``TIF_SIGPENDING``,
clearing a stale current-task flag, and the forked-task lock wrapper.

It excludes signal queue mutation, signal selection, signal delivery,
thread-flag implementation, cgroup freezer transitions, and the freezer
state machine.

Rust owns these decisions and transitions:

1. Testing job-control pending and freeze-trap bits.
2. Testing local pending signals against the blocked set.
3. Testing shared pending signals against the blocked set.
4. Querying cgroup freezer state and setting ``TIF_SIGPENDING``.
5. Avoiding a clear while the current task is being frozen.
6. Clearing a stale current-task flag only after all set reasons are absent.
7. Leaving another task's flag untouched in the task-only entry.
8. Acquiring and releasing the current signal-handler lock around the
   forked-task recalculation.

Entry points and ownership
--------------------------

Rust directly defines ``recalc_sigpending_tsk()``, ``recalc_sigpending()``,
and ``calculate_sigpending()``. The native build compiles their original C
implementations out of ``kernel/signal.c``. Rust owns the complete reason
combination, set or clear order, and fork lock sequence. No C helper combines
signal sets, job-control state, or freezer state.

``recalc_sigpending()`` keeps its original ordinary export.
``recalc_sigpending_tsk()`` and ``calculate_sigpending()`` remain unexported.

Retained C boundary
-------------------

The following operations remain in C:

* ``TIF_SIGPENDING`` set, clear, and test operations.
* ``cgroup_task_frozen()``.
* ``freezing()``.
* Signal-handler lock and interrupt-mode primitives.
* The ordinary export metadata.

These operations isolate architecture thread flags and freezer subsystem
state. The retained boundary does not select whether the pending flag should
be set or cleared.

The caller must hold ``task->sighand->siglock``. The full recalculation entry
requires its task argument to be ``current`` because only current may clear
its own flag during syscall restart processing.

Ordering and invariants
-----------------------

The task-only entry sets ``TIF_SIGPENDING`` when any job-control, pending-signal,
or cgroup-freezer reason exists. It never clears the flag.

The full entry first applies the same set reasons. If none exist and the
current task is being frozen, Rust leaves the flag unchanged. Otherwise Rust
clears an existing flag and reports the transition to test counters.

The forked-task entry acquires ``current->sighand->siglock`` with interrupts
disabled, sets ``TIF_SIGPENDING``, runs the full recalculation, and releases
the lock. The lock covers the complete set-and-recalculate sequence.

Validation record
-----------------

Each boot used 1536 MiB of guest memory. Native completed three four-vCPU
runs and one single-vCPU run. Every other configuration completed one run at
each CPU count.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Boots
     - Groups
     - Result
   * - Native Rust
     - 3 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - C fallback
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with KASAN
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with lockdep
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass

The integration test exercises local and shared pending signals, blocked
signals, both job-control reason classes, cgroup freezer state, stale flag
clearing, the task-only no-clear contract, and the forked-task lock wrapper.
Rust-only counters verify each transition.

Every boot also ran ``complete_signal``, ``send_signal``,
``prepare_signal``, and ``signal_dequeue`` with the selected recalculation
configuration.

Every KUnit log reported no failed case, BUG, warning, KASAN report, lockdep
complaint, or kernel panic.

Build and production checks
---------------------------

The production build passes with ``CONFIG_WERROR=y`` and Clippy. The module
build completes modpost without an error. The Rust source passes
``rustfmtcheck`` and Rustdoc. Focused Sphinx generates this page with only the
existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The complete
change passes ``git diff --check`` and strict ``checkpatch`` with zero errors,
warnings, or checks.

The production image disables KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``recalc_sigpending_tsk()``,
``recalc_sigpending()``, and ``calculate_sigpending()``. Native
``kernel/signal.o`` contains undefined references and export metadata, not
the three implementations. Fallback and C-only ``kernel/signal.o`` files
define all three entries.

The native image keeps one ordinary export table entry for
``recalc_sigpending()``. The task-only and fork entries have no export table
entries. The old ``rust_signal_pending_recalc*`` wrapper names are absent, and
the production image has no unresolved symbols.

The production ``vmlinux`` contains no pending-signal recalculation test
symbols. The image boots to the expected missing root-filesystem panic and
reports no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for thread flags or freezer state.
* A helper starts to combine pending-signal, job-control, or freezer policy.
* Signal-set layout or blocked-set rules change.
* Job-control pending or freeze-trap definitions change.
* The ``sighand->siglock`` contract changes.
* ``recalc_sigpending()`` starts accepting a non-current task.
