.. SPDX-License-Identifier: GPL-2.0

Native Rust Thread-Group Signal Structure Creation
==================================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_STRUCT_COPY`` option selects native Rust for the
production ``copy_signal()`` path in ``kernel/fork.c``. The original C
implementation remains available as a fallback.

This unit covers creation of ``struct signal_struct`` for a new thread group.
It includes the ``CLONE_THREAD`` short circuit, allocation, count and list
initialization, shared pending state, statistics, POSIX timers, inherited
resource limits and OOM scores, audit and autogroup setup, and execution
locks.

It excludes signal-handler structure creation, task-stack duplication,
address-space duplication, credentials, file tables, namespaces, and the
remaining ``copy_process()`` resource sequence.

Rust owns these decisions and transitions:

1. Detecting a new thread within an existing thread group.
2. Selecting the signal-structure allocation path.
3. Returning ``-ENOMEM`` when allocation fails.
4. Publishing the initial thread-group counts and self-linked thread list.
5. Initializing the child wait queue and shared pending signal state.
6. Initializing accounting, POSIX timer, audit, and autogroup state.
7. Copying inherited resource limits and OOM scores.
8. Initializing cgroup, credential, and execution locks.

Entry points and ownership
--------------------------

Rust directly defines the production ``copy_signal()`` entry. The native build
compiles the original C sequence out of ``kernel/fork.c``. C fallback builds
retain the complete static C function.

Rust owns the complete initialization order. It directly initializes simple
field values and the circular thread-group links. It calls narrow C helpers
for operations that use private caches, subsystem constructors, locking
primitives, or the parent task:

* Signal-structure cache allocation and publication.
* Atomic and reference-count initialization.
* Wait queue, pending signal, seqlock, and previous-CPU-time initialization.
* POSIX timer setup.
* Resource-limit inheritance under ``task_lock()``.
* POSIX CPU timer, TTY audit, autogroup, and cgroup initialization.
* OOM score inheritance.
* Credential and execution lock initialization.

These helpers perform one constructor, inherited read, or indivisible
locking operation. They do not select whether to create a signal structure or
the order of initialization.

Retained C boundary
-------------------

The following operations remain in C because they use private kernel caches,
subsystem internals, generated configuration fields, or established locking
and timer primitives:

* Access to ``signal_cachep``.
* Wait queue and pending signal constructors.
* POSIX timer and CPU timer constructors.
* TTY audit and scheduler autogroup hooks.
* Cgroup thread-group lock construction.
* Resource-limit copying under the group-leader task lock.
* OOM score reads from the parent signal structure.
* Credential and execution lock constructors.

The surrounding ``copy_process()`` error handling remains in C. A failed
allocation is visible as ``-ENOMEM`` and does not produce a partially
initialized thread group.

Ordering and invariants
-----------------------

The Rust sequence preserves the original order. Counts and the circular
thread list exist before wait queue, pending signal, accounting, timer, audit,
autogroup, limit, OOM, and lock initialization.

The newly allocated signal structure is zeroed before Rust initializes it.
The first task is the only thread in the group, its ``live`` and ``sigcnt``
counts are one, ``curr_target`` points to that task, and the shared pending set
is empty.

Resource limits are read while ``task_lock()`` protects the parent group
leader. OOM scores and limits are inherited before the new task can run.

Validation record
-----------------

The native configuration passed all three focused cases in three consecutive
SMP4 boots and one SMP1 boot. The fallback and C-only configurations each
passed both focused cases at SMP4 and SMP1. KASAN, lockdep, and PREEMPT_RT
each passed all three focused cases at both CPU counts.

.. list-table::
   :widths: 40 14 14 32
   :header-rows: 1

   * - Configuration
     - SMP4
     - SMP1
     - Focused cases
   * - Native Rust
     - 3
     - 1
     - 3 of 3
   * - C fallback
     - 1
     - 1
     - 2 of 2
   * - C-only
     - 1
     - 1
     - 2 of 2
   * - KASAN
     - 1
     - 1
     - 3 of 3
   * - Lockdep
     - 1
     - 1
     - 3 of 3
   * - PREEMPT_RT
     - 1
     - 1
     - 3 of 3

The kernel-thread integration case creates a new thread group through the
production ``copy_process()`` path. It checks the initialized thread list,
counts, wait queue, pending state, timers, limits, and OOM scores in the child.

The shared-entry case calls the production ``copy_signal()`` path directly
with ``CLONE_THREAD``. It checks that the call succeeds without replacing
``current->signal``. This branch has no other side effects. Rust transition
counters verify full creation and shared-copy early returns.

Build and production checks
---------------------------

The production configuration enables Rust and the new signal-structure path.
It disables this KUnit suite. Native ``rust/kernel.o`` contains one strong
``copy_signal()`` definition. Native ``kernel/fork.o`` contains only an
undefined reference. The linked image contains one strong definition.

The relocations in ``copy_process()`` contain a direct call to ``copy_signal``.
``copy_signal()`` has no export-table entry. ``Module.symvers`` contains no
old wrapper or signal-copy test symbol. ``nm -u vmlinux`` reports no undefined
symbol.

The production Clippy build produced ``bzImage`` and modules. The four-vCPU
QEMU boot reached the expected missing-root-filesystem panic. The kernel is
not tainted. No WARN, BUG, KASAN report, lockdep complaint, Oops, RCU stall,
or unexpected panic occurs.

Rustfmt and Clippy pass. Rustdoc passes. The focused Rust Sphinx build reports
only the existing unknown ``kernel-docs`` document and undefined
``getting_llvm`` label warnings. Strict checkpatch reports zero errors and
zero warnings.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The signal cache becomes accessible through a sound Rust abstraction.
* Wait queue, seqlock, timer, autogroup, audit, or cgroup initialization gains
  a safe Rust constructor with the same locking contract.
* The parent group-leader task lock stops protecting resource-limit copying.
* Signal-structure layout or required initialization order changes.
* A helper starts to select signal-structure creation policy or ordering.
