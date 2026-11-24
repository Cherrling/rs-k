.. SPDX-License-Identifier: GPL-2.0

Native Rust refcount_t Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_REFCOUNT`` option selects an experimental native Rust
implementation of the ``refcount_t`` state machine.  The migration keeps the
existing ``refcount_t`` layout and caller-facing API.  It excludes device
drivers and does not change any caller.

Rust directly defines all eleven production entry points. The complete C state
machine remains available when the option is disabled.

Rust owns all of these production decisions:

* Increment, decrement, saturation, and use-after-free detection.
* Limited-acquire limits and acquire ordering.
* The 1 to 0 decrement used by conditional locking.
* Mutex, spinlock, and IRQ-safe spinlock return contracts.

The production entry points are:

* ``__refcount_add_not_zero()``
* ``__refcount_add_not_zero_limited_acquire()``
* ``__refcount_add()``
* ``__refcount_sub_and_test()``
* ``__refcount_dec()``
* ``refcount_warn_saturate()``
* ``refcount_dec_if_one()``
* ``refcount_dec_not_one()``
* ``refcount_dec_and_mutex_lock()``
* ``refcount_dec_and_lock()``
* ``refcount_dec_and_lock_irqsave()``

The inline ``refcount_*()`` API remains in ``include/linux/refcount.h``. In
the native Rust configuration, all eleven production entries use ordinary
exports, including the five ``__refcount_*()`` functions that were
inline-only before this migration. Modules resolve the same public names and
do not refer to internal Rust symbols.

In fallback and C-only configurations, the five ``__refcount_*()`` functions
remain static inline. The six original out-of-line functions retain their
original C bodies and ordinary exports.

Retained C boundary
-------------------

The production state machine is in ``rust/kernel/refcount_core.rs``. The
native ``lib/refcount.o`` object contains the export records and the following
helpers, but none of the eleven production function bodies:

* Atomic reads, writes, compare-exchanges, fetch-add, and fetch-sub.
* Relaxed, acquire, and release memory ordering.
* The acquire barrier after a control dependency.
* Mutex and spinlock acquisition and release.
* IRQ-save and IRQ-restore operations.
* Existing warning and saturation diagnostic sites.

These helpers do not decide a state transition.  The Rust module selects the
atomic operation, validates the old value, applies saturation, and controls
the conditional-lock protocol.

The inline API retains its existing fast-path expansion and out-of-line
calls. Every public production entry now crosses directly from C ABI callers
into Rust.

Important invariants
--------------------

A saturated count has the value ``REFCOUNT_SATURATED`` and must not change
after another operation.  The Rust state machine sets this value and emits the
matching warning when an operation detects underflow, overflow, addition to
zero, or a reference leak.

``refcount_add_not_zero()`` succeeds when the old count is non-zero.  A
successful addition that detects signed overflow still saturates the count.
The limited-acquire variant returns false without changing the count when the
requested addition would exceed its limit.

Decrement operations use release ordering.  A successful 1 to 0 transition
adds the acquire ordering required before object release.  The conditional
locking functions return true only when they performed that transition, and
only then do they return with the requested lock held.

``refcount_dec_if_one()`` uses a release compare-exchange to implement the
exact 1 to 0 transition.  ``refcount_dec_not_one()`` returns true without a
change when the count is saturated.  It returns false without a change when
the count is one.  Underflow detection uses the shared warning site.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Native Rust, Rust fallback, and C-only x86_64 builds.
* One-vCPU and four-vCPU runs for every build configuration.
* Generic inline KASAN, lockdep, and PREEMPT_RT builds and boots.
* Concurrent increments and decrements from four workers.
* Native SMP4 regressions for kref, lockref, dec-and-lock, and kobject.
* Symbol ownership checks for all eleven public entry points.
* Module builds and an external module ABI test for the ordinary exports.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation record
-----------------

Validation on 2026-11-24 used base commit ``9ee2d703c50d``. Six x86_64
configurations were built: native Rust, Rust fallback, C-only, generic inline
KASAN, lockdep, and PREEMPT_RT. Native, fallback, C-only, lockdep, and
PREEMPT_RT used GCC 16.2. KASAN used Clang 21.1.8 and LLD. Each configuration
passed one SMP1 boot and one SMP4 boot with seven of seven cases. The 12 boots
executed 84 cases without a failure.

KASAN initialized generic KernelAddressSanitizer. The lockdep image enabled
``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``, ``DEBUG_SPINLOCK``, and
``DEBUG_LIST``. The PREEMPT_RT image reported ``PREEMPT_RT`` in its kernel
banner. The logs contain no ``WARNING``, ``BUG``, Oops, failed test, kernel
panic, KASAN report, lockdep complaint, or RCU stall.

The seven cases cover ordinary transitions, limited acquire, exact-zero
changes, special decrements, every saturation event, lock wrappers, and
concurrency. The concurrent case uses four workers and 5000
increment-decrement pairs per worker.

The native SMP4 image also passed adjacent suites: ``kref`` with four cases,
``lockref`` with four, ``dec_and_lock`` with six, ``kobject`` with four, and
``percpu_ref`` with seven. All 25 cases passed.

Pre-validation found three test defects. A worker could publish completion
and exit while the parent still used its stack state. Workers now remain
alive until ``kthread_stop()``. PREEMPT_RT keeps interrupts enabled while a
spinlock protects a region, so the IRQ-save contract now checks the RT
exception. A stack mutex needed explicit initialization to create a static
lock class key for lockdep.

In native, ``rust/kernel.o`` defines all eleven entry points. The native
``lib/refcount.o`` contains only the retained primitive helpers, warning
sites, and eleven export records. ``vmlinux.symvers`` and ``Module.symvers``
record exactly eleven ordinary ``EXPORT_SYMBOL`` entries. Generated Rust
export headers omit all eleven names.

The fallback and C-only images define the six original out-of-line functions
in ``lib/refcount.o``. The header retains the five static inline functions,
and neither image contains a Rust refcount production symbol.

Native, fallback, and C-only module builds passed modpost. A temporary
external module referenced all eleven ordinary exports against native. A
second temporary module referenced the six original exports against fallback
and C-only. All three modules linked successfully.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Sphinx emitted only the existing
``kernel-docs`` and ``getting_llvm`` cross-directory warnings. Strict
checkpatch reported no errors, warnings, or checks after targeted ignores
for ``EXPORT_SYMBOL`` and function-argument placement.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``refcount_t`` layout or saturation encoding changes.
* Kernel Rust gains safe abstractions for the same atomics, barriers, or
  locking primitives.
* A supported architecture cannot use the retained operation helpers.
* A C helper starts to contain refcount policy instead of a primitive
  operation.
* A warning or lock primitive starts to select a refcount result.
* The refcount protocol gains a new state or memory-ordering requirement.
* Benchmarks justify moving a fast path after its safety can be measured.
* Rust stops defining a public production entry point.
* A fallback object starts to use a Rust refcount symbol.
