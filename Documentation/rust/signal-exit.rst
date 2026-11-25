.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal-Aware Task Exit
==================================

Status and scope
----------------

The ``CONFIG_RUST_EXIT_SIGNALS`` option selects native Rust for the production
``exit_signals()`` function. The option depends on
``CONFIG_RUST_SIGNAL_RETARGET``. The original C implementation remains
available as a fallback.

This unit covers the task exit transition that prevents further changes to the
thread group, retires task-local pending signals, moves shared pending signal
responsibility to another live thread, participates in a completed group stop,
and notifies the parent. It excludes final task release, parent reparenting,
signal delivery, and process exit before ``exit_signals()``.

The Rust code owns these decisions and transitions:

1. Entering the cgroup thread-group change guard.
2. Publishing ``PF_EXITING`` under the signal-handler lock.
3. Detaching every task-local queued signal for deferred release.
4. Selecting the shared pending signal retarget path.
5. Selecting group-stop participation and parent notification.
6. Releasing the thread-group guard before queued signal destruction.
7. Releasing queued signals without the signal-handler lock.
8. Notifying the real parent under ``tasklist_lock`` when required.

Entry points and ownership
--------------------------

Rust directly defines ``exit_signals()``. The native build compiles the
original C implementation out of ``kernel/signal.c``. Rust owns the operation
order and every conditional transition. The C fallback and C-only
configurations retain the complete implementation.

Rust directly calls the internal Rust shared pending signal retarget state
machine. The same state machine also remains available to
``__set_task_blocked()`` through its existing interface.

Retained C boundary
-------------------

The following operations remain in C:

* ``cgroup_threadgroup_change_begin()`` and the matching end operation.
* Signal-handler lock and interrupt-disable operations.
* Static ``sigqueue_dequeue_pending()`` and ``flush_sigqueue_list()``
  operations.
* Task pending-state, thread-group emptiness, and unblocked-mask queries.
* Static group-stop participation and accounting.
* ``tasklist_lock`` and parent stop notification.

The queue and group-stop helpers access static signal-core functions or
intrusive state that has no safe Rust abstraction. They perform one narrow
operation and do not select exit policy or operation order.

The caller must invoke ``exit_signals()`` for the current task immediately
before the task becomes unreachable from ordinary signal senders. The task,
its signal structure, and its signal-handler structure must remain live.

Ordering and invariants
-----------------------

Rust initializes the local retired-queue sentinel before entering the cgroup
thread-group change guard. Under ``sighand->siglock``, Rust sets
``PF_EXITING`` before it detaches the task-local pending queue.

Rust retargets shared pending signals only when the exiting task has
``TIF_SIGPENDING``, the thread group is not empty, and the group is not already
exiting. Group-stop participation occurs only after retargeting and only when
``JOBCTL_STOP_PENDING`` is set.

Rust releases the signal-handler lock before the thread-group change guard
ends. It destroys the retired queue only after both are released. A completed
group stop is reported after queue destruction, with ``tasklist_lock`` held
for the parent notification.

Validation record
-----------------

Each boot used 1536 MiB of guest memory. Native completed three four-vCPU
runs and one single-vCPU run. Every other configuration completed one run at
each CPU count.

.. list-table::
   :widths: 38 18 14 12 18
   :header-rows: 1

   * - Configuration
     - Boots
     - Groups
     - Cases
     - Result
   * - Rust signal-aware exit
     - 3 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass
   * - C signal-aware exit fallback
     - 1 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass
   * - Rust signal-aware exit with KASAN
     - 1 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass
   * - Rust signal-aware exit with lockdep
     - 1 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass
   * - Rust signal-aware exit with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 10 of 10
     - 13 of 13
     - Pass

The integration case creates a real shared thread group. An exiting worker
queues a task-local signal, publishes a shared pending signal, and starts a
group stop. A second worker observes the retargeted pending state. The test
verifies the interruptible wake result and every production transition.

The Rust-only counters verify the task flag update, queue detachment, retarget
selection, group-stop participation, and parent notification. The behavior
case runs with either the C or Rust implementation.

Additional native four-vCPU runs passed 3 of 3 task thread-group exit
synchronization cases and 4 of 4 task exit notification cases. Every focused
log reported no failed case, BUG, warning, KASAN report, lockdep complaint,
oops, or panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck`` and Rustdoc. Focused Sphinx generated this page with only the
existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The complete
change passed ``git diff --check`` and strict ``checkpatch``.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``exit_signals()``. Native ``kernel/signal.o``
contains neither an implementation nor a reference to the entry. Native
``kernel/exit.o`` contains a direct reference from ``do_exit()``.

The production image contains no ``rust_exit_signals`` wrapper. The
``exit_signals`` entry has no export table entry, which preserves its original
unexported class. The production image has no unresolved symbol and boots to
the expected missing root-filesystem panic without an earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for signal queues, signal sets, or
  intrusive lists.
* A helper starts to select exit policy or operation order.
* The ``sighand->siglock`` or cgroup thread-group guard contract changes.
* Signal queue destruction no longer requires the lock-free final sequence.
* Group-stop accounting or parent notification rules change.
* The internal Rust retarget interface changes.
