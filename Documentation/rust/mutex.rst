.. SPDX-License-Identifier: GPL-2.0

Native Rust Mutex Migration
===========================

Status and scope
----------------

The ``CONFIG_RUST_MUTEX`` option selects a native Rust implementation of the
non-RT mutex protocol. Rust directly defines the public initialization, query,
lock, trylock, I/O wait, unlock, nested, and wait/wound mutex entry points. The
existing ``struct mutex`` and ``struct ww_mutex`` layouts and the public API
remain unchanged. The PREEMPT_RT rtmutex representation remains unchanged.

The Rust code owns these production decisions:

* Decoding the task pointer and owner flags.
* Sleep-context validation and debug lock validation entry selection.
* Uncontended mutex acquisition and release.
* Contention trylock success and failure.
* Wait/wound acquire-context checks and wounded-state reset.
* ``MUTEX_FLAG_HANDOFF`` request transitions.
* ``MUTEX_FLAG_PICKUP`` consumption and direct ownership pickup.
* Direct ownership transfer to a selected waiter.
* Slow-path entry selection after the initial fast acquisition fails.
* Slow-unlock owner clearing and waiter wake selection preparation.

The original C implementations remain available when the option is disabled.

Entry points and ownership
--------------------------

Without ``CONFIG_DEBUG_LOCK_ALLOC``, Rust directly defines these thirteen
entry names:

* ``mutex_init_generic()``, ``mutex_is_locked()``, and
  ``mutex_get_owner()``.
* ``mutex_lock()``, ``mutex_lock_interruptible()``,
  ``mutex_lock_killable()``, ``mutex_lock_io()``, and
  ``mutex_trylock()``.
* ``mutex_unlock()``.
* ``ww_mutex_trylock()``, ``ww_mutex_unlock()``, ``ww_mutex_lock()``, and
  ``ww_mutex_lock_interruptible()``.

With ``CONFIG_DEBUG_LOCK_ALLOC``, Rust directly defines these fourteen entry
names:

* ``mutex_init_lockdep()``, ``mutex_is_locked()``, and
  ``mutex_get_owner()``.
* ``mutex_lock_nested()``, ``_mutex_lock_nest_lock()``,
  ``_mutex_lock_killable()``, ``mutex_lock_interruptible_nested()``,
  ``mutex_lock_io_nested()``, and ``_mutex_trylock_nest_lock()``.
* ``mutex_unlock()``.
* ``ww_mutex_trylock()``, ``ww_mutex_unlock()``, ``ww_mutex_lock()``, and
  ``ww_mutex_lock_interruptible()``.

Without the debug-lock option, the public header maps nested lock names to the
ordinary Rust entries. ``mutex_get_owner()`` remains an internal, unexported
helper in both configurations.

The public symbols retain their existing ordinary or GPL export classes
through metadata in ``kernel/locking/mutex.c``. The enabled native C object
defines no public mutex entry body. In non-debug configurations,
``ww_mutex_lock()`` and ``ww_mutex_lock_interruptible()`` are ordinary
exports, while ``mutex_lock_io()`` is a GPL export. In debug-lock
configurations, the nested entries and the wait/wound blocking entries are GPL
exports.

Scheduler-backed entries retain their ``.sched.text`` placement. In non-debug
configurations, this applies to the lock, trylock, I/O wait, unlock, and
wait/wound blocking entries. In debug-lock configurations, it also applies to
the nested and nest-lock entries.

The Rust-to-C helper calls are:

* Initialization, sleep-context assertion, and preemption control.
* Lockdep acquire and acquisition delivery.
* Contention tracepoint delivery.
* Wait/wound context and unlock delivery.
* Wait/wound debug deadlock injection.
* Atomic owner reads and acquire/release compare-exchange operations.
* Current task lookup and scheduler proxy-execution checks.
* Mutex slow-path lock and unlock entry.

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
* Signal checks, task state changes, and scheduling.
* Lockdep, lock-stat, and contention tracepoint delivery.
* MUTEX and wait/wound debug validation.
* Wait/wound wound/wait ordering and debug deadlock injection.
* Public ABI declarations, caller-facing annotations, and ordinary export
  metadata.
* The complete PREEMPT_RT rtmutex implementation.

These helpers do not choose the fast or contended path, encode or decode the
owner, request or consume handoff flags, choose trylock success, select direct
ownership transfer, or decide whether slow unlock has completed.

Context analysis boundary
-------------------------

The original ``ww_mutex_unlock()`` C entry uses ``__no_context_analysis``.
Kernel Rust has no equivalent function attribute. The public header still
supplies the caller-facing acquisition and release annotations. Analysis
inside the native Rust entry bodies is therefore not covered by the C
context-analysis pass.

Locking-debug helpers use the C helper return address as the acquisition,
contention, or release site. The original external caller return address is
not preserved across the Rust entry and helper boundary.

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
scheduler proxy handoff. The Rust protocol preserves the existing flag
ordering and acquire/release semantics.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Native Rust, Rust fallback, and C-only x86_64 builds.
* Native Rust, Rust fallback, and C-only KUnit runs on one and four vCPUs.
* Rust-enabled generic inline KASAN and lockdep plus lock-stat runs.
* PREEMPT_RT compilation with ``CONFIG_RUST_MUTEX`` disabled.
* Owner decoding, fast lock, fast unlock, and non-blocking trylock.
* Interruptible and killable waiter cancellation.
* Blocking wakeup, direct handoff, and pickup by the selected task.
* FIFO ownership order across multiple waiters.
* Wait/wound mutex blocking, interruptible, trylock, and repeated-context
  integration.
* Nested classes, nest-lock maps, and I/O wait entries.
* Concurrent exclusion and operation accounting.
* Symbol ownership, export class, duplicate-export, and unresolved-symbol
  checks.
* Module builds and external ordinary and GPL module ABI tests.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation evidence
-------------------

The final source tree was validated on 2026-11-24 with x86_64 ``LLVM=1``
builds and KVM QEMU:

* Native Rust, Rust fallback, C-only, generic inline KASAN, and lockdep plus
  lock-stat configurations each passed the eight mutex cases on one and four
  vCPUs.

* The ten direct-entry boots passed 80 of 80 cases. Every boot reported no
  failure, skip, kernel warning, bug, sanitizer report, lock dependency
  report, or panic.

* The lockdep configuration enabled ``PROVE_LOCKING``, ``LOCK_STAT``,
  ``DEBUG_MUTEXES``, and ``DEBUG_LOCK_ALLOC``. Both KASAN boots initialized
  generic KASAN before the tests.

* The native four-vCPU adjacent suites passed 76 of 76 cases: mutex 8, rwsem
  9, rwlock 5, completion 6, wait queue 18, wait bit 7, simple wait queue 5,
  semaphore 6, lockref 4, and RCU wait 8.

* The PREEMPT_RT image compiled with ``CONFIG_RUST_MUTEX`` absent. Its
  ``rtmutex_api`` object retains the public implementation, and its Rust
  object defines no mutex entry. One-vCPU semaphore, completion, wait queue,
  and simple wait queue tests passed 35 of 35 cases.

The native and KASAN Rust objects define the thirteen non-debug entry names.
The lockdep Rust object defines the fourteen debug-lock entry names. Their C
mutex objects define none of those entries. The fallback, C-only, and RT C
objects define the public entries, and their Rust objects define none. The
expected blocking entries remain in ``.sched.text``.

All six ``Module.symvers`` files contain the expected ordinary and GPL export
sets. They contain no duplicate mutex-family export and no internal
``rust_mutex_*`` export. ``mutex_get_owner()`` remains unexported. Every
kernel image has no unresolved symbol, and every built module passed modpost
without an unresolved internal Rust mutex symbol.

Native, fallback, C-only, KASAN, lockdep, and RT module builds passed modpost.
An external proprietary module linked the ordinary mutex and wait/wound
exports. A separate GPL module linked the GPL-only lock, I/O, nested, and
wait/wound exports. Both modules linked against the native, fallback, C-only,
KASAN, and lockdep configurations.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` pass.
Focused Sphinx completes with an existing cross-document warning outside this
patch. ``MAINTAINERS`` lists the files introduced by this migration.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic owner operations, task
  blocked-on state, wake queues, lockdep, tracing, or optimistic spin queues.
* The mutex owner flag encoding or task pointer alignment contract changes.
* Scheduler proxy execution stops using mutex handoff.
* Wait/wound mutex ordering moves out of the current C implementation.
* The PREEMPT_RT representation stops using rtmutex.
* Benchmarks show that out-of-line helpers are too costly for the
  uncontended fast path.
