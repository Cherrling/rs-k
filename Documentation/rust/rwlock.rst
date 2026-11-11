.. SPDX-License-Identifier: GPL-2.0

Native Rust Queued Read-Write Spinlock Protocol
===============================================

Status and scope
----------------

The ``CONFIG_RUST_RWLOCK`` option selects a native Rust implementation of the
queued read-write spinlock count and handoff protocol on non-RT kernels. The
existing ``rwlock_t`` and ``struct qrwlock`` layouts and public API remain
unchanged.

The Rust code owns these production decisions:

* Reader and writer fast acquisition.
* Non-blocking reader and writer acquisition.
* Reader and writer slow-path count transitions.
* The writer-waiting flag and writer handoff state.
* Reader and writer unlock transitions.

Raw atomic operations and memory ordering, the architecture spinlock that
protects the wait queue, interrupt-context detection, tracing, preemption
semantics, and public ABI adapters remain in C.

Entry points and ownership
--------------------------

The public lock, trylock, and unlock interfaces keep their names, signatures,
and annotations. The state machine is in ``rust/kernel/rwlock_core.rs``.
``kernel/locking/qrwlock.c`` provides narrow atomic, architecture, and trace
helpers.

The Rust-to-C helper calls are:

* Atomic count reads, additions, subtractions, compare-exchange, and release
  stores.
* Interrupt-context detection.
* Wait-queue spinlock acquisition and release.
* Acquire and relaxed conditional count reads.
* Contention tracepoint adapters.

The C-to-Rust production entries are:

* ``rust_qrwlock_read_lock()`` and ``rust_qrwlock_read_trylock()``.
* ``rust_qrwlock_write_lock()`` and ``rust_qrwlock_write_trylock()``.
* ``rust_qrwlock_read_unlock()`` and ``rust_qrwlock_write_unlock()``.
* ``rust_qrwlock_read_lock_slowpath()`` and
  ``rust_qrwlock_write_lock_slowpath()``.

The C file provides non-GPL exported adapters for the six inline fast-path and
unlock entries. These adapters call the Rust state machine with the license
class used by the original inline lock interface.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture,
interrupt, tracing, or inline locking interfaces:

* Atomic operations and their acquire and release ordering.
* ``arch_spinlock_t`` operations for the wait queue.
* Interrupt-context detection.
* Conditional atomic read loops.
* Contention tracepoints.
* Public inline API and module export adapters.

These helpers do not select fast or slow paths, request writer handoff,
perform count transitions, or decide when to release the wait queue.

Important invariants
--------------------

The low nine count bits contain writer ownership and handoff state. Readers
occupy the aligned count field above bit 8. A writer request sets the waiting
flag until all readers have left, then replaces the waiting-only count with
the writer-locked value.

A reader that fails the fast path leaves its provisional count in place until
the slow path either acquires in interrupt context or moves into the
wait-queue protocol. A waiting writer prevents new readers from entering the
fast path.

The final reader release decrements the reader count with release ordering.
Writer release stores zero to the writer-visible field with release ordering.
The wait-queue spinlock protects slow-path handoff and the waiting flag.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and three consecutive four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Fast read, write, trylock, and unlock count transitions.
* Reader slow-path acquisition and writer-waiting state.
* Writer preference over a newly arriving reader.
* Concurrent reader/writer exclusion and operation accounting.
* PREEMPT_RT compilation with ``CONFIG_RUST_RWLOCK`` disabled.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The count and handoff protocol was validated on 2026-11-11 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* The Rust image passed all 5 KUnit cases on three consecutive four-vCPU
  runs.
* The Rust image passed 3 cases and skipped the 2 contended cases on one
  vCPU.
* The four-vCPU C fallback, generic inline KASAN, and lockdep images each
  passed all 5 cases.
* A separate four-vCPU PREEMPT_RT configuration compiled
  ``kernel/locking/qrwlock.o`` and the Rust kernel crate with
  ``CONFIG_RUST_RWLOCK`` disabled.
* Clippy completed with the kernel trailing ``-D warnings`` policy.
* Public ``queued_*_lock()``, trylock, and unlock fast paths call the Rust
  entries before selecting the existing C slow paths.
* The slow-path adapters tail-call the Rust reader and writer state machines.
  Their disassembly contains no C count or handoff decisions.
* The six public fast-path adapters use the original non-GPL export class.
  A ``Proprietary`` module that directly uses the queued lock fast and slow
  paths completed modpost and module linking without a GPL symbol conflict.
* Rustfmt and ``git diff --check`` pass. Strict checkpatch reports no errors
  or checks. Its only warning asks whether new file paths are maintained, and
  the added ``MAINTAINERS`` entries cover those paths.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic operations,
  interrupt-context checks, or lock tracing.
* The queued read-write lock count encoding or handoff policy changes.
* The architecture wait-queue representation changes.
* PREEMPT_RT starts using the queued lock representation.
* Benchmarks show that the out-of-line state-machine entries add unacceptable
  contention-path overhead.
