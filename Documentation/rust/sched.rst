.. SPDX-License-Identifier: GPL-2.0

Native Rust Scheduler Wake State
================================

Status and scope
----------------

The ``CONFIG_RUST_SCHED_WAKE_STATE`` option selects native Rust state matching
for task wakeups and inactive-task waits. The existing ``struct task_struct``
layout and scheduler entry points remain unchanged.

Rust owns these production decisions:

* Matching the requested wake mask against the current task state.
* Falling back to the saved task state when the current state does not match.
* Giving a current-state match precedence over a saved-state match.
* Reporting wake success independently from whether the task can run now.
* Clearing a matched saved state to ``TASK_RUNNING``.

Task-state memory accesses, warning delivery, task and run-queue locking,
wake-list handling, CPU selection, task enqueue, preemption, statistics, and
public scheduler ABI adapters remain in C.

Entry points and ownership
--------------------------

``kernel/sched/core.c`` keeps the scheduler-facing ``__task_state_match()``
and ``ttwu_state_match()`` adapters. When the Rust option is enabled, the task
state result and wake-state transition come from
``rust/kernel/sched_core.rs``.

The Rust-to-C helper calls are:

* Task current-state and saved-state ``READ_ONCE`` accesses.
* The saved-state write.
* The invalid debug-preemption wake-mask warning.

The C-to-Rust production entries are:

* ``rust_sched_task_state_match()``.
* ``rust_sched_ttwu_state_match()``.

The caller keeps the existing ``p->pi_lock`` serialization, or the current-task
preemption exclusion, around the production entry. Rust does not acquire or
release scheduler locks.

Retained C boundary
-------------------

Task-state accesses remain in C because ``READ_ONCE`` and the task layout are
defined by the C scheduler ABI. The helper reads current and saved state only.
Rust receives values and performs the matching decision.

The saved-state write remains in C to preserve the existing task-structure
access. Rust decides when the write is required.

``WARN_ON_ONCE`` remains in C because report formatting and rate limiting
depend on kernel diagnostics. Rust decides whether the debug-preemption state
mask is invalid.

``try_to_wake_up()`` keeps all locking, memory barriers, wake-list handling,
CPU selection, task migration, enqueue, preemption, and accounting in C. This
unit changes only the state-match gate and saved-state transition.

Important invariants
--------------------

A current-state match returns positive and wakes the task.

A saved-state-only match returns negative, reports wake success, and clears
the saved state to ``TASK_RUNNING``. The task becomes runnable only when the
lock or freezer restore completes.

No match returns zero and reports wake failure.

When both states match, the current state wins and the saved state is not
changed.

Under ``CONFIG_DEBUG_PREEMPT``, a wake mask that contains
``TASK_RTLOCK_WAIT`` with any other state bit presents an invalid
``TASK_NORMAL`` and must issue the existing warning.

Validation gate
---------------

The Rust KUnit cases cover the state-match table and saved-state transition.
Real wakeup cases cover current-task rejection and waking a stopped kernel
thread with both the Rust and C implementations.

The unit must pass repeated four-vCPU Rust runs, a C fallback run, PREEMPT_RT,
KASAN, lockdep, and a no-KUnit production build. Static checks include Rustfmt,
Clippy, strict checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The wake-state unit was validated on 2026-11-12 with x86_64 ``LLVM=1`` builds
and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all four wake-state cases.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive four-vCPU C fallback runs passed both real wakeup cases
  with ``CONFIG_RUST_SCHED_WAKE_STATE`` disabled.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all four wake-state cases and all twenty-five hrtimer
  regression cases.
* The no-KUnit production image built with Clippy while the Rust wake-state
  option was enabled.
* The final Rust image contained no unresolved symbols and no run reported a
  ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.
* ``try_to_wake_up()`` calls ``rust_sched_ttwu_state_match()``.
  ``wait_task_inactive()`` calls ``rust_sched_task_state_match()``. Rust calls
  the current-state and saved-state read helpers and calls the saved-state
  write helper only after a saved-state match.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* Task current-state or saved-state representation changes.
* ``try_to_wake_up()`` changes the state-match gate or saved-state protocol.
* Debug-preemption wake-mask validation changes.
* Rust gains stable task-state accessors and kernel warning abstractions.
