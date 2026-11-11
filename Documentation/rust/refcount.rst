.. SPDX-License-Identifier: GPL-2.0

Native Rust refcount_t Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_REFCOUNT`` option selects an experimental native Rust
implementation of the ``refcount_t`` state machine.  The migration keeps the
existing ``refcount_t`` layout and caller-facing API.  It excludes device
drivers and does not change any caller.

The Rust code owns all of these production decisions:

* Increment, decrement, saturation, and use-after-free detection.
* Limited-acquire limits and acquire ordering.
* The 1 to 0 decrement used by conditional locking.
* Mutex, spinlock, and IRQ-safe spinlock return contracts.

The original C state machine remains the fallback when the option is disabled.

Entry points and ownership
--------------------------

The inline ``refcount_*()`` API remains in ``include/linux/refcount.h``.  When
the Rust option is enabled, the inline wrappers call the same public
``__refcount_*()`` entry points through C ABI declarations.

The production state machine is in ``rust/kernel/refcount_core.rs``.  The
``lib/refcount.c`` file provides exported C ABI adapters for these functions:

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

The public adapters preserve the existing symbol names and export classes.
The ``__refcount_*()`` functions were inline-only before this migration.
Their new adapters use ordinary exports so non-GPL modules keep the same
access.  Modules do not refer to internal Rust symbols.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture or
kernel primitives:

* Atomic reads, writes, compare-exchanges, fetch-add, and fetch-sub.
* Relaxed, acquire, and release memory ordering.
* The acquire barrier after a control dependency.
* Mutex and spinlock acquisition and release.
* IRQ-save and IRQ-restore operations.
* Existing warning and saturation diagnostic sites.

These helpers do not decide a state transition.  The Rust module selects the
atomic operation, validates the old value, applies saturation, and controls
the conditional-lock protocol.

The migration does not preserve inline expansion.  Each public entry uses an
out-of-line C ABI transition to Rust.  This unit validates ownership and
semantics first.  A measured hot-path regression remains a reason to review
the call boundary.

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

* A Rust-enabled x86_64 build and boot with the KUnit suite.
* An x86_64 C fallback build that runs the same suite.
* A Rust-enabled build with generic inline KASAN.
* One-vCPU and repeated four-vCPU Rust runs.
* Four-vCPU fallback and KASAN runs.
* Concurrent increments and decrements from four workers.
* ABI checks for every exported adapter.
* A check that modules do not contain unresolved Rust refcount symbols.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation record
-----------------

The x86_64 validation used Clang 21 and three configurations:

* Rust implementation enabled.
* Rust implementation disabled, with the original C implementation.
* Rust implementation enabled with generic inline KASAN.

Every configuration passed all 7 KUnit cases.  The Rust build passed one
one-vCPU run and three consecutive four-vCPU runs.  The C fallback and KASAN
builds each passed one four-vCPU run.  The concurrent case used four workers
and 5000 increment-decrement pairs per worker.

The validation logs contained no BUG, Oops, KASAN report, warning, underflow
report, or kernel panic.  The generic ACPI boot message that mentions reporting
a bug is not a kernel bug report.

In the Rust image, every public adapter is a 9-byte tail jump to the matching
Rust production function.  All adapter exports are ordinary symbols.  The
built ``sit.ko`` and ``ip_tunnel.ko`` modules reference
``__refcount_sub_and_test()`` and contain no unresolved Rust refcount symbol.
The C fallback image contains no Rust refcount production symbol.

Clippy, rustfmt, checkpatch, and ``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same atomics, barriers, or
  locking primitives.
* A supported architecture cannot use the retained operation helpers.
* A C helper starts to contain refcount policy instead of a primitive
  operation.
* The refcount protocol gains a new state or memory-ordering requirement.
* Benchmarks justify moving a fast path after its safety can be measured.
* Module callers can use the internal Rust symbols directly without an ABI
  review.
