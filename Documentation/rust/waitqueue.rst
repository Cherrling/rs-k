.. SPDX-License-Identifier: GPL-2.0

Native Rust Wait Queue Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_WAITQUEUE`` option selects a native Rust implementation of
the wait queue state machine. The migration keeps the existing
``wait_queue_head`` and ``wait_queue_entry`` layouts and caller-facing API.

Rust directly defines all 24 production entry points. The complete original C
implementation remains available under ``#ifndef CONFIG_RUST_WAITQUEUE``.

Rust owns these production decisions:

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
operations, and wait condition loops remain in C. Those operations are
separate migration units.

Entry points and ownership
--------------------------

Rust directly defines these ordinary-export entry points:

* ``add_wait_queue()``
* ``add_wait_queue_exclusive()``
* ``remove_wait_queue()``
* ``__wake_up()``
* ``prepare_to_wait()``
* ``prepare_to_wait_exclusive()``
* ``init_wait_entry()``
* ``prepare_to_wait_event()``
* ``do_wait_intr()``
* ``do_wait_intr_irq()``
* ``finish_wait()``
* ``autoremove_wake_function()``
* ``wait_woken()``
* ``woken_wake_function()``
* ``woken_wake_bit_function()``

Rust directly defines these GPL-export entry points:

* ``add_wait_queue_priority()``
* ``add_wait_queue_priority_exclusive()``
* ``__wake_up_locked()``
* ``__wake_up_locked_key()``
* ``__wake_up_sync_key()``
* ``__wake_up_locked_sync_key()``
* ``__wake_up_sync()``

Rust directly defines these unexported entry points:

* ``__wake_up_on_current_cpu()``
* ``__wake_up_pollfree()``

The ordinary symbols retain their ``EXPORT_SYMBOL`` class through metadata in
``kernel/sched/wait.c``. Rust automatic export generation omits all 15
ordinary names and both unexported names. It emits the seven GPL exports.

``init_wait_entry()`` reads the current task in Rust and installs the
Rust-defined ``autoremove_wake_function()`` callback. The C boundary has no
initialization adapter.

The production state machine is in ``rust/kernel/waitqueue_core.rs``. The
native scheduler utility object contains only the narrow primitive helpers and
ordinary export metadata from ``kernel/sched/wait.c``.

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
* Ordinary non-GPL export metadata for Rust-defined entry points.

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

* Native Rust, Rust fallback, and C-only x86_64 builds.
* One-vCPU and four-vCPU KUnit runs for all six configurations.
* Generic inline KASAN, lockdep, and PREEMPT_RT builds and boots.
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
* Symbol ownership, mixed export class, duplicate-export, and
  unresolved-symbol checks.
* Module builds and an external module ABI test for all export classes.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-24 used base commit ``dd322f0ac6f5``. Six x86_64
configurations were built: native Rust, Rust fallback, C-only, generic inline
KASAN, lockdep, and PREEMPT_RT. Native, fallback, C-only, lockdep, and
PREEMPT_RT used GCC 16.2. KASAN used Clang 21.1.8 and LLD. Each configuration
passed one SMP1 boot and one SMP4 boot with eighteen of eighteen cases. The 12
boots executed 216 cases without a failure or skip.

KASAN initialized generic KernelAddressSanitizer. The lockdep image enabled
``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``, ``DEBUG_SPINLOCK``, and
``DEBUG_LIST``. The PREEMPT_RT image reported ``PREEMPT_RT`` in its kernel
banner. The logs contain no ``WARNING``, ``BUG``, Oops, failed test, kernel
panic, KASAN report, lockdep complaint, or RCU stall.

The native SMP4 image also passed six adjacent suites: ``completion`` with six
cases, ``rcuwait`` with eight, ``sched_wake`` with 37, ``swait`` with five,
``task_work`` with six, and ``wait_bit`` with seven. All 69 cases passed.

In native, KASAN, lockdep, and PREEMPT_RT, ``rust/kernel.o`` defined all 24
entry points. The native C scheduler object, built from ``wait.c``, contained
none of those definitions and retained 27 primitive helpers. The fallback and
C-only images defined all 24 entry points in the C scheduler object and none
in Rust.

Every configuration recorded exactly 15 ordinary ``EXPORT_SYMBOL`` entries,
seven GPL entries, no entry for either unexported entry point, and no duplicate
wait queue export. Rust automatic export generation omitted all 17 ordinary
and unexported names and emitted the seven GPL names.

Native, fallback, and C-only module builds passed modpost. A temporary
external module referenced all 22 exported entry points and linked against
each of those three configurations.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Sphinx emitted only the existing
``kernel-docs`` and ``getting_llvm`` cross-directory warnings. Strict
checkpatch reported no errors, warnings, or checks after targeted ignores for
``EXPORT_SYMBOL`` and function-argument placement.

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
