.. SPDX-License-Identifier: GPL-2.0

Native Rust percpu_ref() Migration
==================================

Status and scope
----------------

The ``CONFIG_RUST_PERCPU_REFCOUNT`` option selects an experimental native
Rust implementation of the ``percpu_ref()`` lifecycle.  The migration excludes
device drivers and does not change any caller-facing API.

Rust directly defines all nine production lifecycle entry points. The
complete C lifecycle remains available when the option is disabled.

Rust owns these state transitions:

* Initialization, including the initial reference and flags.
* Switching between per-CPU and atomic counting.
* RCU-assisted kill confirmation.
* Exit and backing allocation release.
* Reinitialization and resurrection.

Entry points and state ownership
--------------------------------

The production entry points are:

* ``percpu_ref_init()``
* ``percpu_ref_exit()``
* ``percpu_ref_switch_to_atomic()``
* ``percpu_ref_switch_to_atomic_sync()``
* ``percpu_ref_switch_to_percpu()``
* ``percpu_ref_kill_and_confirm()``
* ``percpu_ref_is_zero()``
* ``percpu_ref_reinit()``
* ``percpu_ref_resurrect()``

These functions retain their names, signatures, and GPL export class. Rust
directly defines each function. In the native configuration, the original C
lifecycle state machine and its public wrappers are absent from the build.

The Rust state machine uses the existing ``struct percpu_ref`` and
``struct percpu_ref_data`` layouts.  It keeps the existing global switch lock,
RCU callback mechanism, count bias, and confirmation protocol.  It does not
call the original C lifecycle functions.

Retained C boundary
-------------------

The native ``lib/percpu-refcount.o`` object contains only primitive helpers.
It defines none of the nine public lifecycle functions. The following
operations stay in C because they directly use architecture or kernel
primitives:

* ``percpu_ref_get()``, ``percpu_ref_put()``, and ``percpu_ref_tryget_live()``
  stay inline in ``include/linux/percpu-refcount.h``.  These are lockless,
  performance-critical per-CPU and atomic paths.
* Thin helpers allocate and free per-CPU storage and ``percpu_ref_data``.
* Thin helpers acquire the switch lock and use its wait queue.
* Thin helpers perform ``READ_ONCE()``, ``WRITE_ONCE()``, release-store, and
  atomic-long operations.
* Thin helpers sum and clear per-CPU counters.
* Thin helpers register RCU callbacks and recover the enclosing data object.
* Thin helpers report existing warning and underflow diagnostics.

These helpers do not decide lifecycle transitions. The Rust module reads their
results and implements the state machine, including asynchronous RCU
confirmation and final-release ordering.

Important invariants
--------------------

The global switch lock serializes lifecycle changes except for documented
lockless fast paths.  A non-NULL confirmation callback marks an atomic switch
in progress.  The count bias prevents the atomic counter from reaching zero
before all per-CPU counters are collected.  The release store that restores
per-CPU mode pairs with the ``READ_ONCE()`` in ``__ref_is_percpu()``.

The temporary reference acquired during an atomic switch is dropped only after
confirmation.  After the confirmation callback runs, new
``percpu_ref_tryget_live()`` calls must fail.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Native Rust, Rust fallback, and C-only x86_64 builds.
* One-vCPU and four-vCPU runs for every build configuration.
* Generic inline KASAN, lockdep, and PREEMPT_RT builds and boots.
* Initialization in per-CPU, atomic, and dead modes.
* Both mode-switch directions and synchronous confirmation.
* Kill confirmation, final release, reinitialization, and resurrection.
* Post-confirmation rejection by ``percpu_ref_tryget_live()``.
* Concurrent mode switching with four workers.
* Symbol ownership checks for all nine public entry points.
* Module builds and an external module ABI test for the GPL exports.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-24 used base commit ``1be8db2dbb31``. Six x86_64
configurations were built: native Rust, Rust fallback, C-only, generic inline
KASAN, lockdep, and PREEMPT_RT. Native, fallback, C-only, lockdep, and
PREEMPT_RT used GCC 16.2. KASAN used Clang 21.1.8 and LLD. Each configuration
passed one SMP1 boot and one SMP4 boot with seven of seven cases. The 12 boots
executed 84 cases without a failure.

KASAN initialized generic KernelAddressSanitizer. The lockdep image enabled
``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``, ``DEBUG_SPINLOCK``, and
``DEBUG_LIST``. The PREEMPT_RT image reported ``PREEMPT_RT`` in its kernel
banner. The logs contain no ``WARNING``, ``BUG``, Oops, failed test, kernel
panic, KASAN report, lockdep complaint, or RCU stall.

The seven cases cover basic references, both mode-switch directions, initial
atomic state, synchronous confirmation, reinitialization, resurrection,
post-confirmation ``tryget_live`` rejection, and concurrent mode switching.

The native SMP4 image also passed adjacent suites: ``refcount`` with seven
cases, ``dec_and_lock`` with six, ``kref`` with four, ``kobject`` with four,
and ``lockref`` with four. All 25 cases passed.

In native, ``rust/kernel.o`` defines all nine public entry points. The native
``lib/percpu-refcount.o`` contains only the retained primitive helpers and
none of the nine public function bodies. The generated Rust export header and
``vmlinux.symvers`` record exactly nine ``EXPORT_SYMBOL_GPL`` entries.
No internal ``rust_percpu_ref_*`` production name remains.

The fallback and C-only images define the complete C lifecycle in
``lib/percpu-refcount.o``. Their Rust kernel objects contain no public
``percpu_ref_*`` definition.

Native, fallback, and C-only module builds passed modpost. A temporary
external module referenced all nine GPL exports and linked successfully
against each configuration.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and
``git diff --check`` passed. Sphinx emitted only the existing
``kernel-docs`` and ``getting_llvm`` cross-directory warnings. Strict
checkpatch reported no errors, warnings, or checks after targeted ignores
for ``EXPORT_SYMBOL`` and function-argument placement.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same per-CPU, RCU, locking, or
  atomic primitives.
* A supported architecture cannot use the retained inline fast paths.
* A helper starts to contain lifecycle policy instead of primitive operations.
* The refcount protocol gains a new state, callback rule, or memory-ordering
  requirement.
* Benchmarks justify moving a fast path after its safety and performance can
  both be measured.
