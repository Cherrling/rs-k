.. SPDX-License-Identifier: GPL-2.0

Native Rust percpu_ref() Migration
==================================

Status and scope
----------------

The ``CONFIG_RUST_PERCPU_REFCOUNT`` option selects an experimental native
Rust implementation of the ``percpu_ref()`` lifecycle.  The migration excludes
device drivers and does not change any caller-facing API.

The Rust code owns these state transitions:

* Initialization, including the initial reference and flags.
* Switching between per-CPU and atomic counting.
* RCU-assisted kill confirmation.
* Exit and backing allocation release.
* Reinitialization and resurrection.

The C implementation remains the fallback when the option is disabled.

Entry points and state ownership
--------------------------------

The exported ``percpu_ref_*()`` functions remain C ABI adapters.  When the
option is enabled, each adapter has a 9-byte jump to the corresponding
``rust_percpu_ref_*()`` function.  The original C lifecycle state machine is
compiled out.

The Rust state machine uses the existing ``struct percpu_ref`` and
``struct percpu_ref_data`` layouts.  It keeps the existing global switch lock,
RCU callback mechanism, count bias, and confirmation protocol.  It does not
call the original C lifecycle functions.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture or
kernel primitives:

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

These helpers do not decide lifecycle transitions.  The Rust module reads their
results and implements the state machine.

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

Validation record
-----------------

The x86_64 validation used three configurations:

* Rust lifecycle enabled.
* Rust lifecycle disabled, with the original C lifecycle and the same tests.
* Rust lifecycle enabled with generic, inline KASAN.

The test suite covers basic references, both mode-switch directions, initial
atomic state, synchronous confirmation, reinitialization, resurrection,
post-confirmation ``tryget_live`` rejection, and concurrent mode switching.
Every configuration passed all 7 tests.  The Rust KUnit runs used 1 vCPU and
three consecutive 4 vCPU runs.  The C fallback and KASAN runs used 4 vCPUs.

The validation logs contained no ``BUG``, ``Oops``, KASAN report, warning,
underflow report, or panic.  Every public lifecycle entry point was a 9-byte
ABI jump, and the corresponding Rust production symbols were present.
Rustfmt, Clippy, checkpatch, and ``git diff --check`` passed.

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
