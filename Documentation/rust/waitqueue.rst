.. SPDX-License-Identifier: GPL-2.0

Native Rust Wait Queue Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_WAITQUEUE`` option selects a native Rust implementation of
the wait queue state machine.  The migration keeps the existing
``wait_queue_head`` and ``wait_queue_entry`` layouts and caller-facing API.

The Rust code owns these production decisions:

* Wait entry flags and insertion order.
* Wait entry initialization and callback selection.
* Autoremove decisions after scheduler wake results.
* Woken wait flag transitions and barrier sequencing.
* Variable wait key filtering for woken wakes.
* Wait preparation and idempotent queue insertion.
* Interruptible locked-wait preparation and schedule decisions.
* Current task state transitions for wait preparation and completion.
* Signal rejection and wait entry removal on return.
* Callback traversal order.
* Negative callback termination.
* Exclusive and non-exclusive wake eligibility.
* Exclusive wake budget accounting.
* Normal, synchronized, and current-CPU wake flags.
* Wakeup return counts.
* POLLFREE wake traversal and final empty-queue enforcement.

Queue initialization, scheduler state stores, scheduler entry and timeout
operations, and wait condition loops remain in C.  Those operations will be
separate migration units.

Entry points and ownership
--------------------------

The public wake and entry-operation entry points retain their names,
signatures, and ordinary export classes.  The production state machine is in
``rust/kernel/waitqueue_core.rs``.  ``kernel/sched/wait.c`` provides the C ABI
adapters and the narrow primitive boundary.

Retained C boundary
-------------------

The following operations stay in C because they directly use scheduler,
locking, poll, or intrusive list internals:

* Wait queue spinlock acquisition and interrupt state restoration.
* Lockdep assertions that require the wait queue lock.
* Intrusive list pointer insertion, removal, emptiness checks, head detection,
  next-entry lookup, and careful reinitialization.
* Current task state stores with the scheduler's required memory barriers.
* SMP barrier execution and flag stores that require architecture semantics.
* Scheduler timeout and kthread stop or park predicates.
* Lock release, scheduling, and lock reacquisition for locked waits.
* Signal predicate evaluation for the current task.
* Variable wait key extraction for the current entry.
* Delivery of the scheduler wake request through ``default_wake_function()``.
* Scheduler wake flags for current-CPU and synchronized wakeups.
* Poll key construction and the final POLLFREE queue check.

These helpers do not select an insertion position, reject signals, update task
state, select callbacks, decide whether autoremove removes an entry, transition
the woken flag, select whether to schedule, filter variable wake keys, enforce
the exclusive budget, decide when callback traversal stops, or remove a locked
wait entry after a signal.

Important invariants
--------------------

Entry insertion and removal run under the wait queue lock.  Normal and priority
entries are inserted after the existing priority prefix.  Exclusive entries
are appended to the tail.  A priority-exclusive entry fails with ``-EBUSY``
when another priority entry already leads the queue.

Wait entry initialization stores the requested flags, current task, and
autoremove callback.  It then initializes the intrusive list links to point to
the entry itself.

The autoremove callback passes the wake request to C.  Rust removes the entry
with the careful reinitialization primitive only when the scheduler reports a
successful wake.  A zero result leaves the entry queued and propagates the
result to the caller.

The woken wait protocol uses a full barrier before the waker sets
``WQ_FLAG_WOKEN``.  The waiter installs its task state with the scheduler store
barrier, checks the woken flag, and schedules only when the flag is clear and
the kthread is not stopping or parking.  It restores ``TASK_RUNNING`` without a
barrier, then stores the flags with ``WQ_FLAG_WOKEN`` cleared through the
architecture-specific barrier store.  Other wait flags remain unchanged.

The woken bit callback first verifies that the wait entry's variable address
and bit number match the wake key.  A mismatch returns zero without changing
the entry flags or waking the task.

The locked wait functions require the queue lock to be held.  Rust appends an
unqueued entry to the tail, installs ``TASK_INTERRUPTIBLE``, and rejects a
pending signal while preserving the caller's lock.  Without a pending signal,
Rust chooses to sleep and C releases the queue lock, schedules, and reacquires
the same lock in the matching interrupt mode.

Wait preparation inserts an unqueued entry only once.  The queue insertion
precedes the scheduler state store so a wakeup sees either the queued entry or
the waiter sees the wakeup.  Signal rejection removes an exclusive entry before
the waiter returns ``-ERESTARTSYS``.

Wait completion restores ``TASK_RUNNING`` before it checks the entry.  The
lockless emptiness check is careful because another CPU can update the
intrusive list links.

Wake callbacks run under the wait queue lock.  The traversal reads the next
list entry before invoking the current callback because the callback may remove
its own entry.

A negative callback result stops traversal immediately.  A successful
exclusive callback consumes one exclusive wake credit.  An exclusive callback
that returns zero does not consume a credit.  Non-exclusive callbacks never
consume a credit.

The common wake loop uses wrapping signed subtraction for the exclusive
budget.  This preserves the existing behavior where an unlimited wake passes
zero and still counts successful exclusive callbacks.

POLLFREE callbacks must remove their entries.  The migration retains an
assertion that the queue is empty after the wake traversal.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Entry insertion order, removal, and priority-exclusive contention.
* Wait entry field initialization, callback identity, and list initialization.
* Successful autoremove and retained entries after failed wakeups.
* Woken timeout retention, flag clearing, callback wake, and bit-key filtering.
* Locked signal rejection and successful wake for both lock modes.
* Idempotent wait preparation, exclusive queue status, signal rejection, and
  finish cleanup.
* Callback order and wake modes.
* Exclusive budget and zero-return callback behavior.
* Negative callback termination.
* Synchronized, current-CPU, locked, and keyed wake variants.
* POLLFREE key and queue cleanup.
* Scheduler-backed waiter integration.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation results
------------------

The final source tree passed seven x86_64 KUnit runs on 2026-11-11:

* One run on one vCPU with the Rust implementation enabled.
* Three consecutive runs on four vCPUs with the Rust implementation enabled.
* One run on four vCPUs with the C fallback implementation.
* One run on four vCPUs with generic inline KASAN enabled.
* One run on four vCPUs with lockdep, spinlock, and list debugging enabled.

Every run reported eighteen passed tests and no failures, skips, kernel warnings,
sanitizer reports, lockdep reports, or panic.

All twenty-four public wait, entry-operation, woken-callback, and locked-wait
adapters disassemble to tail calls into their Rust counterparts.  The
initialization adapter prepares the current task and autoremove callback ABI
arguments before its tail call.  The exported symbol names and classes match
the C fallback.  The narrow C boundary helpers are not exported.  The C
fallback image and the twelve loadable modules from both builds contain no
unresolved Rust wait queue symbols.

Clippy, rustfmt, strict checkpatch, and ``git diff --check`` completed without
errors or warnings.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for raw spinlocks, lockdep assertions,
  scheduler flags, poll keys, or intrusive list traversal.
* The exclusive wake accounting or callback return protocol changes.
* The wait entry flag or priority ordering contract changes.
* Scheduler state barriers, signal eligibility, or finish ordering changes.
* Wake callbacks gain permission to remove entries other than their own.
* The wake path starts selecting entries by a method other than list order.
* Benchmarks show that the out-of-line ABI and helper transitions are too
  costly for scheduler and device I/O wake paths.
