.. SPDX-License-Identifier: GPL-2.0

Native Rust Zombie Task Release
===============================

Status and scope
----------------

The ``CONFIG_RUST_TASK_RELEASE`` option selects native Rust for the production
zombie task release path. The original C implementation remains available as
a fallback.

This unit covers ``release_task()``, ``__exit_signal()``, and
``__unhash_process()``. It excludes task exit entry, parent choice, zombie
creation, final task reference-count operations, architecture release hooks,
and subsystem cleanup implementations. It does not change the public C ABI.

The Rust code owns these decisions and transitions:

1. Release preparation and task-list locking order.
2. Signal-structure statistics updates and child-group notification.
3. PID, process, sibling, and thread-list removal.
4. Sighand publication and cleanup order.
5. Group-leader notification and exit-code selection.
6. Zap-leader detection and repeated release.
7. PID, credential-namespace, signal-queue, and task-reference release order.

Entry points and ownership
--------------------------

The exported C ``release_task()`` entry point remains in ``kernel/exit.c``.
When the option is enabled, it calls ``rust_task_release()`` and the original
C release state machine is compiled out.

Rust runs ``release_task()``, signal exit, and PID and thread-list unhashing in
one implementation. The C adapter contains one direct call to Rust. The Rust
code does not call the original ``release_task()``, ``__exit_signal()``, or
``__unhash_process()`` implementations.

The Rust module uses narrow C helpers for individual operations. A helper
performs one lock, atomic, RCU, list, accounting read, subsystem call, or
architecture hook. The helpers do not choose release policy or operation
order.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel,
architecture, or subsystem primitives:

* ``tasklist_lock`` and ``sighand->siglock`` acquisition and lockdep checks.
* RCU dereference and release publication.
* PID detach and PID lifetime release calls.
* Task and signal list operations.
* Per-CPU process and global thread counters.
* POSIX CPU-timer hooks and CPU accounting reads.
* Task I/O accounting reads and accumulation.
* TTY, sighand, signal queue, cgroup, pidfs, credential namespace, ptrace,
  randomness, and architecture release operations.
* ``do_notify_parent()`` and delayed task-reference release.

These operations have kernel-internal lock, RCU, architecture, or subsystem
contracts. Rust selects when each operation runs and whether each conditional
branch is entered.

Ordering and invariants
-----------------------

The caller must supply a dead task that is ready for ``release_task()``.
Rust first releases the process limit, pidfs, and cgroup state. It pins the
task PID before unhashing so ``proc_flush_pid()`` can run after
``tasklist_lock`` is released.

Rust takes ``tasklist_lock`` for writing, releases ptrace state, and enters
signal cleanup. It holds ``sighand->siglock`` while it accumulates CPU and I/O
statistics and removes the task from PID and thread-group lists. It publishes
the cleared ``sighand`` pointer before unlocking.

The group-leader path clears the TTY and releases its final signal resources.
The non-leader path updates group-exit notification and current-target state.
Rust removes every PID association before releasing the temporary PID arrays.

If the released task is the last non-leader thread and the leader is a zombie,
Rust updates the leader exit code, calls ``do_notify_parent()``, and marks the
leader ``EXIT_DEAD`` when the parent does not reap it. Rust then repeats the
complete release path for that leader.

Validation record
-----------------

Each KUnit configuration ran three consecutive times with four vCPUs and
PREEMPT_RT. The Rust, fallback, and KASAN configurations also enabled lockdep.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust task release
     - 3
     - 2 of 2
     - Pass
   * - C task release fallback
     - 3
     - 1 of 1
     - Pass
   * - Generic inline KASAN with Rust
     - 3
     - 2 of 2
     - Pass
   * - Rust without ``PID_NS`` and ``POSIX_TIMERS``
     - 3
     - 2 of 2
     - Pass

The integration test creates, starts, stops, and reaps 24 kernel threads. It
waits for RCU callbacks and checks that the Rust production entry was called.
The policy tests cover all group-leader notification predicates and both
exit-code selections.

Every KUnit log reported no BUG, warning, KASAN report, Oops, refcount report,
lockdep complaint, or kernel panic.

The production build used Clippy with KUnit, KASAN, and ``PROVE_LOCKING``
disabled. PREEMPT_RT remained enabled. The C adapter contains a direct
relocation to ``rust_task_release``. The Rust symbol is present, KUnit symbols
are absent, and ``llvm-nm -u vmlinux`` reports no unresolved symbols.

The production image starts with four vCPUs under KVM. Task creation and early
process setup complete before the image reaches the expected missing
root-filesystem panic. The boot log contains no additional diagnostic.

A second production build disabled ``CONFIG_PID_NS`` and
``CONFIG_POSIX_TIMERS``. It passed Clippy, linked with the same direct Rust
call and no unresolved symbols, and reached the same expected boot panic
without an additional diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same lock, RCU, accounting, or
  list primitives.
* A helper starts to select release policy or operation order.
* Task or signal ownership rules change.
* The zap-leader repetition protocol changes.
* Architecture release hooks gain a Rust-native implementation.
* ``do_notify_parent()`` or delayed task-reference release moves to Rust.
