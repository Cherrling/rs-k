.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Copying for mremap
==================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_COPY`` option selects native Rust for the production
``copy_vma()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. Anonymous page-offset selection for an unfaulted source.
2. Whether an anonymous source can self-merge.
3. The copied-range uprobe merge restriction for file-backed mappings.
4. Destination overlap rejection.
5. The ``MREMAP_DONTUNMAP`` self-merge restriction.
6. Merge versus duplicate selection.
7. Source-pointer replacement after a self-merge.
8. Reverse-map lock requirement after a merge.
9. VMA, policy, anonymous-state, file, and callback setup order.
10. VMA insertion and reverse-order failure cleanup.

Retained C boundary
-------------------

The C adapter initializes one iterator and merge-state object. Rust receives
an opaque state pointer and performs no direct access to the internal merge
state layout.

The C helpers expose one field access, decision input, or existing operation.
The boundary contains:

* Source anonymous-state and file inspection.
* Anonymous and file page-offset values.
* Merge-state field assignment and adjacent-VMA lookup.
* The existing copied-range merge entry.
* VMA duplication, range assignment, policy duplication, and anonymous-VMA
  cloning.
* File-reference acquisition and the ``vm_ops->open`` callback.
* VMA linking and debug validation.
* Hugetlb reservation repair.
* Close, file, anonymous-VMA, policy, and VMA release operations.

Rust selects all conditional setup and cleanup steps. No helper selects the
merge-versus-duplicate path or the rollback order.

Ordering and invariants
-----------------------

Rust follows this successful merge order:

1. Inspect the source anonymous and file state.
2. Choose anonymous page offsets when the source has not been faulted.
3. Locate adjacent VMAs and reject an overlapping destination.
4. Apply any source-removal restriction for ``MREMAP_DONTUNMAP``.
5. Attempt the copied-range merge.
6. Update the source pointer and reverse-map lock requirement.

Rust follows this successful duplicate order:

1. Duplicate the source VMA.
2. Set the destination range and page offsets.
3. Duplicate the memory policy.
4. Clone the anonymous-VMA state.
5. Acquire the file reference and invoke ``vm_ops->open``.
6. Link the new VMA into the destination address space.

Failure cleanup preserves the original reverse order:

.. list-table::
   :header-rows: 1

   * - Failure point
     - Cleanup
   * - VMA duplication
     - Return without a new VMA.
   * - Policy duplication
     - Release the VMA.
   * - Anonymous-VMA cloning
     - Release the policy and VMA.
   * - VMA linking
     - Repair hugetlb reservations, close callbacks, release the file,
       unlink anonymous state, release the policy, and free the VMA.

Testing
-------

The dedicated userspace suite covers anonymous moves, fixed moves,
``MREMAP_DONTUNMAP``, file-backed moves, adjacent merges, self-merges, and the
``MREMAP_DONTUNMAP`` self-merge rejection.

Validation record
-----------------

The implementation was validated on 2026-11-15 with these results:

* The Rust production configuration passed the dedicated ``copy_vma`` suite
  three times.
* The C fallback, KASAN with ``DEBUG_VM``, lockdep, and PREEMPT_RT
  configurations each passed the dedicated suite three times.
* The Rust production configuration passed the ``dup_mmap``, ``exit_mmap``,
  ``vma_unmap``, and ``vma_merge`` suites three times each.
* The host VMA test binary passed 26 of 26 tests.
* The production image has one ``rust_vma_copy`` entry and 41 C helper
  functions. The C fallback contains neither.
* Disassembly of the production ``copy_vma`` calls ``rust_vma_copy``.
* The production, fallback, KASAN, lockdep, and PREEMPT_RT QEMU logs contain
  no ``BUG``, ``WARNING``, KASAN report, ``Oops``, or RCU stall marker.
* Rustfmt, Clippy, and Rust documentation generation passed.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The copied-range merge contract changes.
* ``MREMAP_DONTUNMAP`` self-merge rules change.
* File or anonymous page-offset selection changes.
* Policy, anonymous-VMA, file, or callback ownership changes.
* VMA linking gains a new failure point.
* A helper starts selecting merge or duplicate behavior.
