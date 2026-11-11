.. SPDX-License-Identifier: GPL-2.0

Native Rust Read-Write Semaphore Protocol
=========================================

Status and scope
----------------

The ``CONFIG_RUST_RWSEM`` option selects a native Rust implementation of the
read-write semaphore count and owner protocol on non-RT kernels. The existing
``struct rw_semaphore`` layout and public API remain unchanged. The
PREEMPT_RT ``rwbase`` implementation remains unchanged.

The Rust code owns these production decisions:

* Reader fast acquisition and its read-fail check.
* Writer fast acquisition.
* Non-blocking reader and writer acquisition.
* Reader optimistic stealing from the slow path.
* Contended writer count transitions and handoff requests.
* Reader and writer owner encoding and decoding.
* ``up_read()``, ``up_write()``, and downgrade count transitions.
* Whether a release or waiter insertion must wake waiters.

Raw atomic operations and memory ordering, preemption, intrusive waiter
lists, optimistic spinning, task state, scheduling, wake queues, hung-task
accounting, lockdep, tracing, debugging, and public ABI adapters remain in C.

Entry points and ownership
--------------------------

The public lock, trylock, unlock, and downgrade entry points retain their
names, signatures, annotations, and export classes. The state machine is in
``rust/kernel/rwsem_core.rs``. ``kernel/locking/rwsem.c`` provides narrow
atomic and debug helpers. C keeps waiter insertion, waiter ordering,
optimistic spinning, scheduling, and task wakeups.

The Rust-to-C helper calls are:

* Atomic count reads, additions, and acquire compare-exchange.
* Atomic owner reads and writes.
* Current task lookup.
* Reader owner clearing and nonspinnable flag operations.
* Count warnings and contended-release tracing.

The C-to-Rust production entries are:

* ``rust_rwsem_read_fast()`` and ``rust_rwsem_read_trylock()``.
* ``rust_rwsem_reader_slow_steal()``.
* ``rust_rwsem_write_fast()`` and ``rust_rwsem_write_unqueued()``.
* ``rust_rwsem_write_slow()``.
* ``rust_rwsem_set_writer_owner()``.
* ``rust_rwsem_cond_wake()``.
* ``rust_rwsem_owner()`` and ``rust_rwsem_is_reader_owned()``.
* ``rust_rwsem_up_read()``, ``rust_rwsem_up_write()``, and
  ``rust_rwsem_downgrade_write()``.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture,
scheduler, tracing, debug, lockdep, or intrusive locking internals:

* Atomic operations and their acquire and release ordering.
* Preemption protection around count and owner updates.
* Intrusive waiter insertion, removal, ordering, and wake selection.
* Optimistic spin queue, owner-running checks, and need-resched checks.
* Task state changes, signal checks, scheduling, and wake queue delivery.
* Hung-task blocker accounting and contention tracepoints.
* Lockdep, ``DEBUG_RWSEMS`` validation, and public ABI annotations.
* The complete PREEMPT_RT ``rwbase`` implementation.

These helpers do not choose the fast or slow path, decide reader stealing,
perform writer count transitions, encode or decode ownership, or decide
whether a release must wake waiters.

Important invariants
--------------------

The low count bits identify writer ownership and waiter protocol state.
Readers occupy the aligned count field above bit 7. The high bit is the
read-fail guard. Rust preserves these masks and acquire or release ordering.

A writer owner is the task pointer. A reader owner is a task pointer with the
reader-owned hint set. The owner value can retain the last reader after
unlock, so debug code treats it as a hint unless the writer bit is clear and
the reader-owned bit is set.

A failed reader fast addition must remain counted until the slow path either
steals the lock or removes the provisional reader contribution. A reader can
steal only when no writer owns the lock and no handoff is pending. Reader
starvation protection keeps a reader-owned lock with multiple readers from
accepting another optimistic steal.

Contended writers require the wait lock. A writer can request handoff after
its scheduling or timeout policy permits it. A writer that is not first must
yield when the first waiter owns the handoff request. Acquisition clears the
handoff and clears the waiter flag when the last waiter is removed.

The final reader release wakes waiters only when the remaining count is
exactly the waiter flag. The final writer release wakes waiters whenever the
old count contains the waiter flag. A downgrade leaves one reader contribution
and wakes queued readers when the old count contains the waiter flag.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and three consecutive four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Fast read and write acquisition, unlock, and owner encoding.
* Reader and writer trylock behavior under contention.
* Reader optimistic stealing and release wake transitions.
* Blocking reader wakeup and writer FIFO handoff.
* Interruptible and killable waiter cancellation.
* Writer-to-reader downgrade with a queued reader.
* Concurrent reader/writer exclusion and operation accounting.
* PREEMPT_RT compilation with ``CONFIG_RUST_RWSEM`` disabled.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The count and owner protocol was validated on 2026-11-11 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* The Rust image passed all 8 KUnit cases on one vCPU.
* Three consecutive four-vCPU Rust runs each passed all 8 KUnit cases.
* The four-vCPU C fallback passed all 8 cases.
* The four-vCPU generic inline KASAN and lockdep images each passed all
  8 cases.
* A production-style configuration with ``CONFIG_DEBUG_RWSEMS`` disabled
  compiled ``kernel/locking/rwsem.o``, ``lib/tests/test_rwsem.o``, and the
  Rust kernel crate.
* A separate four-vCPU PREEMPT_RT configuration compiled
  ``kernel/locking/rwsem.o`` and ``kernel/locking/rtmutex_api.o`` with
  ``CONFIG_RUST_RWSEM`` disabled.
* Clippy completed with the kernel trailing ``-D warnings`` policy.
* Public ``down_read`` and ``down_write`` paths call the Rust fast
  acquisition entries before selecting the existing C slow paths.
* Public release and downgrade paths call Rust for count transitions and
  wake decisions, then use the existing C wake adapters.
* The affected public symbols have the same export class as the C fallback.
  No built module has an unresolved ``rust_rwsem_*`` symbol.
* Rustfmt and ``git diff --check`` pass. Strict checkpatch reports no errors
  or checks. Its only warning asks whether new file paths are maintained, and
  the added ``MAINTAINERS`` entries cover those paths.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic count and owner operations,
  preemption, wake queues, or locking debug facilities.
* The count or owner encoding changes.
* The rwsem handoff or reader stealing policy changes.
* Intrusive waiter ordering moves out of the current C implementation.
* The PREEMPT_RT representation stops using ``rwbase``.
* Benchmarks show that out-of-line state-machine helpers add unacceptable
  contention-path overhead.
