.. SPDX-License-Identifier: GPL-2.0

Native Rust kobject lifecycle
=============================

Status and scope
----------------

The ``CONFIG_RUST_KOBJECT`` option selects native Rust implementations for
the ``kobject`` initialization state and reference lifecycle. The option
depends on ``CONFIG_RUST_KREF``.

This unit covers internal initialization, typed initialization, reference
acquisition, conditional acquisition, initialization-state validation, and
final-reference dispatch. It does not migrate sysfs registration, kset list
management, uevent delivery, naming, or object cleanup.

The existing C implementation remains available when the option is disabled.

Entry points and ownership
--------------------------

The existing ``kobject_*()`` functions remain C ABI entry points. When the
option is enabled, they call native Rust implementations for lifecycle work.

Rust owns:

* The internal initialization sequence.

* Typed initialization validation and state reset.

* Reference acquisition and conditional acquisition.

* Initialization-state checks before ordinary reference operations.

* Final-reference detection and cleanup callback dispatch.

Rust calls the native Rust ``kref`` lifecycle directly. The ``kref``
implementation performs the underlying count transitions.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct kobject`` layout and compile-time initializers.

* Bitfield accessors for initialization and registration state.

* Invariant diagnostics and stack traces.

* Sysfs, kset, uevent, naming, and parent-reference operations.

* The final ``kobject_cleanup()`` callback.

The C boundary contains no reference-count transition decisions when
``CONFIG_RUST_KOBJECT`` is enabled.

Important invariants
--------------------

A zero-initialized kobject begins with no references. Successful
``kobject_init()`` creates one reference, initializes the object list entry,
clears registration state, and marks the object initialized.

Ordinary reference acquisition requires an initialized object. Conditional
acquisition succeeds only when the count is non-zero. The final reference
causes Rust to dispatch the C cleanup path exactly once.

An invalid initialization request keeps the object unchanged. An invalid
ordinary reference operation emits the same diagnostic category as the C
implementation and continues through the historical behavior.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_KOBJECT=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Initialization, typed initialization, null operations, normal and
  conditional references, final release, and concurrent references.

* Symbol checks for all five Rust entry points.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Ten final SMP boot tests passed with three cases and no failures.  The test
matrix covered native Rust, ``CONFIG_RUST_KOBJECT=n``, and ``CONFIG_RUST=n``
with one and four CPUs.  Native Rust also passed with KASAN and with lockdep,
prove-locking, and debug spinlocks.

A scan of all ten boot logs found no BUG, warning, KASAN report, lock
inversion, panic, or RCU stall.

The native ``lib/kobject.o`` references all five Rust entry points.  The
fallback and C-only objects reference none of them.  The native ``vmlinux``
contains the five Rust entry points and seven retained C helper symbols.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``struct kobject`` layout or bitfield state changes.

* The cleanup or delayed-release callback contract changes.

* Kernel Rust gains a safer interface for bitfield state or callback dispatch.

* Measurements show that out-of-line lifecycle calls cause an unacceptable
  hot-path regression.
