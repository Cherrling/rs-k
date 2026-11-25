.. SPDX-License-Identifier: GPL-2.0

Native Rust Task Exit Notification
==================================

Status and scope
----------------

The ``CONFIG_RUST_TASK_EXIT_NOTIFY`` option selects native Rust for the
production task exit notification path. The original C implementation remains
available as a fallback.

This unit covers ``exit_notify()``, ``find_child_reaper()``,
``find_new_reaper()``, ``forget_original_parent()``, ``reparent_leader()``, and
the release order for tasks accumulated on the local dead-task list. It
excludes process exit entry, final task release, PID namespace teardown,
ptrace policy, signal delivery, and process-group cleanup.

The Rust code owns these decisions and transitions:

1. Child-reaper selection for a dying PID namespace reaper.
2. Child-parent selection across thread groups and child subreapers.
3. Reparenting every thread in each child thread group.
4. Parent-death signal and inconsistent-parent checks.
5. Zombie notification and final child-list transfer.
6. Ptrace, group-leader, and untraced-thread notification selection.
7. Autoreap decisions and dead-task release ordering.

Entry points and ownership
--------------------------

Rust directly defines ``exit_notify()``. The native build compiles the
original C state machine out of ``kernel/exit.c``. The C fallback and C-only
configurations retain the complete implementation.

Rust runs the complete production state machine. Rust does not call the
original ``exit_notify()`` or any of its local helper functions.

The Rust module uses narrow C helpers for individual operations. A helper
performs one lock, RCU, intrusive list, signal, ptrace, notification, wakeup,
or PID namespace primitive. The helpers do not select policy or operation
order.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel,
architecture, or subsystem primitives:

* ``tasklist_lock`` acquisition, release, and release while dropping the lock.
* Intrusive task-list traversal and splice operations.
* RCU publication of the real-parent pointer.
* Thread-group membership and emptiness checks.
* PID namespace lookup, child-reaper publication, and namespace teardown.
* Ptrace transfer, notification, and parent consistency checks.
* Parent-death and zombie signal delivery through ``do_notify_parent()``.
* Orphaned process-group cleanup.

Rust selects when each operation runs and whether each conditional branch is
entered. The retained helpers preserve the lock, RCU, signal, and process
hierarchy contracts of the original C implementation.

The dead-task list enters the Rust task-release state machine directly. C does
not provide an adapter or policy decision between exit notification and final
task release.

Ordering and invariants
-----------------------

Rust initializes a local dead-task list and takes ``tasklist_lock`` for
writing. It transfers ptraced tasks before selecting the child reaper. If the
exiting task is the child reaper for its namespace, Rust selects a live thread
or performs PID namespace teardown with the original lock-drop sequence.

Rust selects the new reaper in this order: a live thread in the exiting thread
group, the nearest child-subreaper ancestor in the same PID namespace, or the
PID namespace child reaper. It updates every child thread to the selected
reaper before moving each child leader's sibling link.

The exiting task becomes a zombie only after its children are transferred.
Rust then selects the ptrace, group-leader, or untraced-thread notification
path. Tasks marked for autoreap join the dead-task list. Rust releases
``tasklist_lock`` before releasing any task on that list.

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
   * - Rust task exit notification
     - 3 SMP4, 1 SMP1
     - 1 of 1
     - 4 of 4
     - Pass
   * - C task exit notification fallback
     - 1 SMP4, 1 SMP1
     - 1 of 1
     - 2 of 2
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 1 of 1
     - 2 of 2
     - Pass
   * - Rust task exit notification with KASAN
     - 1 SMP4, 1 SMP1
     - 1 of 1
     - 4 of 4
     - Pass
   * - Rust task exit notification with lockdep
     - 1 SMP4, 1 SMP1
     - 1 of 1
     - 4 of 4
     - Pass
   * - Rust task exit notification with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 1 of 1
     - 4 of 4
     - Pass

The integration tests start and stop 24 kernel threads, then create a nested
kernel-thread child. The parent exits before the child, so the test exercises
the production child-reparent path. The child explicitly uses
``kthread_exit()`` so a raw ``kernel_thread()`` callback does not return
through the user-mode FPU exit path.

A PID namespace test creates a namespace init process and a live thread in the
same thread group. The namespace init exits first and selects the live thread
as the new child reaper. When the last thread exits, it performs the namespace
teardown path. The test reaps the namespace init and checks both production
transitions.

The policy tests cover all autoreap branch selections and the ptrace
death-signal choices. Native adjacent runs also passed signal-aware exit,
task thread-group exit synchronization, and zombie task release.

Every focused log reported no failed case, BUG, warning, KASAN report, Oops,
refcount report, lockdep complaint, or kernel panic.

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck`` and Rustdoc. Focused Sphinx generated this page with only the
existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The complete
change passed ``git diff --check`` and strict ``checkpatch``.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``exit_notify()``. Native ``kernel/exit.o``
contains only an undefined reference. The production image contains no
``rust_task_exit_notify`` wrapper. The entry has no export table entry, which
preserves its original unexported class. The production image has no
unresolved symbol.

The production image starts with four vCPUs under KVM. Task creation and early
process setup complete before the image reaches the expected missing
root-filesystem panic. The boot log contains no additional diagnostic.

A second production build disabled ``CONFIG_PID_NS``. It passed
``CONFIG_WERROR=y`` and Clippy, preserved the same symbol ownership, linked
without unresolved symbols, and reached the same expected boot panic without
an additional diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same lock, RCU, intrusive list,
  signal, or PID namespace primitives.
* A helper starts to select exit policy or operation order.
* Process hierarchy, thread-group, or child-subreaper rules change.
* The PID namespace teardown lock-drop protocol changes.
* The dead-task list stops entering the native Rust ``release_task()`` state
  machine.
