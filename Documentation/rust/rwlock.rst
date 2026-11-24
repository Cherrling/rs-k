.. SPDX-License-Identifier: GPL-2.0

Native Rust Queued Read-Write Spinlock Protocol
===============================================

Status and scope
----------------

The ``CONFIG_RUST_RWLOCK`` option selects a native Rust implementation of the
queued read-write spinlock count and handoff protocol on non-RT kernels. The
existing ``rwlock_t`` and ``struct qrwlock`` layouts and public API remain
unchanged.

Rust directly defines these production entry points:

* The reader and writer fast paths used by the inline queued-lock API.
* The reader and writer trylock entries used by the inline API.
* The reader and writer unlock entries used by the inline API.
* The reader and writer slow-path entries.

Rust also owns reader and writer state selection, provisional count handling,
writer waiting, writer handoff, and unlock count transitions.

Raw atomic operations and memory ordering, the architecture spinlock that
protects the wait queue, interrupt-context detection, tracing, preemption and
lockdep wrappers, and ordinary export metadata remain in C.

Entry points and ownership
--------------------------

The public lock, trylock, and unlock interfaces keep their names, signatures,
and annotations. The state machine is in ``rust/kernel/rwlock_core.rs``.
``kernel/locking/qrwlock.c`` provides narrow atomic, architecture, and trace
helpers.

The inline queued-lock API calls these Rust entries:

* ``rust_qrwlock_read_lock()`` and ``rust_qrwlock_read_trylock()``.
* ``rust_qrwlock_write_lock()`` and ``rust_qrwlock_write_trylock()``.
* ``rust_qrwlock_read_unlock()`` and ``rust_qrwlock_write_unlock()``.

The reader and writer contention entries are also defined in Rust:

* ``queued_read_lock_slowpath()``.
* ``queued_write_lock_slowpath()``.

The two slow paths remain in ``.spinlock.text``. The native C object defines
none of these eight entries. It provides their ordinary, non-GPL export
metadata and the helper functions listed below.

The Rust-to-C helper calls are:

* Atomic count reads, additions, subtractions, compare-exchange, and release
  stores.
* Interrupt-context detection.
* Wait-queue spinlock acquisition and release.
* Acquire and relaxed conditional count reads.
* Contention tracepoint adapters.

The public ``queued_*_lock()``, trylock, and unlock functions remain inline.
They provide the original preemption-disabled locking semantics and lockdep
annotations, then call the Rust state machine.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture,
interrupt, tracing, or inline locking interfaces:

* Atomic operations and their acquire and release ordering.
* ``arch_spinlock_t`` operations for the wait queue.
* Interrupt-context detection.
* Conditional atomic read loops.
* Contention tracepoints.
* The public inline API, preemption and lockdep wrappers, and ordinary export
  metadata.

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

* Native Rust, Rust fallback, and C-only x86_64 builds.
* Native Rust, Rust fallback, and C-only KUnit runs on one and four vCPUs.
* Rust-enabled generic inline KASAN and lockdep KUnit runs.
* Three consecutive four-vCPU native Rust KUnit runs.
* Fast read, write, trylock, and unlock count transitions.
* Reader slow-path acquisition and writer-waiting state.
* Writer preference over a newly arriving reader.
* Concurrent reader/writer exclusion and operation accounting.
* PREEMPT_RT compilation with ``CONFIG_RUST_RWLOCK`` disabled.
* Symbol ownership, export class, duplicate-export, and unresolved-symbol
  checks.
* External ordinary and GPL module ABI tests.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation evidence
-------------------

The final source tree was validated on 2026-11-24 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* Native Rust, Rust fallback, C-only, generic inline KASAN, and lockdep
  configurations each passed the five queued-lock cases on one and four
  vCPUs.

* The ten direct-entry boots passed 80 of 80 cases. One-vCPU boots reported
  three passes and two contended-test skips. Four-vCPU boots reported five
  passes with no failures or skips.

* The native four-vCPU image passed three consecutive runs, 15 of 15 cases.
  Every run reported no kernel warning, bug, sanitizer report, lock dependency
  report, or panic.

* The generic KASAN configuration initialized
  ``KernelAddressSanitizer`` before both test runs. The lockdep configuration
  enabled ``PROVE_LOCKING``, ``LOCK_STAT``, ``DEBUG_LOCK_ALLOC``, and
  ``DEBUG_SPINLOCK``.

* The native four-vCPU adjacent suites passed 76 of 76 cases: mutex 8, rwsem
  9, rwlock 5, completion 6, wait queue 18, wait bit 7, simple wait queue 5,
  semaphore 6, lockref 4, and RCU wait 8.

* The PREEMPT_RT image compiled with ``CONFIG_QUEUED_RWLOCKS`` and
  ``CONFIG_RUST_RWLOCK`` absent. Its vmlinux and Rust object contain no
  queued-lock entry. Its semaphore, completion, wait queue, and simple wait
  queue tests passed 35 of 35 cases.

The native, KASAN, and lockdep Rust objects define the eight public entries.
Their C queued-lock objects define none of the entries and retain only the
helpers. The two Rust slow paths are in ``.spinlock.text``. The fallback and
C-only C objects define the two slow paths, and their active Rust objects
define no queued-lock entry.

Every native-family ``Module.symvers`` contains one non-GPL export for each of
the eight public entries. It contains no duplicate export and no internal
helper export. Every kernel image has no unresolved queued-lock symbol.

Ten external module builds covered ordinary and GPL modules across the five
non-RT configurations. Native-family modules linked all eight entries.
Fallback and C-only modules linked the two slow-path entries. Every build
passed modpost without an unresolved queued-lock symbol.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` pass.
Focused Sphinx completes with an existing cross-document warning outside this
patch. ``MAINTAINERS`` lists the documentation, C implementation, and Rust
state-machine files.

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
