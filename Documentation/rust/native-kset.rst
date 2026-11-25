.. SPDX-License-Identifier: GPL-2.0

Native Rust kset lifecycle
==========================

Status and scope
----------------

The ``CONFIG_RUST_KSET`` option selects native Rust implementations for the
``kset`` initialization, registration, unregistration, lookup, and dynamic
creation lifecycle. The option depends on ``CONFIG_RUST_KOBJECT`` and
``CONFIG_RUST_KLIST``.

Rust directly defines the public entry points. C retains the shared layouts,
the embedded kobject hierarchy, sysfs and uevent operations, locking and list
initialization kernels, allocation, the static kset type, and release
callback.

The complete C implementation remains available when the option is disabled
or when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

The public interfaces keep their existing signatures. When the option is
enabled, Rust directly defines:

* ``kset_init()``

* ``kset_register()``

* ``kset_unregister()``

* ``kset_find_obj()``

* ``kset_create_and_add()``

Rust owns:

* Null and missing-type validation for registration.

* The order of embedded kobject, list, and spinlock initialization.

* Registration failure cleanup for the allocated kobject name.

* Dynamic kset allocation and initialization ordering.

* Dynamic creation failure cleanup.

* Locked traversal of the embedded kobject list.

* NUL-terminated name comparison.

* Conditional reference acquisition for a matching kobject.

* The unregistration order of deletion and final put.

Initialization initializes the embedded kobject before the kset list and
spinlock. This preserves the original C order.

Retained C boundaries
---------------------

The ``struct kset`` layout, embedded ``struct kobject``, kset list, spinlock,
and kset uevent operations remain in C. These objects are shared with device
core and other kernel callers.

Small C helpers retain:

* List-head and spinlock initialization.

* Internal kobject registration.

* Registration diagnostics.

* Kobject name allocation and failure cleanup.

* Kset addition to sysfs.

* Uevent delivery.

* List-lock acquisition and release.

* Dynamic kset allocation and release.

* Access to the static kset kobject type.

The helpers expose no allocation of a new generic Rust abstraction. The
native build contains no C function body for the five public kset entry
points.

The C object retains ``EXPORT_SYMBOL()`` metadata for ``kset_register()`` and
``kset_unregister()``. Automatic Rust export generation remains enabled for
the GPL entries ``kset_find_obj()`` and ``kset_create_and_add()``.
``kset_init()`` remains unexported.

Important invariants
--------------------

A kset passed to ``kset_register()`` must have a ktype. A missing ktype fails
with ``-EINVAL`` and leaves the kset uninitialized.

Successful registration initializes the embedded kobject first. It then adds
the kobject to its parent hierarchy and emits the original add uevent.

If internal kobject registration fails, Rust frees the name and clears the
name pointer. A dynamic kset creation failure also frees the unregistered
kset allocation.

Lookup holds the kset list lock while it traverses the embedded kobject list
and compares names. A match uses ``kobject_get_unless_zero()`` before the lock
is released.

Unregistration deletes the kobject and then transfers the registration-owned
reference with ``kobject_put()``.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_KSET=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Initialization, empty lookup, invalid registration, dynamic creation,
  child registration, lookup reference acquisition, unregistration, and
  duplicate-name creation failure.

* Four-worker concurrent lookup and conditional-reference acquisition.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Adjacent list, hlist, klist, refcount, kref, kobject, and lockref suites.

* Symbol checks that Rust defines all five public entry points, the fallback
  and C-only objects define the complete C implementation, and every entry
  has one strong definition in the final kernel.

* Export checks for the original GPL and non-GPL classes and for the
  unexported ``kset_init()`` entry.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``6bb453b1d6da``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_KSET=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_KSET=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, LLVM, ``CONFIG_SMP=y``,
``CONFIG_NR_CPUS=4``, ``CONFIG_KUNIT=y``, and
``CONFIG_KOBJECT_KUNIT_TEST=y``. Native, fallback, and C-only export checks
also used ``CONFIG_MODULES=y``.

All nine QEMU runs passed all nine kobject cases. Native, fallback, and C-only
runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs used
four virtual CPUs.

The five kset cases cover initialization, empty lookup, invalid registration,
dynamic registration, child registration, lookup reference acquisition,
unregistration, duplicate-name creation failure, and concurrent lookup. The
duplicate-name case verifies registration rollback after sysfs returns
``-EEXIST``.

The concurrent case used four kthreads. Each worker performed 2000 successful
lookup and conditional-reference cycles, for 8000 cycles.

The adjacent regression passed 39 ``list`` cases, 18 ``hlist`` cases, 11
``klist`` cases, 7 ``refcount`` cases, 4 ``kref`` cases, and 4 ``lockref``
cases. Including the nine kobject cases, the focused and adjacent runs passed
92 cases.

The focused boot logs contained no BUG, KASAN report, lock inversion, kernel
panic, or RCU stall. The KASAN configuration enabled generic inline KASAN.
The lockdep run initialized the lock dependency validator and used expanded
debug spinlocks. The RT kernel reported ``SMP PREEMPT_RT``.

The negative tests intentionally emitted the missing-ktype diagnostic and the
duplicate sysfs-name diagnostic. These messages are expected test output.

The native ``rust/kernel.o`` defined all five public symbols. The native
``lib/kobject.o`` defined none of them and referenced ``kset_register()`` and
``kset_unregister()`` only through their C export metadata. The fallback and
C-only ``lib/kobject.o`` objects each defined the complete five-function C
implementation.

The final native ``vmlinux`` contained exactly one strong definition for each
public entry and no unresolved symbol. It contained no ``rust_kset_*``
adapter. The generated Rust GPL export header contained only
``kset_find_obj()`` and ``kset_create_and_add()``.

The native, fallback, and C-only ``Module.symvers`` files recorded
``kset_register()`` and ``kset_unregister()`` as ``EXPORT_SYMBOL`` and
``kset_find_obj()`` and ``kset_create_and_add()`` as ``EXPORT_SYMBOL_GPL``.
All three files omitted ``kset_init()``.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation emitted two existing
unrelated cross-reference warnings in ``rust/index.rst`` and
``rust/quick-start.rst``.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The ``struct kset`` or embedded ``struct kobject`` layout changes.

* Kset list, spinlock, or lock-order semantics change.

* Sysfs registration, name allocation, or uevent delivery changes.

* The static kset type or release callback changes.

* The GPL or non-GPL export class changes.

* Rust stops defining a public kset entry point.

* A fallback object starts to use a Rust kset symbol.
