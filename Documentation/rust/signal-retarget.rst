.. SPDX-License-Identifier: GPL-2.0

Native Rust Shared Signal Retargeting
=====================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_RETARGET`` option selects native Rust for the
production ``retarget_shared_pending()`` function in ``kernel/signal.c``. The
original C implementation remains available as a fallback.

This unit covers shared pending signal filtering and thread-group retargeting.
It excludes signal delivery, signal queue management, group-stop handling, and
the surrounding ``exit_signals()`` and ``__set_task_blocked()`` state machines.
It does not change the C ABI.

The Rust code owns these decisions and transitions:

1. Group-exit early return.
2. Intersection of shared pending signals and caller-selected signals.
3. Empty-set early return.
4. Thread-group traversal after the caller task.
5. Exiting-thread filtering.
6. Blocked-thread filtering.
7. Removal of signals handled by each target.
8. Wakeup selection and early completion.

Entry points and ownership
--------------------------

The local ``retarget_shared_pending()`` function remains in ``kernel/signal.c``.
When the option is enabled, it calls ``rust_signal_retarget_pending()`` and the
original C state machine is compiled out.

Rust owns the complete production algorithm. The C adapter contains one direct
call to Rust. Rust does not call the original state machine.

Rust uses narrow C helpers for individual operations. One helper returns the
next thread, one helper reads the thread pending flag, and one helper wakes a
target thread. The helpers do not select policy or operation order.

Retained C boundary
-------------------

The following operations remain in C because they use task or architecture
primitives:

* ``next_thread()`` traversal of the shared thread list.
* ``task_sigpending()`` inspection of the architecture thread flag.
* ``signal_wake_up()`` wakeup and process kick.

The callers retain ``sighand->siglock`` across the complete retarget operation.
Rust preserves that locking requirement and does not sleep.

Ordering and invariants
-----------------------

Rust first checks ``SIGNAL_GROUP_EXIT``. A dying thread group keeps shared
pending signals because no thread will dequeue them.

Rust intersects ``shared_pending.signal`` with the caller-provided signal set.
It then starts traversal at ``next_thread(task)`` and stops when traversal
returns to the starting task.

For each thread, Rust skips ``PF_EXITING`` threads and threads that block every
remaining signal. For an eligible thread, Rust removes its blocked signals from
the remaining set. Rust calls ``signal_wake_up()`` only when the target has no
pending signal flag. The traversal stops when no signal remains.

Validation record
-----------------

Each KUnit configuration ran three consecutive times with four vCPUs and
1536 MiB of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust shared signal retargeting
     - 3
     - 1 of 1
     - Pass
   * - C shared signal retargeting fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust shared signal retargeting with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust shared signal retargeting with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust shared signal retargeting with lockdep
     - 3
     - 1 of 1
     - Pass

The integration test creates a real shared thread group with one leader and
three workers. It calls the production retarget entry under the signal-handler
lock.

The wake case mixes two shared pending signals with an exiting thread, a fully
blocked thread, and a target that blocks only one signal. The test verifies that
only the eligible target is woken and that Rust records each skip and selection
branch.

Additional cases verify group-exit early return, empty signal intersection, and
the no-duplicate-wakeup path when the target already has a pending signal.

Every KUnit log reported no BUG, warning, KASAN report, Oops, refcount report,
lockdep complaint, or kernel panic.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The complete change
passed strict ``checkpatch`` and ``git diff --check``.

The production image disabled KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` showed direct relocations from ``exit_signals()`` and
``__set_task_blocked()`` to ``rust_signal_retarget_pending``.

The production ``vmlinux`` contained the Rust retarget symbol, contained no
signal retarget test symbols, and had no undefined symbols. The image booted
to the expected missing root-filesystem panic and reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for thread traversal or signal wakeup.
* A helper starts to select retarget policy or operation order.
* Thread-group list or signal pending-flag rules change.
* The ``sighand->siglock`` contract changes.
* ``exit_signals()`` or ``__set_task_blocked()`` gains a Rust-native
  implementation.
