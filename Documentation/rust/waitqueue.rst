.. SPDX-License-Identifier: GPL-2.0

Native Rust Wait Queue Wake Migration
=====================================

Status and scope
----------------

The ``CONFIG_RUST_WAITQUEUE`` option selects a native Rust implementation of
the wait queue wake path.  The migration keeps the existing
``wait_queue_head`` and ``wait_queue_entry`` layouts and caller-facing API.

The Rust code owns these production decisions:

* Callback traversal order.
* Negative callback termination.
* Exclusive and non-exclusive wake eligibility.
* Exclusive wake budget accounting.
* Normal, synchronized, and current-CPU wake flags.
* Wakeup return counts.
* POLLFREE wake traversal and final empty-queue enforcement.

Queue initialization, waiter insertion and removal, wait preparation, task
sleep, wait completion, and the woken wait helpers remain in C.  Those
operations will be separate migration units.

Entry points and ownership
--------------------------

The public wake entry points retain their names, signatures, and ordinary
export classes.  The production wake state machine is in
``rust/kernel/waitqueue_core.rs``.  ``kernel/sched/wait.c`` provides the C ABI
adapters and the narrow primitive boundary.

Retained C boundary
-------------------

The following operations stay in C because they directly use scheduler,
locking, poll, or intrusive list internals:

* Wait queue spinlock acquisition and interrupt state restoration.
* Lockdep assertions that require the wait queue lock.
* Intrusive list head detection and next-entry lookup.
* Scheduler wake flags for current-CPU and synchronized wakeups.
* Poll key construction and the final POLLFREE queue check.

These helpers do not select callbacks, enforce the exclusive budget, or decide
when callback traversal stops.

Important invariants
--------------------

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

Every run reported six passed tests and no failures, skips, kernel warnings,
sanitizer reports, lockdep reports, or panic.

All eight public wake adapters disassemble to direct jumps into their Rust
counterparts.  The exported symbol names and classes match the C fallback.
The narrow C boundary helpers are not exported.  The C fallback image and its
loadable modules contain no Rust wait queue symbols.

Clippy, rustfmt, strict checkpatch, and ``git diff --check`` completed without
errors or warnings.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for raw spinlocks, lockdep assertions,
  scheduler flags, poll keys, or intrusive list traversal.
* The exclusive wake accounting or callback return protocol changes.
* Wake callbacks gain permission to remove entries other than their own.
* The wake path starts selecting entries by a method other than list order.
* Benchmarks show that the out-of-line ABI and helper transitions are too
  costly for scheduler and device I/O wake paths.
