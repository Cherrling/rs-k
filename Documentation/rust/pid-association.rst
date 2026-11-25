.. SPDX-License-Identifier: GPL-2.0

Native Rust PID Task Association
================================

Status and scope
----------------

The ``CONFIG_RUST_PID_ASSOCIATION`` option selects native Rust for the
production PID task association state machine.  The original C implementation
remains available as a fallback.

This unit covers task attachment, detachment, PID replacement, PID transfer,
TID exchange, orphan detection, PID stashing, duplicate-stash diagnostics, and
cached task PID updates.  It does not change the public C ABI.

The Rust code owns these state transitions and decisions:

1. Order each attachment, detachment, replacement, and transfer transition.
2. Scan every PID type before declaring an association orphaned.
3. Decide whether an orphan must be returned to the caller.
4. Detect a duplicate PID stash and request its diagnostic.
5. Drive PID slot and cached task PID updates during TID exchange.
6. Detect an invalid PID transfer request and request its diagnostic.

Entry points and ownership
--------------------------

Rust directly defines the production ``attach_pid()``, ``detach_pid()``,
``change_pid()``, ``exchange_tids()``, and ``transfer_pid()`` entries.  The
native build compiles the original C state machine out of ``kernel/pid.c``.
The C fallback and C-only configurations retain the complete implementation.

The Rust module keeps all state-transition policy.  C helpers perform only
primitive operations on kernel structures.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel or
subsystem primitives:

* ``tasklist_lock`` acquisition by callers.
* The lockdep task-list write assertion.
* Task PID slot selection through the existing ``task_pid_ptr()`` helper.
* Task link and PID list head address calculation.
* RCU head insertion, deletion, replacement, and exchange.
* ``rcu_assign_pointer()`` and ``WRITE_ONCE()``.
* ``pid_nr()`` and PID stash pointer access.
* The kernel warning primitives.

The helpers do not choose attachment order, orphan status, stash behavior, or
which fields to update.

Ordering and invariants
-----------------------

Every production entry point asserts that the caller holds ``tasklist_lock``
for writing before it changes association state.

Attachment runs after a caller has made the selected PID slot current.  The
task association node is linked to the matching PID task list while the write
lock remains held.

Detachment first unlinks the old association and installs the replacement PID
slot.  Rust then scans every PID type.  An association is orphaned only when
the old PID has no task in any type.  The caller retains an orphan through the
PID stash so its lifetime is not lost.

A duplicate stash is invalid state.  Rust requests the established diagnostic
before replacing the existing stash entry.

TID exchange preserves the original sequence.  Rust swaps the two single-entry
PID lists, updates both task PID slots with RCU assignment, and then updates
both cached task PIDs.  The task list write lock covers the complete exchange.

PID transfer preserves the original warning for ``PIDTYPE_PID``.  After the
diagnostic decision, Rust replaces the old task RCU list node with the new node.

Validation record
-----------------

The x86_64 validation used the final source for every KUnit configuration.
KASAN used Clang, ``LLVM=1``, and generic inline KASAN.

.. list-table::
   :widths: 38 24 16 22
   :header-rows: 1

   * - Configuration
     - Boots
     - Cases
     - Result
   * - Native
     - 3 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Rust fallback
     - SMP4, SMP1
     - 5 of 5
     - Pass
   * - C-only
     - SMP4, SMP1
     - 5 of 5
     - Pass
   * - KASAN with native
     - SMP4, SMP1
     - 5 of 5
     - Pass
   * - Lockdep with native
     - SMP4, SMP1
     - 5 of 5
     - Pass
   * - PREEMPT_RT with native
     - SMP4, SMP1
     - 5 of 5
     - Pass

The tests cover attach and detach, PID replacement and stash, orphan detection
through another PID type, TID and cached PID exchange, and association
transfer.

Every KUnit log reported zero failures and no BUG, warning, KASAN report,
Oops, refcount report, or lockdep complaint.

The native SMP4 regression image enabled the PID lifecycle, PID allocation,
PID number, zombie task release, signal-aware exit, and task exit notification
suites.  The adjacent suites passed 25 of 25 cases.

Native ``rust/kernel.o`` defines all five entries and native ``kernel/pid.o``
defines none.  The linked image contains one definition of each entry, no
``rust_pid_*`` wrapper, no export-table entry, and no unresolved symbol.

The production build used Clippy and produced both ``bzImage`` and modules.
The production image starts with four vCPUs under KVM.  PID allocation, task
association, process setup, and early scheduling complete before the image
reaches the expected missing-root-filesystem panic.  The boot log contains no
unexpected BUG, warning, KASAN report, Oops, refcount report, or lockdep
complaint.

A second production configuration disabled ``CONFIG_PID_NS`` while it kept the
Rust association implementation enabled.  It passed the Clippy build, linked
with no unresolved symbols, kept all five entries defined by Rust, and reached
the same expected missing-root-filesystem panic without an unexpected
diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for RCU hlist operations or task PID
  slots.
* A helper starts to choose association policy or orphan handling.
* PID type ordering or ``PIDTYPE_MAX`` semantics change.
* The task list locking or RCU publication protocol changes.
* PID stash ownership or duplicate-stash diagnostics change.
