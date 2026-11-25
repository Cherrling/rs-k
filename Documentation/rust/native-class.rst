.. SPDX-License-Identifier: GPL-2.0

Native Rust device class lifecycle
==================================

Status and scope
----------------

The ``CONFIG_RUST_CLASS`` option selects native Rust implementations for the
device ``class`` registration, unregistration, dynamic creation, and
destruction lifecycle. The option depends on ``CONFIG_RUST_KSET``.

Rust directly defines the public entry points. C retains the shared class and
subsystem layouts, class lookup and reference accounting, sysfs and klist
primitives, lock initialization, the static class kset and kobject types,
diagnostics, and the final class release callback.

The complete C implementation remains available when the option is disabled
or when its Rust dependencies are disabled.

Entry points and ownership
--------------------------

The public interfaces keep their existing signatures and exports. When the
option is enabled, Rust directly defines:

* ``class_register()``

* ``class_unregister()``

* ``class_create()``

* ``class_destroy()``

Rust owns:

* Null class validation.

* Namespace and namespace-type validation.

* Zeroed class-private allocation.

* Device-list callback initialization for class member devices.

* Interface-list, hierarchy, lock, mutex, and name initialization.

* The initialization order of the embedded class kset and glue-directory
  kset.

* Class kset and kobject type binding.

* Registration failure cleanup for names, lock keys, and private storage.

* Class group creation and registration rollback.

* Dynamic class allocation, release callback binding, and error pointers.

* The unregistration order of group removal, kset removal, and the final
  lookup-reference release.

* Null and error-pointer tolerance in dynamic class destruction.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct class`` and ``struct subsys_private`` layouts.

* ``class_to_subsys()`` lookup and its reference ownership.

* Class attribute operations, namespace selection, and final class release.

* The ``/sys/class`` kset and the static class kobject type.

* Sysfs group creation and removal.

* Klist, kset, mutex, and lock dependency initialization primitives.

* Class name allocation and diagnostics.

* Dynamic class release.

* Export metadata shared with the complete C fallback.

The native ``drivers/base/class.o`` object contains no body for the four
public class lifecycle entries. The fallback and C-only objects contain the
complete C implementation.

Important invariants
--------------------

The class namespace and namespace-type callbacks must both be present or both
be absent. A mismatch fails registration with ``-EINVAL``.

Class-private storage is allocated as zeroed memory. Device-list callbacks
resolve a ``device_private::knode_class`` link to its device and take or drop
the matching device reference.

Both the main embedded class kset and the embedded ``glue_dirs`` kset must be
initialized before registration. Device core uses ``glue_dirs`` to create
intermediate class directories under parent devices.

Registration initializes the private object before setting its name. It then
binds the class kset and kobject type, registers the class kset, and creates
the class attribute groups. Failure after kset registration removes the
kobject, releases its name, unregisters the lock key, and frees private
storage.

Unregistration looks up the private object with a reference. It removes the
class groups before unregistering the kset, then releases the lookup
reference. A null or already unregistered class is a no-op.

``class_destroy()`` accepts null and error pointers. A valid class must have
been allocated by ``class_create()``. The kobject release callback invokes
the class release hook before lock-dependent private storage is unregistered
and freed.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_CLASS=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Namespace mismatch rejection, null unregistration tolerance, static
  registration, idempotent unregistration, duplicate registration rejection,
  dynamic creation, duplicate dynamic creation rejection, destruction, and
  recreation.

* C-only KASAN with the complete C fallback.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Adjacent device-model, kobject, kset, klist, list, and reference suites.

* Symbol checks that Rust defines all four public entries and that fallback
  and C-only objects define the complete C implementation.

* Export checks that every configuration retains ``EXPORT_SYMBOL_GPL`` for
  the four entries.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``527f29a69a23``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_CLASS=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_CLASS=n``
Pure C            ``CONFIG_RUST=n``
KASAN fallback    Pure C configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus
                  ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``,
``CONFIG_NR_CPUS=4``, ``CONFIG_KUNIT=y``, and
``CONFIG_DM_KUNIT_TEST=y``. Native, fallback, and C-only export checks also
used ``CONFIG_MODULES=y``.

All nine QEMU runs passed all four class cases. Native, fallback, and C-only
runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs used
four virtual CPUs.

The focused cases cover namespace mismatch rejection, null unregistration,
static registration and idempotent unregistration, duplicate static
registration, dynamic creation and destruction, duplicate dynamic creation,
and recreation after destruction.

The device-model smoke tests passed 17 cases in each of the native, fallback,
and C-only runs. The adjacent regression passed 13 kobject cases, 39 list
cases, 18 hlist cases, 11 klist cases, 7 refcount cases, 4 kref cases, and
4 lockref cases.

The focused boots contained no BUG, KASAN report, lock inversion, kernel
panic, or RCU stall. The negative tests intentionally emitted the
missing-namespace and duplicate sysfs-name diagnostics. These messages are
expected test output.

The native ``rust/kernel.o`` defined all four public symbols. The native
``drivers/base/class.o`` defined none of them. The fallback and C-only
``drivers/base/class.o`` objects each defined the complete four-function C
implementation.

The final native, fallback, and C-only kernels contained exactly one strong
definition for each public entry. Each ``Module.symvers`` file recorded all
four entries as ``EXPORT_SYMBOL_GPL``. Native Rust generated GPL exports for
the same four symbols.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Focused Sphinx generation emitted only two
existing unrelated cross-reference warnings. Strict checkpatch reported only
the generic ``FILE_PATH_CHANGES`` warning for newly added files. With that
expected warning ignored, checkpatch reported no errors, warnings, or checks.
