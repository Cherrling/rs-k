.. SPDX-License-Identifier: GPL-2.0

Native Rust device lifecycle
============================

Status and scope
----------------

The ``CONFIG_RUST_DEVICE`` option selects native Rust implementations for
device initialization, addition, registration, deletion, and
unregistration. The option depends on ``CONFIG_RUST_DRIVER``.

Rust directly defines the public entry points. It owns the private-object
allocation, naming, parent-reference acquisition, kobject transaction,
subsystem attachment, probe activation, failure rollback, and removal
ordering. C retains the shared device and private layouts, parent selection,
sysfs, class, bus, PM, and devtmpfs helpers, device links, diagnostics, the
final release callback, and the original export metadata.

The complete C implementation remains available when the option is disabled
or when its Rust dependencies are disabled.

Entry points and ownership
--------------------------

The public interfaces keep their existing signatures and exports. When the
option is enabled, Rust directly defines:

* ``device_initialize()``

* ``device_add()``

* ``device_register()``

* ``device_del()``

* ``device_unregister()``

Rust owns:

* Null-device and missing-name validation.

* Zeroed device-private allocation and initialization.

* Kobject type binding and global device-kset selection.

* Device mutex, override, devres, device-link, and PM initialization order.

* Kobject initialization, lock state, NUMA assignment, and coherent-DMA setup.

* Initial-name transfer and bus-based fallback naming.

* Parent reference acquisition, glue-directory selection, and NUMA
  inheritance.

* Kobject registration and platform notification.

* Uevent, class symlink, attribute, bus, PM sysfs, and PM state attachment.

* Device-number attributes, ``/sys/dev`` entries, and devtmpfs setup.

* Bus probe notification, firmware-link creation, and the ready-to-probe
  transition.

* Device probing and firmware-link consumer release after probe.

* Parent-child list insertion and class-device registration.

* Reverse-order rollback at each addition failure stage.

* The complete device deletion and unregistration order.

Retained C boundaries
---------------------

The C boundary retains:

* The ``struct device``, ``struct device_private``, and related shared
  layouts.

* The static device kobject type and its final release callback.

* Parent kobject selection, glue-directory management, and class lookup.

* Kobject, klist, bus, class, PM, sysfs, uevent, and devtmpfs primitives.

* Device links and firmware-link implementation.

* Devres release and managed-resource accounting.

* Diagnostics shared with the native transaction.

* Export metadata shared with the complete C fallback.

The native ``drivers/base/core.o`` object contains no body for the five
public device lifecycle entries. Native Rust defines those entries in
``rust/kernel.o``. The fallback and C-only objects retain the complete C
implementation.

Important invariants
--------------------

Initialization binds the caller-owned device storage to the static device
kobject type before adding any state that a release path can observe. The
devres list is initialized before PM and bus setup. This prevents device
addition and driver binding from using an uninitialized devres list.

Registration allocates private storage only when the caller has not already
initialized it. The initial name is copied before the pointer is cleared.
If no initial name exists, the bus enumeration prefix and device ID provide
the fallback name. A device without either form of name is rejected.

Addition retains a temporary device reference for the transaction. It also
retains the selected parent device reference and any glue-directory
reference until deletion or rollback. Kobject registration happens before
platform notification and every sysfs or subsystem attachment.

The order after the kobject is registered is the uevent attribute, class
symlinks, device attributes, bus attachment, PM sysfs state, PM state, and
optional device-number entries. Bus notification and the add uevent follow
those attachments. Firmware links are created before probing can observe the
device.

The device lock protects the transition to ready-to-probe. This ordering
ensures that a concurrent driver probe cannot pass the readiness check early.
After probing, Rust inserts the device into its parent's child list and
notifies class interfaces while holding the class subsystem mutex.

Every fallible addition stage records the furthest completed operation.
Rollback removes later state in reverse order. A failure after creating the
device-number attribute removes that attribute before PM, bus, attribute,
symlink, uevent, platform, kobject, parent, and private state are released.
A failure before kobject registration cleans only the selected glue
directory, parent reference, and private allocation.

Deletion marks the device dead under the device lock, clears its firmware
node, and enters the no-I/O allocation scope. It removes the parent-list
node and device-number entries before class, uevent, attributes, bus, PM,
deferred-probe, platform, device-link, and devres state. Final notifications,
kobject removal, glue-directory cleanup, and parent-reference release use the
same registration order reversed.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_DEVICE=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Split initialization and addition, combined registration, duplicate name
  rejection, parent-child registration, missing-name rejection, bus-group
  rollback, and device-number sysfs-entry rollback.

* C-only KASAN with the complete C fallback.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Adjacent device-model, class, bus, driver, kobject, klist, list, and
  reference suites.

* Symbol checks that Rust defines all five public entries and that the native
  C object defines none of them.

* Export checks that every configuration retains ``EXPORT_SYMBOL_GPL`` for
  all five entries.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used parent commit ``984df48944b5``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_DEVICE=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_DEVICE=n``
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

All nine QEMU runs passed all seven device cases. Native, fallback, and
C-only runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs
used four virtual CPUs.

The focused cases cover split initialization, addition, and deletion,
combined registration and unregistration, duplicate name rejection,
parent-child registration, missing-name rejection, bus-group failure
rollback, and ``/sys/dev`` link failure rollback. The bus-group test recovers
with the same name as the failed device. The device-number test creates a
duplicate ``char/4095:0`` link and then registers a same-name device without a
device number. Both recovery cases prove that the failed kobject transaction
is reusable.

The native device-model suites passed 17 cases. These cases contain 2
root-device cases, 8 platform-device cases, 4 class cases, and 3 bus cases.
The native driver suite passed 5 cases. The native four-CPU adjacent
regression passed 13 kobject cases, 39 list cases, 18 hlist cases, 11 klist
cases, 7 refcount cases, 4 kref cases, and 4 lockref cases. The combined
adjacent total is 118 cases.

The focused boots contained no BUG, KASAN report, lock inversion, kernel
panic, or RCU stall. The negative tests intentionally emitted duplicate
device-name, duplicate sysfs-group, and duplicate device-number diagnostics.
These messages are expected test output.

The KASAN run used the x86 GCC configuration. That configuration forces
``CONFIG_RUST=n`` when KASAN is enabled. The run therefore validates the
complete C fallback with KASAN instrumentation. It is not a native Rust KASAN
run.

The native ``rust/kernel.o`` defined all five public symbols. The native
``drivers/base/core.o`` object defined none of them. The fallback and C-only
``drivers/base/core.o`` objects each defined the complete five-function
implementation.

The final native kernel contained exactly one matching strong definition for
each public entry. Each native, fallback, and C-only ``Module.symvers`` file
recorded all five entries as ``EXPORT_SYMBOL_GPL``.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Focused Sphinx generation emitted existing
unrelated reference warnings. Strict checkpatch, with the generic
``FILE_PATH_CHANGES`` warning ignored, reported no errors, warnings, or
checks.
