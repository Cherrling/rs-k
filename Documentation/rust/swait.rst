.. SPDX-License-Identifier: GPL-2.0

Native Rust Simple Wait Queue Migration
=======================================

Status and scope
----------------

The ``CONFIG_RUST_SWAIT`` option selects an experimental native Rust
implementation of simple wait queues.  The migration keeps the existing
``swait_queue_head`` and ``swait_queue`` layouts and caller-facing API.

The Rust code owns these production decisions:

* Queue initialization and membership.
* FIFO selection for a single wakeup.
* Wake-all traversal and bounded lock release.
* Exclusive and event wait preparation.
* Signal rejection before a task enters the queue.
* Completion cleanup and current task state restoration.

The original C state machine remains the fallback when the option is disabled.

Entry points and ownership
--------------------------

The public entry points retain their names and ordinary export classes.  The
internal ``__prepare_to_swait()``, ``__finish_swait()``, and
``swake_up_all_locked()`` entry points remain available to the completion
code.

The production state machine is in ``rust/kernel/swait_core.rs``.  The
``kernel/sched/swait.c`` file provides exported C ABI adapters and the narrow
primitive boundary.

The migration does not preserve inline expansion.  Each public entry crosses
the C ABI into Rust.  This unit validates semantics and lifetime rules before
performance work.

Retained C boundary
-------------------

The following operations stay in C because they directly use scheduler,
architecture, or lock debugging internals:

* Raw spinlock initialization, lock, unlock, and IRQ state restoration.
* Lock dependency class initialization.
* Current task retrieval and task state stores.
* Pending signal queries.
* Task wakeup calls with scheduler flags.
* Intrusive list insertion, deletion, splice, and empty checks.

These helpers do not select a waiter, decide whether to wake, or control the
wait and wake privacy protocol.

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

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback and KASAN KUnit runs.
* FIFO single-wake ordering with queued kernel threads.
* Internal locked wake-all behavior used by completions.
* Public wake-all behavior and lock release between wakeups.
* Prepare, signal rejection with a queued entry, and finish state
  restoration.
* Export and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation record
-----------------

The final tree passed the following x86_64 checks with ``LLVM=1``:

* Rust, one vCPU: five tests passed, once.
* Rust, four vCPUs: five tests passed, three consecutive runs.
* C fallback, four vCPUs: five tests passed.
* Generic inline KASAN, four vCPUs: five tests passed.
* ``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``,
  ``DEBUG_SPINLOCK``, and ``DEBUG_LIST`` enabled, four vCPUs: five tests
  passed.

The five cases cover queue lifecycle, signal rejection, FIFO single wakeup,
public wake-all, and internal locked wake-all.  The logs contain no unexpected
``WARNING``, ``BUG``, ``Oops``, KASAN report, failed test, or kernel panic.

The production ABI check confirmed that each public C adapter tail-calls its
Rust implementation and retains an ordinary export.  The Rust-enabled image
contains the ``rust_swait_*`` production symbols.  The C fallback image has no
such symbols, and no loadable module has an unresolved ``rust_swait_*``
symbol.

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
* Benchmarks show that the out-of-line ABI transition is too costly for a
  scheduler hot path.
