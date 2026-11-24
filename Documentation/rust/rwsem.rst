.. SPDX-License-Identifier: GPL-2.0

Native Rust Read-Write Semaphore Migration
==========================================

Status and scope
----------------

The ``CONFIG_RUST_RWSEM`` option selects a native Rust implementation of the
non-RT read-write semaphore protocol. Rust directly defines the public lock,
trylock, unlock, downgrade, and initialization entry points. The existing
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
* Initialization and failure cleanup around the public entry points.

The original C implementations remain available when the option is disabled.

Entry points and ownership
--------------------------

When the option is enabled, Rust directly defines these eleven public entry
points:

* ``__init_rwsem()``.
* ``down_read()``, ``down_read_interruptible()``, and
  ``down_read_killable()``.
* ``down_read_trylock()``.
* ``down_write()`` and ``down_write_killable()``.
* ``down_write_trylock()``.
* ``up_read()`` and ``up_write()``.
* ``downgrade_write()``.

With ``CONFIG_DEBUG_LOCK_ALLOC``, Rust also defines these seven entry points:

* ``down_read_nested()`` and ``down_read_killable_nested()``.
* ``_down_write_nest_lock()``.
* ``down_read_non_owner()``.
* ``down_write_nested()`` and ``down_write_killable_nested()``.
* ``up_read_non_owner()``.

Thus the debug-lock configuration has eighteen Rust-defined public entry
points. Without that option, the public header maps the nested and anonymous
owner names to ordinary Rust entries.

The public symbols retain their ordinary ``EXPORT_SYMBOL`` class through
metadata in ``kernel/locking/rwsem.c``. The enabled native C object defines no
public rwsem entry body. The state machine and entry composition are in
``rust/kernel/rwsem_core.rs``.

Scheduler-backed blocking entries retain their ``.sched.text`` placement.
This applies to ``down_read()`` and its signal-aware variants,
``down_write()``, ``down_write_killable()``, and
``down_write_killable_nested()``.

The Rust-to-C helper calls are:

* Initialization, sleep-context assertion, and preemption control.
* Lockdep acquire, nested acquire, release, and downgrade delivery.
* Lock-stat contention and acquisition delivery.
* Atomic count reads, additions, and acquire compare-exchange.
* Atomic owner reads and writes.
* Current task lookup.
* Reader owner clearing and nonspinnable flag operations.
* Count warnings, ownership warnings, and contended-release tracing.
* Reader and writer slow-path entry.
* Ordinary and downgrade wake delivery.

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
* Lockdep and lock-stat delivery, ``DEBUG_RWSEMS`` validation, and
  initialization.
* Public ABI declarations, caller-facing annotations, and ordinary export
  metadata.
* The complete PREEMPT_RT ``rwbase`` implementation.

These helpers do not choose the fast or slow path, decide reader stealing,
perform writer count transitions, encode or decode ownership, or decide
whether a release must wake waiters.

Context analysis boundary
-------------------------

The original public C entries use ``__no_context_analysis``. Kernel Rust has
no equivalent function attribute. The public header still supplies the
caller-facing acquisition and release annotations. Analysis inside the native
Rust entry bodies is therefore not covered by the C context-analysis pass.

Locking-debug helpers use the C helper return address as the acquisition or
release site. The original external caller return address is not preserved
across the Rust entry and helper boundary.

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

* Native Rust, Rust fallback, and C-only x86_64 builds.
* Native Rust, Rust fallback, and C-only KUnit runs on one and four vCPUs.
* Rust-enabled generic inline KASAN and lockdep builds and runs.
* Lock-stat compilation and a focused lock-stat boot.
* PREEMPT_RT compilation with ``CONFIG_RUST_RWSEM`` disabled.
* Fast acquisition, unlock, initialization, and owner encoding.
* Reader and writer trylock behavior under contention.
* Reader optimistic stealing and release wake transitions.
* Blocking reader wakeup and writer FIFO handoff.
* Interruptible and killable waiter cancellation.
* Writer-to-reader downgrade with a queued reader.
* Nested lockdep classes, anonymous reader ownership, and nested lock maps.
* Concurrent reader and writer exclusion and operation accounting.
* Symbol ownership, export class, duplicate-export, and unresolved-symbol
  checks.
* Module builds and a non-GPL external module ABI test.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation evidence
-------------------

The final source tree was validated on 2026-11-24 with x86_64 ``LLVM=1``
builds and KVM QEMU:

* Native Rust, Rust fallback, C-only, generic inline KASAN, and lockdep plus
  lock-stat configurations each passed the nine rwsem cases on one and four
  vCPUs.

* The ten rwsem boots passed 90 of 90 cases. Every boot reported no failure,
  skip, kernel warning, bug, sanitizer report, lock dependency report, or
  panic.

* The PREEMPT_RT image compiled with ``CONFIG_RUST_RWSEM`` disabled. One and
  four-vCPU adjacent semaphore boots passed 12 of 12 cases.

* The native four-vCPU adjacent suites passed 56 of 56 cases: mutex 7, rwsem
  9, rwlock 5, completion 6, simple wait queue 5, wait queue 18, and
  semaphore 6.

The native and KASAN Rust objects define the eleven ordinary public entries.
The lockdep plus lock-stat Rust object defines all eighteen public entries.
Their C rwsem objects define none of those entries. The fallback, C-only, and
RT C rwsem objects define the eleven ordinary entries, and their Rust objects
define none. The five ordinary Rust configurations keep the five expected
blocking entries in ``.sched.text``. The lockdep plus lock-stat object also
keeps ``down_write_killable_nested()`` there.

All six ``Module.symvers`` files contain exactly the expected eleven or
eighteen ordinary exports. They contain no GPL export, duplicate, or internal
``rust_rwsem_*`` export. The generated Rust GPL export headers exclude every
public and internal rwsem name. Every kernel image has no unresolved symbol.

Native, fallback, C-only, KASAN, lockdep, and RT module builds passed modpost.
A proprietary external module linked all eleven ordinary exports against the
native, fallback, and C-only configurations. A lockdep build of that module
linked all eighteen ordinary exports.

Clippy, Rustfmt, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation emitted only the known
unrelated cross-reference warning in ``rust/index.rst``.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic count and owner operations,
  preemption, wake queues, locking debug facilities, or context analysis.
* The count or owner encoding changes.
* The rwsem handoff or reader stealing policy changes.
* Intrusive waiter ordering moves out of the current C implementation.
* Lockdep or lock-stat instrumentation moves into a Rust-owned abstraction.
* The PREEMPT_RT representation stops using ``rwbase``.
* Benchmarks show that out-of-line entry and helper transitions add
  unacceptable contention-path overhead.
