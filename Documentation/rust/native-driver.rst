.. SPDX-License-Identifier: GPL-2.0

Native Rust device driver lifecycle
===================================

Status and scope
----------------

The ``CONFIG_RUST_DRIVER`` option selects native Rust implementations for
device driver registration and unregistration. The option depends on
``CONFIG_RUST_BUS``.

Rust directly defines the public entry points and the internal bus add and
remove state machines. C retains the shared driver and bus layouts, object
lookup and reference accounting, the static driver kobject type, sysfs
attributes, attach and detach primitives, module links, diagnostics, and the
final private-object release callback.

The complete C implementation remains available when the option is disabled
or when its Rust dependencies are disabled.

Entry points and ownership
--------------------------

The public interfaces keep their existing signatures and exports. When the
option is enabled, Rust directly defines:

* ``driver_register()``

* ``driver_unregister()``

Rust owns:

* Null driver, bus, and name validation.

* Registered-bus validation and obsolete callback diagnostics.

* Duplicate driver-name lookup and rejection.

* Zeroed driver-private allocation.

* Driver-device klist initialization.

* Driver pointer binding and the bus child-kset relationship.

* Driver kobject initialization and registration.

* Bus driver-list insertion and automatic device attachment.

* Module link creation and private-driver debug setup.

* Driver uevent, bus driver groups, bind, and unbind attributes.

* Exact rollback after attachment and module-link failures.

* Driver group creation and late registration rollback.

* Driver-add uevent delivery and deferred-probe timeout extension.

* The complete unregistration order and bus reference release accounting.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct device_driver``, ``struct driver_private``, and
  ``struct subsys_private`` layouts.

* ``driver_find()`` lookup and ``bus_to_subsys()`` reference ownership.

* The static driver kobject type, sysfs operations, and release callback.

* Driver uevent and bind-file attribute definitions.

* Module link implementation.

* Device attach, device detach, and klist operations.

* Kobject, sysfs, uevent, and bus reference primitives.

* The unexpected-unregistration warning site.

* Export metadata shared with the complete C fallback.

The native ``drivers/base/driver.o`` object contains no body for the two
public driver lifecycle entries. The native ``drivers/base/bus.o`` object
also contains no body for the internal ``bus_add_driver()`` or
``bus_remove_driver()`` functions. The fallback and C-only objects contain
the complete C implementation.

Important invariants
--------------------

Registration first validates that the bus is registered and that no driver
with the same name is already present. Driver-private storage is zeroed
before initialization. Its device klist is initialized before the private
object is linked to the caller-provided driver.

The driver kobject is initialized and added below the bus driver kset with
the driver name as its sysfs name. Only after that succeeds does the driver
join the bus driver klist. Automatic probing then attempts to attach the new
driver to existing devices.

If automatic attachment fails, registration removes the bus list node and
releases the private kobject without calling ``driver_detach()``. This
matches the C path. If module link creation fails, registration first detaches
devices because a successful automatic attach may already have bound them.

The uevent attribute, bus driver groups, and optional bind files are created
after the internal driver-add transaction succeeds. The uevent and bus group
errors are nonfatal, matching the C implementation. Failure to create the
driver's own groups removes the complete internal driver registration.

Unregistration removes the caller-provided groups, optional bind files, bus
driver groups, and the uevent attribute. It then removes the bus list node,
detaches devices, removes module links, and releases the private kobject. The
final two bus references release the temporary lookup and the reference
retained by registration.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_DRIVER=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Successful registration and lookup, duplicate name rejection, unregistered
  bus rejection, late driver-group failure rollback, and reuse after
  unregistration.

* C-only KASAN with the complete C fallback.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Adjacent device-model, kobject, kset, klist, list, and reference suites.

* Symbol checks that Rust defines both public entries and that no native C
  object defines either entry or the internal bus add and remove functions.

* Export checks that every configuration retains ``EXPORT_SYMBOL_GPL`` for
  both entries.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``6c715cb47ab1``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_DRIVER=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_DRIVER=n``
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

All nine QEMU runs passed all five driver cases. Native, fallback, and C-only
runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs used
four virtual CPUs.

The focused cases cover successful registration and lookup, duplicate name
rejection, a late failure after the driver uevent file exists, successful
recovery with the same name, reuse after unregistration with the same sysfs
group, and an unregistered bus. The late failure proves that the complete bus
list, module, kobject, and sysfs state is released.

The existing device-model suites passed 17 cases in the native run. These
cases contain 2 root-device cases, 10 platform-device cases, 4 class cases,
and 3 bus cases.

The native four-CPU adjacent regression passed 13 kobject cases, 39 list
cases, 18 hlist cases, 11 klist cases, 7 refcount cases, 4 kref cases, and
4 lockref cases. The combined adjacent total is 96 cases.

The focused boots contained no BUG, KASAN report, lock inversion, kernel
panic, or RCU stall. The negative tests intentionally emitted duplicate
driver-name, duplicate sysfs-group, and unregistered-bus diagnostics. These
messages are expected test output.

The KASAN run used the x86 GCC configuration. That configuration forces
``CONFIG_RUST=n`` when KASAN is enabled. The run therefore validates the
complete C fallback with KASAN instrumentation. It is not a native Rust KASAN
run.

The native ``rust/kernel.o`` defined both public symbols. The native
``drivers/base/driver.o`` and ``drivers/base/bus.o`` objects defined none of
the two public entries or the two internal bus functions. The fallback and
C-only ``drivers/base/driver.o`` objects each defined the complete
two-function public implementation.

The final native kernel contained exactly one matching strong definition for
each public entry. Each native, fallback, and C-only ``Module.symvers`` file
recorded both entries as ``EXPORT_SYMBOL_GPL``. Native Rust generated GPL
exports for the same two symbols.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Focused Sphinx generation emitted one existing
unrelated cross-reference warning. Strict checkpatch, with the generic
``FILE_PATH_CHANGES`` warning ignored, reported no errors, warnings, or
checks.
