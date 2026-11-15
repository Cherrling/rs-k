.. SPDX-License-Identifier: GPL-2.0

Native Rust Copy and Page-Table Move Orchestration for mremap
=============================================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_COPY`` option selects native Rust for the production
``copy_vma_and_data()`` path. The original C implementation remains available
as a fallback.

Rust owns these decisions:

1. VMA-copy failure handling and accounting correction.
2. Iterator invalidation after a copied-range self-merge.
3. Publication of the copied source and destination VMAs.
4. Page-table move completion.
5. The file or special-mapping ``mremap`` callback decision.
6. Moving page-table entries back after a partial move or callback failure.
7. Accounting correction before the destination is selected for unmapping.
8. Destination remap-state transitions after a failure.
9. Userfaultfd preparation after a successful move.
10. Hugetlb reservation repair and output publication.

Retained C boundary
-------------------

The C adapter initializes one remap operation object and one page-table move
control object. The Rust entry receives an opaque state pointer and does not
access the internal remap or page-table control layouts.

The C helpers expose one field access, decision input, or existing operation.
The boundary contains:

* VMA page-offset calculation and the native Rust VMA copy entry.
* Remap-state and iterator invalidation field access.
* Source and destination VMA publication.
* Architecture page-table movement.
* The ``vm_ops->mremap`` callback.
* Memory accounting and userfaultfd preparation.
* Hugetlb reservation repair.
* Output-pointer publication.

No helper selects the success, partial-move failure, callback failure, or
rollback path. Rust owns every branch and the rollback order.

Ordering and invariants
-----------------------

Rust follows this successful order:

1. Copy the source VMA to the destination range.
2. Invalidate the caller iterator when a self-merge replaced the source.
3. Publish the source and destination VMA pointers.
4. Move all page-table entries.
5. Invoke the destination VMA ``mremap`` callback.
6. Prepare the userfaultfd context.
7. Repair hugetlb reservations.
8. Publish the new VMA to the caller.

Rust follows this failure order:

1. Stop immediately when the VMA copy fails.
2. Undo the memory charge and publish a null destination when no VMA exists.
3. Stop after the partial page-table move or failed callback.
4. Move the reached page-table entries back to the source range.
5. Undo the move expansion charge unless ``MREMAP_DONTUNMAP`` keeps the
   source.
6. Select the destination VMA, length, and address for caller cleanup.
7. Repair hugetlb reservations and publish the destination VMA.

The source VMA write lock and mmap write lock remain held across this path.
The rollback page-table move takes reverse-map locks.

Testing
-------

The dedicated ``copy_vma`` suite moves populated anonymous and file-backed
ranges, verifies the page contents at the destination, and checks the source
and destination VMAs. It also covers fixed-address expansion, fixed-address
shrinking, the successful ``vm_ops->mremap`` callback through the vDSO
mapping, a failing partial AIO-ring move that exercises page-table rollback
and caller destination cleanup, ``MREMAP_DONTUNMAP``, copied-range
self-merges, adjacent merges, and the ``MREMAP_DONTUNMAP`` self-merge
rejection.

The KASAN fault-injection configuration also moves a populated range that
spans two PGDs. Stack-filtered page allocation failure stops the move between
the two PGDs. The test verifies that the first page's physical frame returns
to the source, its PTE keeps the soft-dirty marker, the second page never
moves, and the destination VMA is removed.

Reproduce this optional case with ``CONFIG_CHECKPOINT_RESTORE``,
``CONFIG_MEM_SOFT_DIRTY``, ``CONFIG_FAULT_INJECTION``,
``CONFIG_FAULT_INJECTION_DEBUG_FS``,
``CONFIG_FAULT_INJECTION_STACKTRACE_FILTER``, and
``CONFIG_FAIL_PAGE_ALLOC`` enabled.

Validation record
-----------------

The implementation was validated on 2026-11-15 with these results:

* The native Rust production, C fallback, KASAN with ``DEBUG_VM``, lockdep,
  and PREEMPT_RT configurations each passed the dedicated suite three times.
* The KASAN configuration passed the injected partial-move rollback case three
  times. The other configurations report that this optional fault-injection
  case is skipped.
* The native Rust production configuration passed the ``dup_mmap``,
  ``exit_mmap``, ``vma_unmap``, and ``vma_merge`` suites three times each.
* The test source passed GCC and Clang builds with ``-Wall -Wextra -Werror``.
* Production Clippy, ``rustfmtcheck``, ``rustdoc``, and ``git diff --check``
  passed.
* The new document passed an isolated Sphinx build with warnings treated as
  errors and passed strict reStructuredText validation.
* Strict checkpatch reports no errors or warnings after ignoring the expected
  new-file advisory. ``scripts/get_maintainer.pl`` matches every added file.
* The production image contains one ``rust_mremap_copy`` entry and all 17 C
  helper functions. The C fallback contains none of these symbols.
* Production disassembly of ``move_vma`` calls ``rust_mremap_copy`` directly.
  Rust disassembly calls all 17 helpers in the documented order.
* C fallback disassembly of ``move_vma`` calls ``move_page_tables`` directly
  for both the forward move and rollback.
* The final QEMU logs contain no ``BUG``, ``WARNING``, KASAN report, ``Oops``,
  lockdep complaint, or RCU stall marker.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The page-table move control contract or reverse-map locking changes.
* The ``vm_ops->mremap`` callback contract changes.
* ``MREMAP_DONTUNMAP`` accounting rules change.
* Userfaultfd preparation ordering changes.
* Hugetlb reservation repair moves or gains a new failure point.
* A helper starts selecting success, failure, or rollback behavior.
