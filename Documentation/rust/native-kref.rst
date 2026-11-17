.. SPDX-License-Identifier: GPL-2.0

Native Rust kref lifecycle
==========================

Status and scope
----------------

The ``CONFIG_RUST_KREF`` option selects native Rust implementations for the
``kref`` lifecycle. The option depends on ``CONFIG_RUST_REFCOUNT``.

This unit covers initialization, reference reads, unconditional and
conditional references, final-reference detection, release callback dispatch,
and the mutex and spinlock release variants. The existing ``struct kref``
layout and ``kref`` API remain unchanged.

The original C inline implementation remains available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``kref_*()`` functions remain inline C ABI adapters. When the
option is enabled, every adapter calls a native Rust entry point.

Rust owns:

* Initial kref state.

* Reference reads and increments.

* Conditional increments from a non-zero count.

* Final-reference detection.

* Release callback dispatch.

* Release behavior for the plain, mutex, and spinlock variants.

Rust calls the native ``refcount_t`` state machine directly. This includes
the increment, conditional increment, decrement-to-zero, and conditional
locking transitions. The C adapters do not select a reference transition.

Retained C boundaries
---------------------

The C boundary retains:

* The public inline API and compile-time ``KREF_INIT()`` initializer.

* The embedded ``refcount_t`` layout.

* Atomic reads and writes used by the native refcount implementation.

* Mutex and spinlock acquisition and release.

* The callback ABI used by kernel release functions.

The C adapters are small calls to Rust and contain no reference-count policy.
The release callback remains a C boundary because it is supplied by each
kernel subsystem.

Important invariants
--------------------

A kref begins with one reference. A normal reference increment requires the
caller to hold an existing reference. A conditional increment succeeds only
when the count is non-zero.

The last reference causes Rust to invoke the release callback exactly once.
For ``kref_put_mutex()`` and ``kref_put_lock()``, a successful final-reference
transition returns with the requested lock held and calls the release callback
with that lock held. The callback must unlock it.

A non-final conditional-locking decrement returns without either lock held.
An absent release callback is an API violation and terminates in Rust.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_KREF=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Initialization, normal references, conditional references, final release,
  mutex release, spinlock release, and concurrent references.

* Symbol checks for all seven Rust entry points.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

On 2026-11-17, x86_64 QEMU boots with one CPU and four CPUs passed the 4-case
KUnit suite for native Rust, Rust fallback, and C-only builds. Native Rust
boots under generic KASAN and under lockdep, prove-locking, and debug
spinlocks also passed the same 4-case suite. All ten boot logs completed
without a BUG, warning, KASAN report, lock inversion, panic, or RCU stall.

The native test object referenced all seven Rust entry points. The Rust
fallback and C-only test objects referenced none of them.

Rustfmt, Clippy, Rustdoc, and focused Sphinx generation passed. The focused
Sphinx build generated this document and reported only two existing reference
warnings from other Rust index pages.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``kref`` or ``refcount_t`` layout changes.

* The public release callback contract changes.

* A retained C adapter starts to select a reference transition.

* Kernel Rust gains a safer interface for atomic state or callback dispatch.

* Measurements show that the out-of-line call boundary causes an unacceptable
  hot-path regression.
