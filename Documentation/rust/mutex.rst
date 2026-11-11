.. SPDX-License-Identifier: GPL-2.0

Native Rust Mutex Owner Protocol
================================

Status and scope
----------------

The ``CONFIG_RUST_MUTEX`` option selects a native Rust implementation of the
mutex owner protocol on non-RT kernels. The existing ``struct mutex`` layout
and public API remain unchanged. The PREEMPT_RT rtmutex representation keeps
its existing implementation.

The Rust code owns these production decisions:

* Decoding the task pointer and owner flags.
* Uncontended mutex acquisition and release.
* Contention trylock success and failure.
* ``MUTEX_FLAG_HANDOFF`` request transitions.
* ``MUTEX_FLAG_PICKUP`` consumption and direct ownership pickup.
* Direct ownership transfer to a selected waiter.
* Slow-unlock owner clearing and waiter wake selection preparation.

Raw atomic operations, scheduler proxy state, intrusive wait-list operations,
task blocked-on state, optimistic spinning, scheduling, wakeups, lockdep,
mutex debugging, and public ABI adapters remain in C.

Entry points and ownership
--------------------------

The public lock, trylock, and unlock entry points retain their names,
signatures, annotations, and export classes. The owner state machine is in
``rust/kernel/mutex_core.rs``. ``kernel/locking/mutex.c`` provides narrow C
helpers and keeps the existing slow-path scheduling and optimistic spinning
implementation.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture,
scheduler, tracing, debug, or lockdep internals:

* Atomic owner reads and acquire/release compare-exchange operations.
* Current task lookup.
* Scheduler proxy-execution donor inspection.
* Intrusive mutex waiter insertion, removal, and waiter selection.
* Task blocked-on tracking and task wakeups.
* Optimistic spin queue and owner-running checks.
* Mutex lockdep acquire and release annotations.
* MUTEX debug validation and waiter magic checks.
* Contention tracepoint delivery.
* Public ABI and ``__sched`` annotations.

These helpers do not choose the fast or contended path, encode or decode the
owner, request or consume handoff flags, choose trylock success, select direct
ownership transfer, or decide whether slow unlock has completed.

Important invariants
--------------------

Low alignment bits in ``lock->owner`` carry mutex protocol state. Rust keeps
the task pointer bits separate from the waiter, handoff, and pickup flags.

A zero task pointer means the mutex is not owned. A nonzero task pointer means
that task owns the mutex. Only the owner can release it.

The handoff flag requests that slow unlock preserve the lock for a selected
task instead of making it generally available. Direct handoff installs that
task pointer with the pickup flag. Only that task may consume the pickup flag
and complete acquisition.

The WAITERS flag survives owner transitions until the wait list becomes empty.
Slow unlock clears ownership and leaves WAITERS set so waiters can be woken.

Mutex waiting remains FIFO except for wait/wound mutex stamp ordering and
scheduler proxy handoff. The Rust owner protocol preserves the existing flag
ordering and acquire/release semantics.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Owner decoding, fast lock, fast unlock, and non-blocking trylock.
* Interruptible and killable waiter cancellation.
* Blocking wakeup, direct handoff, and pickup by the selected task.
* FIFO ownership order across multiple waiters.
* Wait/wound mutex fast-path integration.
* Concurrent exclusion and operation accounting.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The mutex owner protocol was validated on 2026-11-11 with x86_64 ``LLVM=1``
builds and QEMU 11.1.3:

* The Rust image passed all 7 KUnit cases on one vCPU.
* Three consecutive four-vCPU Rust runs each passed all 7 KUnit cases.
* The four-vCPU C fallback passed all 7 cases.
* The four-vCPU generic inline KASAN and lockdep images each passed all
  7 cases.
* A separate four-vCPU PREEMPT_RT configuration compiled
  ``kernel/locking/mutex.o`` and ``kernel/locking/rtmutex_api.o`` with
  ``CONFIG_RUST_MUTEX`` disabled.
* Clippy completed with the kernel trailing ``-D warnings`` policy.
* ``mutex_is_locked`` and ``mutex_get_owner`` tail-call into Rust. Fast lock
  and unlock call Rust before selecting the existing C slow paths.
* Slow unlock calls Rust to prepare owner state and to apply direct handoff.
* The affected public symbols have the same export class as the C fallback.
  No built module has an unresolved ``rust_mutex_*`` symbol.
* Rustfmt and ``git diff --check`` pass. Strict checkpatch reports no errors
  or checks. Its only warning asks about new file paths, and the added
  ``MAINTAINERS`` entries cover those paths.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic owner operations, task
  blocked-on state, wake queues, lockdep, or optimistic spin queues.
* The mutex owner flag encoding or task pointer alignment contract changes.
* Scheduler proxy execution stops using mutex handoff.
* Wait/wound mutex ordering moves out of the current C implementation.
* The PREEMPT_RT representation stops using rtmutex.
* Benchmarks show that out-of-line owner helpers are too costly for the
  uncontended path.
