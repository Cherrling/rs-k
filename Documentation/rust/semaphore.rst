.. SPDX-License-Identifier: GPL-2.0

Native Rust Counting Semaphore Migration
========================================

Status and scope
----------------

The ``CONFIG_RUST_SEMAPHORE`` option selects a native Rust state machine for
the counting semaphore APIs in ``kernel/locking/semaphore.c``. The migration
keeps the existing ``struct semaphore`` layout and public API.

The Rust code owns these production decisions:

* Positive-count fast acquisition and count decrement.
* Non-blocking trylock success and failure.
* FIFO waiter insertion and removal.
* Interruptible, killable, and uninterruptible contended waits.
* Timeout result selection.
* Direct count transfer to the first waiter on release.
* Current holder updates and contended acquisition loops.
* Wake queue population and release handoff decisions.

Raw locking, task state stores, signal predicates, scheduling, intrusive list
primitives, wake queue internals, hung-task accounting, tracepoint delivery,
and public ABI adapters remain in C.

Entry points and ownership
--------------------------

The public acquisition, trylock, timeout, and release entry points retain
their names, signatures, and export classes. The production state machine is
in ``rust/kernel/semaphore_core.rs``. ``kernel/locking/semaphore.c`` provides
the C ABI adapters and narrow primitive boundary.

Retained C boundary
-------------------

The following operations stay in C because they directly use locking,
scheduler, tracing, or wait queue internals:

* Raw spinlock acquisition, release, and interrupt-state restoration.
* Current task lookup and scheduler state stores.
* Current task signal predicate evaluation.
* ``schedule_timeout()``.
* Intrusive list initialization, insertion, removal, and emptiness checks.
* Hung-task blocker and last-holder accounting.
* Contention tracepoint begin, end, and contended-release delivery.
* Wake queue insertion, emptiness checks, and deferred task wakeups.

These helpers do not select the fast or contended path, update the semaphore
count, choose a waiter, decide timeout or signal results, mark a direct
handoff, or decide when a blocked acquisition succeeds.

Important invariants
--------------------

The semaphore spinlock protects the count, first waiter, and waiter list.
Interrupts are disabled for every semaphore lock acquisition because trylock
and release are valid in interrupt context.

The first waiter's list node is the waiter list head. Later waiters are added
at its tail, and removing the first waiter advances ``first_waiter`` to the
next list node.

A positive count is consumed directly. A zero count enters the contended path,
where Rust adds the current task to the FIFO list and repeats the signal,
timeout, task-state, unlock, schedule, and relock sequence.

Release increments the count when no waiter is queued. Otherwise it removes
the first waiter, marks a direct handoff, and queues that task for wakeup
after the semaphore lock is released.

The waiter observes the direct handoff after reacquiring the semaphore lock.
It then becomes the holder without decrementing the count again.

Timeout and signal removal are serialized by the semaphore lock. The first
event observed after relocking determines whether removal or a successful
handoff wins.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Count, trylock, fast acquisition, and holder tracking.
* Interruptible, killable, and timeout slow-path results.
* Blocking handoff and timed handoff.
* FIFO ordering across multiple waiters.
* Concurrent ownership exclusion and operation accounting.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The semaphore migration was validated on 2026-11-11 with an x86_64
``LLVM=1`` build and QEMU 11.1.3:

* The Rust image passed all 6 KUnit cases on one vCPU.
* Three consecutive four-vCPU Rust runs each passed all 6 KUnit cases.
* The four-vCPU C fallback passed all 6 cases.
* The four-vCPU generic inline KASAN and lockdep images each passed all
  6 cases.
* Clippy completed with the kernel trailing ``-D warnings`` policy.
* Acquisition and trylock adapters tail-call into Rust. ``up`` initializes
  the wake queue and then calls the Rust release state machine.
* The six public symbols have the same ``EXPORT_SYMBOL`` class as the C
  fallback. No built module has an unresolved ``rust_semaphore_*`` symbol.
* Rustfmt and ``git diff --check`` pass. Strict checkpatch reports no errors
  or checks. Its only warning asks about new file paths, and the added
  ``MAINTAINERS`` entries cover those paths.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains stable abstractions for raw spinlocks, wake queues,
  scheduler state, or intrusive list operations.
* The semaphore waiter representation or FIFO handoff contract changes.
* Semaphore counts move from a locked scalar to an atomic protocol.
* Signal or timeout ordering changes relative to direct handoff.
* Hung-task or contention tracepoint semantics require Rust ownership.
* Benchmarks show that the out-of-line ABI and boundary helpers are too costly
  for common semaphore users.
