.. SPDX-License-Identifier: GPL-2.0

Native Rust Page-Table Entry Move Orchestration
===============================================

Status and scope
----------------

The ``CONFIG_RUST_MOVE_PGT_ENTRY`` option selects native Rust for the production
``move_pgt_entry()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. Whether a normal PMD or PUD move needs rmap locks.
2. Whether the move state requests rmap locks for huge-page moves.
3. Rmap-lock acquisition and release around the selected operation.
4. Dispatch to normal PMD, normal PUD, huge PMD, or huge PUD movement.
5. Detection of an invalid page-table entry kind.
6. Move-result propagation.

Retained C boundary
-------------------

The C adapter initializes one opaque page-table move state. Rust receives the
opaque state pointer and does not access the page-table move layout.

The C helpers expose one field decision input or one existing architecture
operation. The boundary contains:

* The existing non-forced rmap-lock request.
* Rmap-lock acquisition and release.
* Normal PMD and PUD page-table moves.
* Transparent huge PMD and PUD moves.
* Architecture and transparent-huge-page capability checks.
* The existing invalid-entry diagnostic.

These operations contain architecture-specific page-table encoding, locking,
TLB flushing, and huge-page handling. Moving those operations to Rust would
replace a narrow architecture boundary with duplicated low-level bit logic.

Ordering and invariants
-----------------------

Normal PMD and PUD moves always take rmap locks. Huge-page moves use the lock
request carried by the page-table move state.

Rust takes rmap locks before the selected operation and drops them after the
operation. The mmap write lock remains held across this path.

Rust dispatches exactly one operation for a valid entry kind. An invalid entry
kind produces the existing warning and a false result.

Testing
-------

The ``copy_vma`` userspace suite includes a 2 MiB-aligned anonymous move. The
source and destination addresses and both VMAs align to the PMD boundary, and
the populated source page forces a normal PMD entry move. The test verifies
the destination data, both VMA ranges, and source removal.

The existing partial-move failure test injects a page allocation failure while
moving page tables. The KASAN configuration verifies error propagation and full
source restoration across the page-table move path.

Validation record
-----------------

Validation completed on 2026-11-15. Five x86_64 kernel configurations built:
production, C fallback, KASAN, lockdep, and PREEMPT_RT. The production, KASAN,
lockdep, and PREEMPT_RT configurations enabled
``CONFIG_RUST_MOVE_PGT_ENTRY``.

The ``copy_vma`` userspace suite passed three out of three QEMU runs on all
five configurations. The PMD-aligned case passed in every run. The KASAN
configuration used Clang, generic inline instrumentation, and page-allocation
fault injection. Its partial-move fault-injection case passed three out of
three runs.

The production configuration also passed three out of three QEMU runs for
each adjacent userspace suite: ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and
``vma_merge``.

A case-sensitive scan covered all 27 QEMU logs. It found no ``BUG``,
``WARNING``, KASAN report, kernel oops, lockdep report, or RCU stall.

The Rust checks passed ``rustfmtcheck``, Clippy, and Rustdoc. Strict
reStructuredText validation, ``git diff --check``, and strict ``checkpatch``
passed. GCC and Clang built the userspace test with ``-Wall -Wextra
-Werror``.

The production image contains one ``rust_move_pgt_entry`` symbol and eight
page-table move helper symbols. Disassembly shows that ``move_page_tables()``
calls ``rust_move_pgt_entry()`` with ``NORMAL_PMD`` on the aligned PMD path.
The C fallback image contains neither the entry symbol nor any page-table move
helper symbol.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Required rmap-lock rules for normal or huge entries change.
* PMD or PUD move return semantics change.
* Transparent huge-page capability handling changes.
* The page-table move state gains a new lock request input.
* An operation starts selecting success, failure, or warning behavior.
