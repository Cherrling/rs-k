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

With the option enabled, Rust directly defines these public entry points:

* ``kref_init()``
* ``kref_read()``
* ``kref_get()``
* ``kref_put()``
* ``kref_put_mutex()``
* ``kref_put_lock()``
* ``kref_get_unless_zero()``

The native C object ``lib/kref.o`` contains only their ordinary export
metadata. The Rust implementation owns every reference transition and release
decision.

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

* The public declarations and compile-time ``KREF_INIT()`` initializer.

* The embedded ``refcount_t`` layout.

* Atomic reads and writes used by the native refcount implementation.

* Mutex and spinlock acquisition and release.

* The callback ABI used by kernel release functions.

* Ordinary non-GPL export metadata in ``lib/kref.c``.

The export object contains no reference-count policy. The release callback
remains a C boundary because each kernel subsystem supplies it.

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

Validation on 2026-11-24 used base commit ``531d347d8711``. Fourteen x86_64
QEMU boots ran the four-case ``kref`` suite. Native used one SMP1 boot and
three SMP4 boots. Rust fallback, C-only, KASAN, lockdep, and PREEMPT_RT each
used one SMP1 boot and one SMP4 boot. All 56 test cases passed with no skips.

The native SMP4 image also passed four adjacent suites: ``kobject`` with four
cases, ``refcount`` with seven, ``lockref`` with four, and ``mutex`` with
seven. All 22 cases passed.

The final boot logs contained no ``BUG``, warning, Oops, kernel panic, KASAN
report, lockdep complaint, or RCU stall.

In native, KASAN, lockdep, and PREEMPT_RT configurations,
``rust/kernel.o`` defined all seven public entry points and ``lib/kref.o``
defined none. The fallback and C-only configurations defined none in Rust
and did not emit out-of-line exports. Every Rust production
``vmlinux.symvers`` contained the seven expected ``EXPORT_SYMBOL`` entries
and no GPL duplicates. Disassembly showed that the Rust ``kref_get()`` entry
executed the reference transition directly.

The native, fallback, and C-only module builds passed modpost. A native
module that uses the public API, ``usblcd.ko``, referenced ``kref_get()``,
``kref_init()``, and ``kref_put()`` and resolved them through the ordinary
exports.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx reported only the existing
``kernel-docs`` and ``getting_llvm`` warnings. Checkpatch required targeted
ignores for its export-only placement and conditional-acquisition prototype
false positives.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``kref`` or ``refcount_t`` layout changes.

* The public release callback contract changes.

* A retained C function starts to select a reference transition.

* Kernel Rust gains a safer interface for atomic state or callback dispatch.

* Measurements show that the out-of-line call boundary causes an unacceptable
  hot-path regression.
