.. SPDX-License-Identifier: GPL-2.0

Native Rust Page-Table Range Move
=================================

Status and scope
----------------

The ``CONFIG_RUST_MOVE_PAGE_TABLES`` option selects native Rust for the
production ``move_page_tables()`` path. The original C implementation remains
available as a fallback.

Rust owns these production decisions:

1. The empty-range and hugetlb-VMA exits.
2. Range termination and address progression.
3. PUD and PMD extent calculation and clipping.
4. Normal PUD and PMD size selection.
5. Transparent huge PUD and PMD dispatch and split fallback.
6. Missing and empty source page-table handling.
7. PUD, PMD, and PTE allocation failure exits.
8. PTE move retry handling and loop reinitialization.
9. Moved-byte accounting after realignment or failure.

Retained C boundary
-------------------

Rust receives an opaque range-move state. C helpers expose one architecture
operation, page-table primitive, state transition, or field projection.

The retained boundary contains:

* Hugetlb page-table moves.
* PMD-boundary address realignment.
* Cache flushing and MMU notifier start and end operations.
* PUD and PMD lookup and allocation.
* Normal and huge page-table move operations.
* Huge PMD splitting.
* PTE allocation and moves.
* PUD and PMD constant, mask, and capability queries.
* Scheduler rescheduling.

These operations use architecture page-table types, locks, TLB flushing, cache
maintenance, and notifier state. Keeping them in C preserves the architecture
boundary while Rust owns the range algorithm.

Ordering and invariants
-----------------------

An empty range returns zero before any VMA access. A hugetlb VMA moves through
the existing hugetlb operation and returns its moved length.

For a normal VMA, C realigns both addresses to a PMD boundary when the existing
alignment rules permit it. Rust then starts cache and MMU notifier coverage
before entering the move loop.

Rust attempts PUD-level movement before PMD-level movement. A huge PUD uses a
transparent huge page move only for a huge PUD-sized extent. A normal PUD uses
a normal move only when architecture support exists and the extent is PUD
sized.

Rust retries the same PMD and extent after ``move_ptes()`` requests another
attempt. A huge PMD is split when a direct huge move is unavailable. Empty
source entries advance without allocation of lower page tables.

PUD or PMD allocation failure exits the loop without moving the current
extent. Rust always closes the MMU notifier range before it calculates the
moved-byte result.

Testing
-------

The ``copy_vma`` userspace suite includes a PMD-aligned move that exercises the
normal PMD fast path. It also includes a three-page move that crosses a PMD
boundary. That move exercises limited extents, address realignment, and the
PTE path while preserving page data and VMA bounds.

The existing allocation-failure case verifies that a partial move returns
``ENOMEM``, restores source page tables and data, and leaves the destination
unmapped.

Validation record
-----------------

Validation completed on 2026-11-15.

The implementation passed these build configurations:

* Production Rust configuration.
* C fallback with ``RUST_MOVE_PGT_ENTRY`` and
  ``RUST_MOVE_PAGE_TABLES`` disabled.
* KASAN generic inline mode with page-allocation fault injection.
* Lockdep with ``CONFIG_PROVE_LOCKING=y``.
* PREEMPT_RT with ``CONFIG_PREEMPT_RT=y``.

The ``copy_vma`` suite passed three consecutive boots on each configuration.
The KASAN runs also passed the partial-move failure injection on all three
boots. The test restored the source page tables and data. It left the
destination unmapped.

The production configuration passed three consecutive boots of each adjacent
suite: ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and ``vma_merge``.
The 27 validation logs had no BUG, WARNING, KASAN, Oops, lockdep, RCU-stall, or
panic report.

Rustfmt, Clippy, and rustdoc passed. The C test passed GCC and Clang builds
with ``-Wall -Wextra -Werror``. The documentation passed strict reStructuredText
rendering.

The production image contains one ``rust_move_page_tables`` entry and 32
page-table helper symbols. Its disassembly calls ``rust_move_page_tables``
from ``move_page_tables``. The C fallback image contains neither the Rust entry
nor the helpers.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Hugetlb move classification or return semantics change.
* Page-table address realignment rules change.
* MMU notifier or cache-maintenance ordering changes.
* PUD or PMD lookup, allocation, or capability rules change.
* Huge PUD or PMD move and split semantics change.
* PTE allocation, move, or retry semantics change.
* Extent clipping or moved-byte accounting rules change.
* A helper starts selecting success, failure, or loop-continuation behavior.
