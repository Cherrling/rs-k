.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Region Creation
===============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_CREATE`` option selects native Rust for the
production ``__mmap_region()`` high-level path. The original C implementation
remains available as a fallback.

Rust owns these production decisions:

1. Setup success and failure handling.
2. Mmap preparation callback dispatch.
3. Early KSM flag update selection.
4. Adjacent VMA merge attempt selection.
5. New-VMA allocation and user-field completion selection.
6. Mapping completion and deferred action dispatch.
7. Accounting rollback and unmapping abort ordering.

Retained C boundary
-------------------

The C adapter initializes one opaque region state. Each helper performs one
existing operation or exposes one narrow state predicate.

The retained boundary contains:

* VMA iterator, mapping state, and descriptor initialization.
* The existing overlapping-VMA setup operation when Rust setup is disabled.
* The existing ``mmap_prepare`` callback operation.
* The existing KSM flag operation.
* The existing adjacent merge operation.
* The existing new-VMA allocation operation when Rust new-VMA allocation is
  disabled.
* The existing user-field and completion operations.
* The existing deferred mmap action operation.
* Memory-account and file-reference rollback.
* The existing unmapping abort operation.

These helpers do not select a success, error, merge, allocation, completion, or
cleanup branch. Rust performs each selection and orders the helper calls.

When ``CONFIG_RUST_MMAP_REGION_SETUP`` is enabled, setup control flow is
provided by the Rust entry described in :doc:`mmap-region-setup`.

When ``CONFIG_RUST_MMAP_REGION_NEW_VMA`` is enabled, new-VMA allocation
control flow is provided by the Rust entry described in
:doc:`mmap-region-new-vma`.

When ``CONFIG_RUST_MMAP_REGION_COMPLETE`` is enabled, completion control flow
is provided by the Rust entry described in :doc:`mmap-region-complete`.

Ordering and invariants
-----------------------

Rust records whether the file has an ``mmap_prepare`` callback and whether KSM
flags may be checked early before it starts setup.

Setup failure aborts the gathered unmap state without unaccounting memory.
Failure after setup first rolls back charged memory, then releases a changed
file reference, then aborts the gathered unmap state.

After setup succeeds, Rust runs the preparation callback when present. It then
updates KSM flags only when the early-check rule permits it.

Rust attempts an adjacent merge only when setup found a previous or next VMA.
If that attempt returns no VMA, Rust allocates a new VMA. User-defined fields
and deferred actions run only for a newly allocated VMA that used
``mmap_prepare``.

Rust completes the VMA before it runs a deferred action. A deferred action
failure is returned after completion and does not use the aborted-unmap path.
Successful creation returns the mapped address.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* Anonymous, file-backed, shared, private, fixed, locked, droppable, and
  populated mappings.
* File mappings that use ``mmap_prepare``.
* Secret-memory preparation rejection and memory-account rollback.
* Adjacent anonymous mapping, merge, and observable VMA extension.
* Replacement of existing mappings.
* Memory policy failures before creation.

Validation record
-----------------

The migration gates passed on 2026-11-15 against baseline commit
``1c24b11b168f``. All configurations use ``CONFIG_WERROR=y`` and LLVM.

The production Rust image, the region C fallback image, the creation C fallback
image, the mmap entry C fallback image, a KASAN image, a lockdep image, and a
PREEMPT_RT image built successfully. The KASAN image uses
``CONFIG_KASAN_GENERIC=y`` and ``CONFIG_KASAN_INLINE=y``. The lockdep image
uses ``CONFIG_PROVE_LOCKING=y`` and ``CONFIG_LOCKDEP=y``. The PREEMPT_RT
image uses ``CONFIG_PREEMPT_RT=y``.

The ``mmap_entry`` userspace suite passed three times in each of the seven
configurations. The suite exercised adjacent anonymous VMA merging. It also
used a 32 MiB private writable secret-memory mapping to verify preparation
rejection and memory-account rollback. The committed-memory total returned to
its pre-mapping value. A shared secret-memory mapping succeeded.

The production image passed the ``copy_vma``, ``dup_mmap``, ``exit_mmap``,
``vma_unmap``, and ``vma_merge`` suites three times each. The 21 mapping-entry
runs and 15 regression runs had no ``BUG``, ``WARNING``, KASAN report, Oops,
lockdep report, RCU stall, call trace, or kernel panic.

The test program compiled with GCC and Clang using
``-Wall -Wextra -Werror``. Rust format, Clippy, and documentation checks
passed with ``rustfmtcheck``, ``CLIPPY=1``, and ``rustdoc``.

The production ``System.map`` contains one ``rust_mmap_region_create`` entry,
one ``rust_mmap_region`` entry, and 18 creation helpers. Disassembly shows
that the C adapter directly calls ``rust_mmap_region_create``.

The creation fallback image contains neither the Rust creation entry nor its
helpers. The region fallback image contains the creation entry and helpers
but no ``rust_mmap_region`` entry. The mmap entry fallback image contains both
Rust entries.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Setup defines a new error class or changes gathered-unmap ownership.
* ``mmap_prepare`` changes field ownership or callback ordering.
* Early KSM eligibility changes.
* Merge and new-VMA selection changes.
* Completion or deferred action ordering changes.
* Accounting, file-reference, or abort rollback semantics change.
* A helper starts selecting an error or control-flow branch.
