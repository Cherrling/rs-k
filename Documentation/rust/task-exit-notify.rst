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

The local ``exit_notify()`` entry point remains in ``kernel/exit.c``. When the
option is enabled, it calls ``rust_task_exit_notify()`` and the original C
state machine is compiled out.

Rust runs the complete production state machine. The C adapter contains one
direct call to Rust. Rust does not call the original ``exit_notify()`` or any
of its local helper functions.

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
* Dead-task release through ``release_task()``.

Rust selects when each operation runs and whether each conditional branch is
entered. The retained helpers preserve the lock, RCU, signal, and process
hierarchy contracts of the original C implementation.

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

Each KUnit configuration ran three consecutive times with four vCPUs,
PREEMPT_RT, and lockdep. The Rust and KASAN configurations ran four cases.
The C fallback ran the two shared cases.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust task exit notification
     - 3
     - 4 of 4
     - Pass
   * - C task exit notification fallback
     - 3
     - 2 of 2
     - Pass
   * - Generic inline KASAN with Rust
     - 3
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
death-signal choices. Every KUnit log reported no BUG, warning, KASAN report,
Oops, refcount report, lockdep complaint, or kernel panic.

The production build used Clippy with KUnit, KASAN, and ``PROVE_LOCKING``
disabled. PREEMPT_RT remained enabled. The C adapter contains a direct
relocation to ``rust_task_exit_notify``. The Rust symbol is present, KUnit
symbols are absent, and ``llvm-nm -u vmlinux`` reports no unresolved symbols.

The production image starts with four vCPUs under KVM. Task creation and early
process setup complete before the image reaches the expected missing
root-filesystem panic. The boot log contains no additional diagnostic.

A second production build disabled ``CONFIG_PID_NS``. It passed Clippy,
linked without objtool warnings, kept the direct Rust call, and had no
unresolved symbols. The image reached the same expected boot panic without an
additional diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same lock, RCU, intrusive list,
  signal, or PID namespace primitives.
* A helper starts to select exit policy or operation order.
* Process hierarchy, thread-group, or child-subreaper rules change.
* The PID namespace teardown lock-drop protocol changes.
* ``do_notify_parent()`` or ``release_task()`` gains a Rust-native
  implementation.
