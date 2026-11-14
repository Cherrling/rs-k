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

The static ``copy_signal()`` function remains in ``kernel/fork.c`` as an
adapter. When the option is enabled, it calls
``rust_signal_struct_copy()`` and the original C sequence is compiled out.

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

Each KUnit configuration runs three consecutive times with four vCPUs and
1536 MiB of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust signal structure copy
     - 3
     - 3 of 3
     - Pass
   * - C signal structure copy fallback
     - 3
     - 2 of 2
     - Pass
   * - Rust signal structure copy with KASAN
     - 3
     - 3 of 3
     - Pass
   * - Rust signal structure copy with PREEMPT_RT
     - 3
     - 3 of 3
     - Pass
   * - Rust signal structure copy with lockdep
     - 3
     - 3 of 3
     - Pass

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
It disables this KUnit suite. ``llvm-nm`` found the production
``rust_signal_struct_copy`` symbol. It found no test symbols and no undefined
symbols.

Disassembly of the inlined ``copy_signal()`` path in ``copy_process()`` shows
a direct call to ``rust_signal_struct_copy``. The production kernel boots to
the expected missing-root-filesystem panic. No warning, BUG, or KASAN report
occurs before that panic.

Rustfmt and Clippy pass. The Rust Sphinx build completes with only the
existing missing Graphviz, Rust reference, and LLVM label warnings. Strict
checkpatch reports no errors and one expected file-path warning.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The signal cache becomes accessible through a sound Rust abstraction.
* Wait queue, seqlock, timer, autogroup, audit, or cgroup initialization gains
  a safe Rust constructor with the same locking contract.
* The parent group-leader task lock stops protecting resource-limit copying.
* Signal-structure layout or required initialization order changes.
* A helper starts to select signal-structure creation policy or ordering.
