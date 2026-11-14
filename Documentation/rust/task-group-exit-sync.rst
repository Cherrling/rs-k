.. SPDX-License-Identifier: GPL-2.0

Native Rust Task Group Exit Synchronization
===========================================

Status and scope
----------------

The ``CONFIG_RUST_TASK_GROUP_EXIT_SYNC`` option selects native Rust for the
production task thread-group exit synchronization path. The original C
implementation remains available as a fallback.

This unit covers ``synchronize_group_exit()`` and the state that it changes in
``do_exit()``. It excludes signal delivery, exit-notification policy, task
release, and the coredump state machine. It does not change the C ABI.

The Rust code owns these decisions and transitions:

1. Quick-thread count convergence.
2. Group-exit publication when the final quick thread exits.
3. Group exit-code publication and stop-count clearing.
4. Post-coredump marking.
5. Coredump entry selection.

Entry points and ownership
--------------------------

The local ``synchronize_group_exit()`` function remains in ``kernel/exit.c``.
When the option is enabled, it calls ``rust_task_group_exit_sync()`` and the
original C state machine is compiled out.

Rust runs the complete production state machine. The C adapter contains one
direct call to Rust. Rust does not call the original
``synchronize_group_exit()`` implementation.

Rust uses narrow C helpers for individual operations. One helper acquires the
signal-handler lock, one helper releases it, and one helper enters the active
coredump synchronization primitive. The helpers do not select policy or
operation order.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel
synchronization or scheduling primitives:

* Acquisition and release of ``sighand->siglock`` with interrupts disabled.
* Active coredump synchronization through ``coredump_task_exit()``.

Rust selects when each operation runs and whether each conditional branch is
entered. The retained helpers preserve the lock and sleep contracts of the
original C implementation.

Ordering and invariants
-----------------------

Rust decrements ``signal->quick_threads`` while it holds
``sighand->siglock``. If the count reaches zero and a group exit is not already
published, Rust sets ``SIGNAL_GROUP_EXIT``, publishes the exit code, and clears
the group stop count.

Rust marks the exiting task with ``PF_POSTCOREDUMP`` and reads
``signal->core_state`` while it still holds the signal-handler lock. It
releases the lock before it enters ``coredump_task_exit()``, because that
primitive can sleep.

Validation record
-----------------

Each KUnit configuration ran three consecutive times with four vCPUs and
1536 MiB of guest memory. The Rust baseline also ran with PREEMPT_RT, generic
inline KASAN, and lockdep.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust task group exit synchronization
     - 3
     - 3 of 3
     - Pass
   * - C task group exit synchronization fallback
     - 3
     - 1 of 1
     - Pass
   * - Generic inline KASAN with Rust
     - 3
     - 3 of 3
     - Pass
   * - PREEMPT_RT with Rust
     - 3
     - 3 of 3
     - Pass
   * - Lockdep with Rust
     - 3
     - 3 of 3
     - Pass
   * - Rust without ``COREDUMP``
     - 3
     - 3 of 3
     - Pass

The first integration case starts and stops 16 kernel threads. It checks that
the Rust production entry runs and that final quick-thread exits publish a
group exit.

The second integration case creates a real shared thread group. The worker
exits first and exercises the non-final quick-thread path. The leader exits
second and exercises group-exit publication. The test reaps the leader before
it finishes.

The selector case covers every group-exit publication condition and both
coredump entry choices. Every KUnit log reported no BUG, warning, KASAN
report, Oops, lockdep complaint, or kernel panic.

The KUnit environment has no userspace or root filesystem. It therefore tests
the coredump selector but does not inject an active ``core_state``. A userspace
coredump harness should add that integration coverage in a later unit.

The production build used Clippy with KUnit, KASAN, and ``PROVE_LOCKING``
disabled. The C adapter contains a direct relocation to
``rust_task_group_exit_sync``. The Rust symbol is present, KUnit symbols are
absent, and ``llvm-nm -u vmlinux`` reports no unresolved symbols.

The production image starts with four vCPUs under KVM. Early process setup
completes before the image reaches the expected missing root-filesystem panic.
The boot log contains no additional diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains a safe abstraction for the signal-handler lock.
* A helper starts to select group-exit policy or operation order.
* Quick-thread accounting or group-exit flag rules change.
* The post-coredump ordering protocol changes.
* ``coredump_task_exit()`` gains a Rust-native implementation.
* A userspace coredump test harness becomes available.
