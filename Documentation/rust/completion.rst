.. SPDX-License-Identifier: GPL-2.0

Native Rust Completion Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_COMPLETION`` option selects an experimental native Rust
implementation of completions.  The migration keeps the existing
``struct completion`` layout and caller-facing API.

The Rust code owns these production decisions:

* Completion count increments and decrements.
* Saturation at ``UINT_MAX`` after broadcast completion.
* Waiter queue loops for finite and infinite waits.
* Timeout and pending-signal results.
* Single completion, current-CPU completion, and broadcast completion.
* Non-blocking count consumption and completion queries.

The original C state machine remains the fallback when the option is disabled.

Entry points and ownership
--------------------------

The public entry points retain their names, signatures, and ordinary export
classes.  ``init_completion()``, ``reinit_completion()``, and the static
initializer remain inline construction boundaries.  They initialize the
embedded swait queue and reset the count to zero.

The production state machine is in ``rust/kernel/completion_core.rs``.
``kernel/sched/completion.c`` provides the C ABI adapters and narrow primitive
boundary.

Retained C boundary
-------------------

The following operations stay in C because they use scheduler, task,
architecture, or locking internals:

* Raw spinlock acquisitions and interrupt state restoration.
* Current task state stores and pending-signal queries.
* Sleepability checks and completion memory-ordering hooks.
* Normal and I/O scheduler timeout calls.
* Simple wait queue preparation, finish, single wake, and broadcast wake.
* The scheduler flag for current-CPU wakeup preference.
* The realtime threaded-context lock assertion.

These helpers do not modify completion counts or decide when a wait succeeds,
times out, or accepts a signal.

Important invariants
--------------------

The embedded swait lock protects completion counts and waiter membership.
The wait loop drops that lock only while the current task sleeps.  It restores
the task state and removes the stack wait entry before returning.

``complete()`` increments a nonzero-bounded count and wakes one FIFO waiter.
``complete_all()`` stores ``UINT_MAX`` and wakes every queued waiter.
``try_wait_for_completion()`` never consumes a broadcast completion.

``completion_done()`` acquires and releases the wait lock after a nonzero
count observation.  This synchronizes with ``complete()`` before a caller can
free the completion.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Count consumption, finite timeout, and all public wait variants.
* FIFO single completion with queued kernel threads.
* Broadcast completion, saturation, reset, and future-waiter behavior.
* Interruptible signal rejection.
* Concurrent completion producers.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation record
-----------------

The final tree passed the following x86_64 checks with ``LLVM=1``:

* Rust, one vCPU: six tests passed, once.
* Rust, four vCPUs: six tests passed, three consecutive runs.
* C fallback, four vCPUs: six tests passed.
* Generic inline KASAN, four vCPUs: six tests passed.
* ``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``,
  ``DEBUG_SPINLOCK``, and ``DEBUG_LIST`` enabled, four vCPUs: six tests
  passed.

The six cases cover count consumption and timeout, public wait variants,
signal interruption, FIFO single completion, broadcast completion, and
concurrent producers.  The logs contain no unexpected ``WARNING``, ``BUG``,
``Oops``, KASAN report, failed test, lock dependency diagnosis, or kernel
panic.

The production ABI check confirmed that all fourteen public C adapters
tail-call their Rust implementation.  The Rust-enabled image contains the
``rust_completion_*`` production symbols.  The thirteen ordinary completion
exports remain present.  The C boundary helpers are not exported.  The C
fallback image has no ``rust_completion_*`` symbols, and no loadable module has
an unresolved ``rust_completion_*`` symbol.

Clippy, Rustfmt validation, checkpatch with strict mode, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for task state, scheduling, signals, raw
  locks, or swait membership.
* Completion initialization moves out of the inline construction boundary.
* The completion count type or saturation value changes.
* A scheduler action other than normal or I/O timeout is introduced.
* Completion wakeup ordering or signal behavior changes.
* Benchmarks show that the out-of-line ABI and helper transitions are too
  costly for scheduler-core users.
