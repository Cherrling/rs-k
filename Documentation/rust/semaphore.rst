.. SPDX-License-Identifier: GPL-2.0

Native Rust Counting Semaphore Migration
========================================

Status and scope
----------------

The ``CONFIG_RUST_SEMAPHORE`` option selects a native Rust implementation of
the counting semaphore APIs in ``kernel/locking/semaphore.c``. Rust directly
defines the public acquisition, trylock, timeout, and release symbols. The
migration keeps the existing ``struct semaphore`` layout and public API.

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
primitives, wake queue initialization, hung-task accounting, tracepoint
delivery, the ``might_sleep()`` assertion, and ordinary export metadata remain
in C.

Entry points and ownership
--------------------------

Rust directly defines ``down()``, ``down_interruptible()``,
``down_killable()``, ``down_trylock()``, ``down_timeout()``, and ``up()``.
The signatures remain in ``include/linux/semaphore.h``. The public symbols
retain their ordinary ``EXPORT_SYMBOL`` class through metadata in
``kernel/locking/semaphore.c``. The native C file provides only the narrow
primitive boundary.

Scheduler-backed entries retain the ``.sched.text`` placement. The sleepable
acquisition entries call the C ``might_sleep()`` assertion before entering the
Rust state machine. ``up()`` allocates and initializes the wake queue through
the C wake queue boundary before Rust selects an increment or direct handoff.

Retained C boundary
-------------------

The following operations stay in C because they directly use locking,
scheduler, tracing, or wait queue internals:

* Raw spinlock acquisition, release, and interrupt-state restoration.
* Current task lookup and scheduler state stores.
* Current task signal predicate evaluation.
* ``schedule_timeout()``.
* Intrusive list initialization, insertion, removal, and emptiness checks.
* Wake queue initialization, insertion, emptiness checks, and deferred task
  wakeups.
* Hung-task blocker and last-holder accounting.
* Contention tracepoint begin, end, and contended-release delivery.
* Sleep-context assertions and ordinary export metadata.

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

* Native Rust, Rust fallback, and C-only x86_64 builds.
* Native Rust, Rust fallback, and C-only KUnit runs on one and four vCPUs.
* Rust-enabled generic inline KASAN, lockdep, and PREEMPT_RT builds and runs.
* Count, trylock, fast acquisition, and holder tracking.
* Interruptible, killable, and timeout slow-path results.
* Blocking handoff and timed handoff.
* FIFO ordering across multiple waiters.
* Concurrent ownership exclusion and operation accounting.
* Symbol ownership, export class, duplicate-export, and unresolved-symbol
  checks.
* Module builds and an external module ABI test.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation evidence
-------------------

The final source tree passed twelve x86_64 KUnit boots on 2026-11-24:

* Native Rust, Rust fallback, and C-only configurations on one and four
  vCPUs.
* Generic inline KASAN, lockdep, and PREEMPT_RT configurations on one and
  four vCPUs.

Every boot reported six passed tests and no failures, skips, kernel warnings,
sanitizer reports, lockdep reports, or panic. The twelve boots passed 72 of
72 cases.

The native four-vCPU adjacent suites passed 49 of 49 cases: mutex 7, rwsem 8,
rwlock 5, completion 6, simple wait queue 5, and wait queue 18.

The native Rust object defines all six public symbols, while the native C
semaphore object defines none. The fallback Rust object defines none, and the
C-only image has no Rust symbol, while their C semaphore objects define all
six. The same ownership split holds for the KASAN, lockdep, and PREEMPT_RT
configurations. All six symbols are in the Rust ``.sched.text`` section.

All six ``Module.symvers`` files contain exactly six ordinary exports, no GPL
exports, no duplicate record, and no historical ``rust_semaphore_*`` wrapper.
Every kernel image has no unresolved symbols.

Native, fallback, and C-only module builds passed modpost, and their twelve
loadable modules contain no unresolved Rust semaphore helper. A non-GPL
external module that references all six exports linked against all three
configurations.

The KASAN run initialized generic KASAN. The lockdep run initialized the lock
dependency validator. The RT kernel reported ``SMP PREEMPT_RT``.

Clippy, Rustdoc, rustfmt, strict checkpatch, and ``git diff --check`` passed
without errors or warnings. Focused Sphinx generation rendered this page with
only an unrelated cross-reference warning in ``rust/index.rst``.

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
