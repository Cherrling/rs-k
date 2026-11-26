.. SPDX-License-Identifier: GPL-2.0

Native Rust device bus lifecycle
================================

Status and scope
----------------

The ``CONFIG_RUST_BUS`` option selects native Rust implementations for device
bus registration and unregistration. The option depends on
``CONFIG_RUST_KSET``.

Rust directly defines the public entry points. C retains the shared bus and
subsystem layouts, bus lookup and reference accounting, static bus kset and
kobject types, sysfs attribute mappings, device reference callbacks,
diagnostics, and the final bus release callback.

The complete C implementation remains available when the option is disabled
or when its Rust dependencies are disabled.

Entry points and ownership
--------------------------

The public interfaces keep their existing signatures and exports. When the
option is enabled, Rust directly defines:

* ``bus_register()``

* ``bus_unregister()``

Rust owns:

* Null bus validation.

* Zeroed bus-private allocation.

* Bus pointer and blocking notifier initialization.

* Embedded main kset and kobject initialization.

* Bus kset and kobject type binding.

* Main kset registration and uevent attribute creation.

* Device and driver child kset creation.

* Interface list, lock key, mutex, and both klist initializations.

* Probe attribute creation and bus attribute group creation.

* Reverse-order rollback after every registration step.

* Device-root teardown and the complete unregistration order.

* The final lookup-reference release.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct bus_type`` and ``struct subsys_private`` layouts.

* ``bus_to_subsys()`` lookup and its reference ownership.

* The ``/sys/bus`` kset and the static bus kobject type.

* Bus uevent, drivers-probe, and drivers-autoprobe attributes.

* Bus device-list reference callbacks.

* Blocking notifier and lock dependency initialization primitives.

* Device, kset, klist, mutex, and sysfs operations.

* Bus diagnostics and the final private-object release callback.

* Export metadata shared with the complete C fallback.

The native ``drivers/base/bus.o`` object contains no body for the two public
bus lifecycle entries. The fallback and C-only objects contain the complete
C implementation.

Important invariants
--------------------

Bus-private storage is zeroed before initialization. The main embedded kset
is initialized before its name is allocated, then bound to the static
``/sys/bus`` kset and bus kobject type. Automatic driver probing starts
enabled.

The main bus kset is registered before the uevent and probe attributes. The
``devices`` and ``drivers`` child ksets are then created under the registered
bus kobject. Only after both child ksets exist does registration initialize
the interface list, lock key, mutex, and device and driver klists.

Registration fails cleanly at every step. Failures after main kset
registration unregister child ksets, remove the uevent attribute, and
unregister the main kset. The main kset release callback then unregisters the
lock key and frees private storage.

Unregistration looks up the private object with a reference. It unregisters
any device root, removes bus groups, probe attributes, and the uevent
attribute, then unregisters the driver and device child ksets and the main
kset. The final lookup reference is released after the main kset has left the
registry. A null or already unregistered bus is a no-op.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_BUS=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Static registration, registration status, kset lookup, idempotent
  unregistration, null unregistration, duplicate name rejection, and
  rollback after late bus group creation failure.

* C-only KASAN with the complete C fallback.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Adjacent device-model, kobject, kset, klist, list, and reference suites.

* Symbol checks that Rust defines both public entries and that fallback and
  C-only objects define the complete C implementation.

* Export checks that every configuration retains ``EXPORT_SYMBOL_GPL`` for
  both entries.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``5577fd4ed555``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_BUS=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_BUS=n``
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

All nine QEMU runs passed all three bus cases. Native, fallback, and C-only
runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs used
four virtual CPUs.

The focused cases cover static registration, registration status, kset
lookup, idempotent unregistration, null unregistration, duplicate name
rejection, and rollback after bus group creation fails because the
``devices`` child directory already exists. The recovery bus uses the same
name as the failed bus to verify that rollback released all sysfs state.

The device-model smoke tests passed 17 cases in each of the native, fallback,
and C-only runs. The 17 cases contain 2 root-device cases, 8 platform-device
cases, 4 class cases, and 3 bus cases.

The native four-CPU adjacent regression passed 13 kobject cases, 39 list
cases, 18 hlist cases, 11 klist cases, 7 refcount cases, 4 kref cases, and
4 lockref cases. The combined adjacent total is 96 cases.

The focused boots contained no BUG, KASAN report, lock inversion, kernel
panic, or RCU stall. The negative tests intentionally emitted the duplicate
bus name and duplicate sysfs group diagnostics. These messages are expected
test output.

The KASAN run used the x86 GCC configuration. That configuration forces
``CONFIG_RUST=n`` when KASAN is enabled. The run therefore validates the
complete C fallback with KASAN instrumentation. It is not a native Rust KASAN
run.

The native ``rust/kernel.o`` defined both public symbols. The native
``drivers/base/bus.o`` defined neither symbol. The fallback and C-only
``drivers/base/bus.o`` objects each defined the complete two-function C
implementation.

The final native kernel contained exactly two matching strong definitions,
one for each public entry. Each native, fallback, and C-only
``Module.symvers`` file recorded both entries as ``EXPORT_SYMBOL_GPL``.
Native Rust generated GPL exports for the same two symbols.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Focused Sphinx generation emitted only one
existing unrelated cross-reference warning. Strict checkpatch reported only
the generic ``FILE_PATH_CHANGES`` warning for newly added files. With that
expected warning ignored, checkpatch reported no errors, warnings, or checks.
