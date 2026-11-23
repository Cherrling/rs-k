.. SPDX-License-Identifier: GPL-2.0

Native Rust kobject lifecycle
=============================

Status and scope
----------------

The ``CONFIG_RUST_KOBJECT`` option selects native Rust implementations for
the ``kobject`` initialization state and reference lifecycle. Rust directly
defines the public entry points. The option depends on ``CONFIG_RUST_KREF``.

This unit covers internal initialization, typed initialization, reference
acquisition, conditional acquisition, initialization-state validation, and
final-reference dispatch. It does not migrate sysfs registration, kset list
management, uevent delivery, naming, or object cleanup.

The complete existing C implementation remains available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``kobject_*()`` interfaces keep their existing signatures and
non-GPL export classes. When the option is enabled, Rust directly defines
``kobject_init()``, ``kobject_get()``, ``kobject_get_unless_zero()``, and
``kobject_put()``.

Rust owns:

* The internal initialization sequence.

* Typed initialization validation and state reset.

* Reference acquisition and conditional acquisition.

* Initialization-state checks before ordinary reference operations.

* Final-reference detection and cleanup callback dispatch.

Rust calls the native Rust ``kref`` lifecycle directly. The ``kref``
implementation performs the underlying count transitions.

The native ``lib/kobject.o`` no longer defines these public functions. It
contains their four non-GPL ``EXPORT_SYMBOL()`` metadata entries and retains
other un-migrated kobject operations and the C helpers listed below.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct kobject`` layout and compile-time initializers.

* Bitfield accessors for initialization and registration state.

* Invariant diagnostics and stack traces.

* Sysfs, kset, uevent, naming, and parent-reference operations.

* The non-GPL export metadata for the four public lifecycle operations.

* The ``rust_kobject_release()`` callback bridge, final ``kobject_release()``
  dispatch, ``kobject_cleanup()``, and optional delayed release work.

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

* Initialization, typed initialization, invalid initialization, null
  operations, normal and conditional references, reinitialization, final
  release, and concurrent references.

* Rust-enabled PREEMPT_RT with four CPUs.

* The adjacent percpu-ref, refcount, kref, kobject, and lockref suites.

* Symbol checks that Rust defines all four public entry points, the native C
  object contains no corresponding function bodies, the fallback and C-only
  objects contain the complete C implementation, and the non-GPL export class
  is unchanged.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``ed60c244cea3``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_KOBJECT=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_KOBJECT_KUNIT_TEST=y``.

All nine QEMU runs passed all four KUnit cases. The cases cover lifecycle
transitions, internal initialization, invalid and repeated initialization,
and concurrent references. The concurrent case used four kthreads and 10000
iterations per thread, for 80000 guarded get and conditional release cycles.

The adjacent object and reference regression passed all 26 cases in the
percpu-ref, refcount, kref, kobject, and lockref suites. The regression boot
also ran generic KUnit tests. Its ``kunit_fault`` case intentionally
triggered a NULL pointer dereference to test fault reporting.

The focused boot logs contained no BUG, warning, KASAN report, lock inversion,
kernel panic, or RCU stall. The KASAN run initialized generic KASAN. The
lockdep run initialized the lock dependency validator and used expanded
debug spinlocks. The RT kernel reported ``SMP PREEMPT_RT``.

In the native configuration, ``rust/kernel.o`` defined the four public
``kobject_*`` symbols and the internal ``rust_kobject_init_internal`` helper.
The native ``lib/kobject.o`` defined no public lifecycle function body and
referenced all four Rust symbols. It retained eight C helpers:
``rust_kobject_release``, three bitfield helpers, and four diagnostic
helpers.

``vmlinux.symvers`` recorded all four public symbols as ``EXPORT_SYMBOL``.
The generated Rust GPL export header excluded those four names and retained
``rust_kobject_init_internal``. The fallback object defined the complete C
implementation and the C-only object defined the same four public functions.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation emitted one unrelated
cross-reference warning in ``rust/index.rst``.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``struct kobject`` layout or bitfield state changes.

* The cleanup or delayed-release callback contract changes.

* The non-GPL export class or C export metadata changes.

* A fallback object starts to use a Rust public kobject symbol.

* Kernel Rust gains a safer interface for bitfield state or callback dispatch.

* Measurements show that out-of-line lifecycle calls cause an unacceptable
  hot-path regression.
