.. SPDX-License-Identifier: GPL-2.0

Native Rust Simple Wait Queue Migration
=======================================

Status and scope
----------------

The ``CONFIG_RUST_SWAIT`` option selects an experimental native Rust
implementation of simple wait queues. The migration keeps the existing
``swait_queue_head`` and ``swait_queue`` layouts and caller-facing API.

Rust directly defines all ten production entry points:

* ``__init_swait_queue_head()``
* ``swake_up_locked()``
* ``swake_up_all_locked()``
* ``swake_up_one()``
* ``swake_up_all()``
* ``__prepare_to_swait()``
* ``prepare_to_swait_exclusive()``
* ``prepare_to_swait_event()``
* ``__finish_swait()``
* ``finish_swait()``

Seven public entry points retain ordinary exports. The three internal locked
wake, preparation, and completion entry points remain unexported. Rust
automatic export generation omits all ten names.

The original C state machine remains complete under ``#ifndef
CONFIG_RUST_SWAIT``.

Ownership
---------

Rust owns:

* Queue initialization and membership.
* FIFO selection for a single wakeup.
* Wake-all traversal and bounded lock release.
* Exclusive and event wait preparation and task-state ordering.
* Signal rejection before a task enters the queue.
* Completion cleanup and current task state restoration.
* Internal wake-all selection used by completion code.

C helpers retain:

* Raw spinlock initialization, lock, unlock, and IRQ state restoration.
* Lock dependency class initialization.
* Current task retrieval and task state stores.
* Pending signal queries.
* Task wakeup calls with scheduler flags.
* Intrusive list insertion, deletion, splice, and empty checks.

The native ``kernel/sched/swait.c`` object retains the seven ordinary export
records and the helper bodies. It contains none of the ten production
function bodies. The helpers do not select a waiter, decide whether to wake,
or control the wait and wake privacy protocol.

The production state machine is in ``rust/kernel/swait_core.rs``. The shared
layouts, initializers, and wait macros remain in ``include/linux/swait.h``.

Important invariants
--------------------

The queue lock protects queue membership.  ``swake_up_locked()`` and
``swake_up_all_locked()`` require the caller to hold that lock.
``swake_up_all_locked()`` is reserved for completion code that runs with
interrupts disabled.

``swake_up_all()`` moves all entries to a private list first.  It releases the
queue lock between wakeups to bound lock hold time.

Preparation stores the current task before inserting the caller-owned wait
entry.  Preparing an entry that is already linked leaves it in its current
position.  Event preparation removes the entry and returns
``-ERESTARTSYS`` when the requested state accepts a pending signal.

``set_current_state()`` provides the barrier used against wakeup.  Completion
uses the relaxed state store because it runs under the queue lock.  Finishing
a wait restores ``TASK_RUNNING`` before removing any remaining queue entry.

A single wakeup selects the first queued entry and preserves FIFO order.
Wakeup occurs before the entry is removed, matching the original locking and
memory-ordering protocol.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Native Rust, Rust fallback, and C-only x86_64 builds.
* Native Rust with one vCPU and repeated four-vCPU KUnit runs.
* Fallback, C-only, KASAN, lockdep, and PREEMPT_RT KUnit runs.
* FIFO single-wake ordering with queued kernel threads.
* Internal locked wake-all behavior used by completions.
* Public wake-all behavior and lock release between wakeups.
* Prepare, signal rejection with a queued entry, and finish state
  restoration.
* Native SMP4 ``completion``, ``rcuwait``, and ``task_work`` suites.
* Symbol ownership, export class, and module unresolved-symbol checks.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation record
-----------------

Validation on 2026-11-24 used base commit ``b490e17c3874``. Six x86_64
configurations were built: native Rust, Rust fallback, C-only, generic inline
KASAN, lockdep, and PREEMPT_RT. Each configuration passed one SMP1 boot and
one SMP4 boot with five of five cases. Native also passed three earlier SMP4
runs.

KASAN used Clang and LLD and initialized generic
KernelAddressSanitizer. The lockdep image enabled ``PROVE_LOCKING``,
``DEBUG_LOCK_ALLOC``, ``LOCKDEP``, ``DEBUG_SPINLOCK``, and ``DEBUG_LIST``.
The PREEMPT_RT image reported ``PREEMPT_RT`` in its kernel banner.

The five cases cover queue lifecycle, signal rejection, FIFO single wakeup,
public wake-all, and internal locked wake-all. The logs contain no
``WARNING``, ``BUG``, Oops, failed test, kernel panic, KASAN report, lockdep
complaint, or RCU stall.

The native SMP4 image also passed three adjacent suites: ``completion`` with
six cases, ``rcuwait`` with eight, and ``task_work`` with six. All 20 cases
passed. The native configuration was rebuilt with
``CONFIG_RCUWAIT_KUNIT_TEST=y`` before these final adjacent runs.

In native, KASAN, lockdep, and PREEMPT_RT configurations, ``rust/kernel.o``
defined all ten entry points. The native ``kernel/sched/swait.c`` object
contained only the ``rust_swait_*`` primitive helpers. The fallback and C-only
images defined the ten production entry points in C and none in Rust.

Every configuration recorded exactly seven ordinary ``EXPORT_SYMBOL`` entries
and no entry for the three internal names. Rust automatic export generation
omitted all ten production names.

The native, fallback, and C-only module builds passed modpost. A temporary
external module referenced all seven ordinary exports and linked successfully
against each of those three configurations.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. A strict Sphinx build stopped only on the
existing ``kernel-docs`` and ``getting_llvm`` cross-directory warnings.
Strict checkpatch reported 0 errors, 0 warnings, and 0 checks with targeted
ignores for ``EXPORT_SYMBOL`` and ``FUNCTION_ARGUMENTS`` placement false
positives.

Pre-validation found that the temporary wake-all list was initialized in a
``MaybeUninit`` value and then moved.  The copied list head retained the old
self pointer.  The final code initializes and uses the list head in place.
Disassembly shows that all temporary-list operations use one stack address.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for task state, wakeup, raw locks, or
  intrusive list membership.
* A supported architecture cannot use the retained raw locking helpers.
* A helper starts to select waiters or control queue transitions.
* The swait protocol changes FIFO order, signal handling, or lock-release
  rules.
* Completion starts using a different internal swait entry point.
* The shared queue or wait layout changes.
