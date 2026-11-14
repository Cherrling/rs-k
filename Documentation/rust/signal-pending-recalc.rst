.. SPDX-License-Identifier: GPL-2.0

Native Rust Pending Signal Recalculation
========================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_PENDING_RECALC`` option selects native Rust for the
production pending-signal recalculation state machine. The original C logic
remains available as a fallback.

This unit covers the policy in ``recalc_sigpending_tsk()`` and
``recalc_sigpending()``. It includes job-control pending and freeze traps,
local pending signals, shared pending signals, blocked signals, cgroup
freezer state, PM and cgroup1 freezer state, setting
``TIF_SIGPENDING``, and clearing a stale current-task flag.

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

Entry points and ownership
--------------------------

The local ``recalc_sigpending_tsk()`` and exported ``recalc_sigpending()``
functions remain in ``kernel/signal.c`` as ABI adapters. When the option is
enabled, their original policy is compiled out and each adapter calls the
corresponding Rust entry.

Rust owns the complete reason combination and set or clear order. No C helper
combines signal sets, job-control state, or freezer state.

Retained C boundary
-------------------

The following operations remain in C:

* ``TIF_SIGPENDING`` set, clear, and test operations.
* ``cgroup_task_frozen()``.
* ``freezing()``.
* The public function and symbol adapters.

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

Validation record
-----------------

Each KUnit configuration runs three consecutive times with four vCPUs and
1536 MiB of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust pending signal recalculation
     - 3
     - 1 of 1
     - Pass
   * - C pending signal recalculation fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal recalculation with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal recalculation with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal recalculation with lockdep
     - 3
     - 1 of 1
     - Pass

The integration test exercises local and shared pending signals, blocked
signals, both job-control reason classes, cgroup freezer state, stale flag
clearing, and the task-only no-clear contract. Rust-only counters verify each
transition.

The existing signal tests also run with the Rust implementation enabled. A
combined run passed ``complete_signal``, ``send_signal``,
``prepare_signal``, ``signal_dequeue``, and ``signal_pending_recalc``.

Every KUnit log reported no failed case, BUG, warning, KASAN report, lockdep
complaint, or kernel panic.

Build and production checks
---------------------------

The final Rust source passed ``rustfmtcheck`` and Clippy. The Rust
documentation build generated this page. Its only warnings were existing
unresolved Rust documentation references and a missing ``dot`` program.
The complete change passed ``git diff --check`` and strict ``checkpatch``
with no errors or checks. Checkpatch reported its generic new-file warning
after ``MAINTAINERS`` was updated with the new entries.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``System.map`` contained ``rust_signal_pending_recalc`` and
``rust_signal_pending_recalc_tsk``. Disassembly showed a direct jump from
``recalc_sigpending()`` to ``rust_signal_pending_recalc()`` and a direct call
from ``calculate_sigpending()`` to the same Rust entry.

The production ``vmlinux`` contained no pending-signal recalculation test
symbols and had no undefined symbols. The image booted to the expected
missing root-filesystem panic and reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for thread flags or freezer state.
* A helper starts to combine pending-signal, job-control, or freezer policy.
* Signal-set layout or blocked-set rules change.
* Job-control pending or freeze-trap definitions change.
* The ``sighand->siglock`` contract changes.
* ``recalc_sigpending()`` starts accepting a non-current task.
