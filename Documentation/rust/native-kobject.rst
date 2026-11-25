.. SPDX-License-Identifier: GPL-2.0

Native Rust kobject lifecycle
=============================

Status and scope
----------------

The ``CONFIG_RUST_KOBJECT`` option selects native Rust implementations for
the ``kobject`` initialization state, reference lifecycle, hierarchy
lifecycle, and final cleanup. Rust directly defines the public entry points.
The option depends on ``CONFIG_RUST_KREF``.

This unit covers internal initialization, typed initialization, reference
acquisition, conditional acquisition, hierarchy removal, rename, move,
automatic hierarchy cleanup, final-reference dispatch, and the optional debug
delayed-release path.

The variadic ``kobject_add()``, ``kobject_init_and_add()``, and
``kobject_set_name()`` interfaces remain C ABI boundaries.

The complete existing C implementation remains available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``kobject_*()`` interfaces keep their existing signatures and
export classes. When the option is enabled, Rust directly defines:

* ``kobject_init()``

* ``kobject_get()``

* ``kobject_get_unless_zero()``

* ``kobject_put()``

* ``kobject_del()``

* ``kobject_rename()``

* ``kobject_move()``

Rust owns:

* The internal initialization sequence.

* Typed initialization validation and state reset.

* Reference acquisition and conditional acquisition.

* Initialization-state checks before ordinary reference operations.

* Final-reference detection and immediate or debug-delayed cleanup.

* Hierarchy removal, sysfs and kset unlinking, and parent reference release.

* Rename transaction state, duplicate-name handling, name replacement, and
  move uevent environment construction.

* Move transaction state, old and new parent reference ownership, and failed
  operation rollback.

* Automatic cleanup for objects that remain registered at final reference.

* Ktype release callback dispatch, name release, and final parent release.

Rust calls the native Rust ``kref`` lifecycle directly. The ``kref``
implementation performs the underlying count transitions.

The native ``lib/kobject.o`` no longer defines these public functions. It
contains non-GPL ``EXPORT_SYMBOL()`` metadata for the five ordinary entries.
Native Rust emits GPL exports for ``kobject_rename()`` and
``kobject_move()``.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct kobject`` layout and compile-time initializers.

* Bitfield accessors for initialization and registration state.

* Invariant diagnostics and stack traces.

* Sysfs group and directory removal, kset list removal, and uevent delivery.

* String and movable-allocation primitives used by rename and move.

* Delayed-work and timer initialization for the debug release helper.

* Export metadata for ordinary public entries and the variadic C entry
  points.

The C boundary contains no reference-count, hierarchy, rename, move, cleanup,
or release decision when ``CONFIG_RUST_KOBJECT`` is enabled. It contains no
generic wrapper that forwards the public entry points to Rust.

Important invariants
--------------------

A zero-initialized kobject begins with no references. Successful
``kobject_init()`` creates one reference, initializes the object list entry,
clears registration state, and marks the object initialized.

Ordinary reference acquisition requires an initialized object. Conditional
acquisition succeeds only when the count is non-zero. The final reference
transfers cleanup ownership to Rust exactly once.

An invalid initialization request keeps the object unchanged. An invalid
ordinary reference operation emits the same diagnostic category as the C
implementation and continues through the historical behavior.

``kobject_del()`` accepts a null pointer as a no-op. For a registered object,
it removes the sysfs groups and directory, emits the remove uevent when
required, leaves the kset, clears the parent pointer, and drops the parent
registration reference.

``kobject_rename()`` holds a temporary object reference. It changes sysfs
state before replacing the stored name. A failure leaves the old name and
path unchanged.

``kobject_move()`` holds temporary object and destination-parent references.
It changes sysfs state before transferring the new parent reference into the
object and dropping the old parent reference. A failure leaves the old
hierarchy unchanged.

Final cleanup unlinks an object that is still registered. It then invokes the
ktype release callback, releases the stored name, and drops the saved parent
reference.

With ``CONFIG_DEBUG_KOBJECT_RELEASE``, final cleanup runs from a delayed work
callback. Storage must remain valid until that callback completes.

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

* Native Rust with ``CONFIG_DEBUG_KOBJECT_RELEASE`` and timer debug objects.

* Explicit removal, rename success and duplicate-name failure, move success
  and duplicate-name failure, automatic kset-member cleanup, and delayed
  final release.

* The adjacent list, hlist, klist, refcount, kref, kobject, and lockref
  suites.

* Symbol checks that Rust defines all seven public entry points, the native C
  object contains no corresponding function bodies, the fallback and C-only
  objects contain the complete C implementation, and both export classes are
  unchanged.

* Native, fallback, and C-only module builds with ``CONFIG_MODULES=y``.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``b529a479de06``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback 1   ``CONFIG_RUST_KOBJECT=n``, one virtual CPU
Rust fallback 4   ``CONFIG_RUST_KOBJECT=n``, four virtual CPUs
Pure C 1          ``CONFIG_RUST=n``, one virtual CPU
Pure C 4          ``CONFIG_RUST=n``, four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
Debug release     Native Rust configuration plus
                  ``CONFIG_DEBUG_KOBJECT_RELEASE=y``,
                  ``CONFIG_DEBUG_OBJECTS=y``, and
                  ``CONFIG_DEBUG_OBJECTS_TIMERS=y``
================  ============================================================

Every configuration used x86_64 GCC, ``CONFIG_SMP=y``,
``CONFIG_NR_CPUS=4``, ``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_KOBJECT_KUNIT_TEST=y``. The debug release run used four virtual CPUs.

All ten QEMU runs passed all 13 kobject cases. The cases cover lifecycle
transitions, internal initialization, invalid and repeated initialization,
explicit and automatic hierarchy removal, rename, move, delayed final release,
and concurrent references. The concurrent case used four kthreads and 10000
iterations per thread, for 80000 guarded get and conditional release cycles.

The adjacent native SMP4 regression passed 96 cases: 39 ``list``, 18
``hlist``, 11 ``klist``, 7 ``refcount``, 4 ``kref``, 4 ``lockref``, and 13
``kobject`` cases.

The focused boot logs contained no unexpected BUG, warning, KASAN report,
lock inversion, kernel panic, or RCU stall. The KASAN run initialized generic
KASAN. The lockdep run initialized the lock dependency validator and used
expanded debug spinlocks. The RT kernel reported ``SMP PREEMPT_RT``. The debug
release suite emitted the expected invalid-initialization, missing-ktype, and
duplicate-name diagnostics.

In the native configuration, ``rust/kernel.o`` defined the seven public
``kobject_*`` symbols and the internal ``rust_kobject_init_internal`` helper.
The native ``lib/kobject.o`` defined no corresponding public function body.
It retained the C bodies for the variadic entry points
``kobject_add()``, ``kobject_init_and_add()``, and
``kobject_set_name()``.

The native ``lib/kobject.o`` contained no ``__kobject_del()``,
``kobject_cleanup()``, ``kobject_release()``, or
``kobject_delayed_cleanup()`` body. The fallback and C-only objects contained
the complete C implementation for all seven native entries.

The native ``vmlinux`` contained exactly one strong definition for each of
the seven public entries. Among the symbols from this unit, the generated
Rust GPL export header contained only ``kobject_rename()``,
``kobject_move()``, and ``rust_kobject_init_internal``.

The native, fallback, and C-only ``Module.symvers`` files recorded
``kobject_init()``, ``kobject_get()``, ``kobject_get_unless_zero()``,
``kobject_put()``, and ``kobject_del()`` as ``EXPORT_SYMBOL``.
``kobject_rename()`` and ``kobject_move()`` were recorded as
``EXPORT_SYMBOL_GPL``. Native, fallback, and C-only module builds completed
without duplicate exports or unresolved symbols.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation emitted only the
existing unrelated cross-reference warnings.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The ``struct kobject`` layout or bitfield state changes.

* The cleanup or delayed-release callback contract changes.

* Either export class or the C export metadata changes.

* A fallback object starts to use a Rust public kobject symbol.

* Kernel Rust gains a safer interface for bitfield state or callback dispatch.

* Measurements show that out-of-line lifecycle calls cause an unacceptable
  hot-path regression.
