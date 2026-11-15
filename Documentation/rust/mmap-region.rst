.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Region Creation Entry
=====================================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION`` option selects native Rust for the production
``mmap_region()`` entry path. The original C implementation remains available
as a fallback.

Rust owns these production decisions:

1. Mmap write-lock assertion ordering.
2. Memory-Deny-Write-Execute rejection.
3. Architecture VMA flag validation and rejection.
4. Shared writable file mapping selection.
5. Writable address-space registration and failure propagation.
6. Core VMA region creation dispatch.
7. Writable address-space cleanup on success and failure.
8. Final address-space validation.
9. Return-value propagation.

Retained C boundary
-------------------

The C adapter initializes one opaque region state. Rust owns control flow and
error selection. Each helper exposes one predicate, one accounting transition,
or one existing operation.

The retained boundary contains:

* The mmap write-lock assertion.
* The MDWE predicate.
* The architecture VMA flag predicate.
* The shared writable mapping predicate.
* Address-space writable registration and release.
* The existing ``__mmap_region()`` creation operation.
* Address-space debugging validation.
* Request and result field storage.

The helper for the existing creation operation includes VMA setup, adjacent
merge, new-VMA allocation, completion, accounting, and callback handling.
Migrating that operation remains a separate core unit.

Ordering and invariants
-----------------------

The caller must hold the mmap write lock. Rust asserts that lock before it
tests MDWE or architecture flags.

An MDWE conflict returns ``-EACCES``. An architecture flag conflict returns
``-EINVAL``. No writable address-space registration occurs before these checks
succeed.

Rust registers a writable address-space only for a file mapping whose VMA flags
are shared and may-write. Registration failure returns the existing error.

After registration, Rust invokes the existing region creation operation. It
releases the writable registration exactly once after that operation returns,
including the error path. Rust validates the address space after cleanup and
returns the original region result.

Testing
-------

The ``mmap_entry`` userspace suite exercises this entry through normal mmap
system calls. It covers:

* Successful writable shared file mappings.
* Sealed memfd writable mapping rejection.
* MDWE rejection of writable executable mappings.
* MDWE acceptance of readable executable mappings.
* Anonymous, fixed, file, locked, droppable, and populated mappings that reach
  the existing creation operation.

Validation record
-----------------

The migration gates passed on 2026-11-15 against baseline commit
``38a892f50254``. All configurations use ``CONFIG_WERROR=y`` and LLVM.

The production Rust image, the region C fallback image, the mmap entry C
fallback image, a KASAN image, a lockdep image, and a PREEMPT_RT image built
successfully. The KASAN image uses ``CONFIG_KASAN_GENERIC=y`` and
``CONFIG_KASAN_INLINE=y``. The lockdep image uses ``CONFIG_PROVE_LOCKING=y``
and ``CONFIG_LOCKDEP=y``.

The ``mmap_entry`` userspace suite passed three times in each of the six
configurations. The suite exercised the new writable shared file mapping case
and both MDWE outcomes through normal mmap system calls.

The production image passed the ``copy_vma``, ``dup_mmap``, ``exit_mmap``,
``vma_unmap``, and ``vma_merge`` suites three times each. The 33 validation
logs had no ``BUG``, ``WARNING``, KASAN report, Oops, lockdep report, RCU
stall, call trace, or kernel panic.

The test program compiled with GCC and Clang using
``-Wall -Wextra -Werror``. Rust format, Clippy, and documentation checks
passed with ``rustfmtcheck``, ``CLIPPY=1``, and ``rustdoc``.

The production ``System.map`` contains one ``rust_mmap_region`` entry and nine
retained-boundary helpers. Disassembly shows that ``mmap_region()`` directly
calls ``rust_mmap_region``. The mmap entry fallback image also contains this
entry. The region fallback image contains neither the Rust entry nor its
helpers.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Mmap lock ownership or assertion ordering changes.
* MDWE or architecture flag policy changes.
* Shared writable mapping classification changes.
* Address-space writable accounting changes.
* Region creation returns before all VMA, file, or accounting work is complete.
* Cleanup or validation ordering changes.
* A helper starts selecting an error or control-flow branch.
