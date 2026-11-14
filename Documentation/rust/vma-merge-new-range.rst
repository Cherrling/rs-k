.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for New-Range VMA Merging
====================================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MERGE_NEW_RANGE`` option selects native Rust for the
production orchestration of a proposed VMA range merge. The original C
implementation remains available as a fallback.

This path is shared by ``mmap()``, ``brk`` expansion, ``mprotect()``, and
``mremap()``. Rust owns these decisions:

1. Whether a special VMA or absent neighbors make the range unmergeable.
2. Whether the proposed range can merge with the previous VMA.
3. Whether the proposed range can merge with the next VMA.
4. Whether a two-sided merge can remove the next VMA.
5. How the proposed range, target VMA, page offsets, and iterator change.
6. Whether the final structural expansion succeeded.
7. The ``NOMERGE`` and ``SUCCESS`` state transitions.

Retained C boundary
-------------------

The helpers expose one VMA predicate or structural operation. Rust selects
when each operation runs.

The C boundary contains:

* Special-flag detection.
* Left and right VMA compatibility predicates.
* VMA start, end, page-offset, and nestability access.
* Structural VMA expansion.
* Reverse-map, anonymous-VMA, uprobe, and lock updates reached by expansion.
* khugepaged registration.
* Debug write-lock and merge-state validation.

Entry points and ownership
--------------------------

The C ``vma_merge_new_range()`` wrapper snapshots the merge state and calls
``rust_vma_merge_new_range()``. The Rust entry updates the shared merge state
in place and returns the expanded target VMA or a null pointer.

On success, Rust returns the target after structural expansion and khugepaged
registration. On failure, Rust returns a null pointer and leaves the
``NOMERGE`` state for callers to inspect.

Ordering and invariants
-----------------------

The merge operation runs with the mmap write lock held. Rust validates that
the proposed range has no middle or target VMA and that the iterator is at or
before the gap.

Rust then applies the C algorithm in this order:

1. Reject special flags or a range with no adjacent VMA.
2. Test the previous VMA.
3. Test the next VMA unless the caller requested expansion only.
4. Extend the range and select the next VMA when a right merge is possible.
5. Extend the range and select the previous VMA when a left merge is possible.
6. Preserve the original end if the next VMA must remain.
7. Reposition the iterator for a normal, non-expand-only merge.
8. Expand the target and register it with khugepaged.
9. Record success.

The C fallback uses the same ordering. A structural expansion failure leaves
the merge state uncommitted and returns no target.

Testing
-------

The userspace suite exercises:

* A new range merging with its right neighbor.
* A new range merging with its left neighbor.
* A new range merging with both neighbors and removing the right VMA.
* Incompatible protections blocking a merge.
* A forked target VMA blocking a merge through the anonymous-VMA rule.
* ``brk`` expansion through the expand-only path.

The suite uses ``/proc/self/maps`` to verify VMA boundaries. The existing
VMA-unmap suite also runs because it exercises new mappings, splits,
replacement, heap changes, and concurrent map and unmap cycles.

Verification results
--------------------

The final validation matrix passed:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Production path
     - Merge result
     - VMA-unmap result
     - KUnit result
   * - Native Rust production
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - Native Rust KUnit
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
   * - C fallback
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - KASAN and DEBUG_VM
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
   * - lockdep
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
   * - PREEMPT_RT
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3

All QEMU runs used four vCPUs. Userspace runs used 1536 MiB of RAM. KUnit runs
used 1024 MiB of RAM, except for KASAN, which used 1536 MiB. KVM was the
accelerator.

The KASAN configuration also enabled ``CONFIG_DEBUG_VM_MAPLE_TREE`` and
``CONFIG_DEBUG_MAPLE_TREE``. No merge-specific KUnit selector was added
because constructing a real address space and VMA iterator requires user-task
state. The dedicated userspace suite exercises the production merge entry, and
the existing KUnit case validates the outer range path.

Build and production checks:

* Production Clippy and all five diagnostic builds completed with
  ``CONFIG_WERROR=y``.
* ``rustfmtcheck``, ``git diff --check``, and shell syntax validation passed.
* Strict checkpatch reported no errors, warnings, or checks.
* The Rust Sphinx build passed with only existing cross-reference and missing
  Graphviz warnings.
* The production image contains ``vma_merge_new_range`` and
  ``rust_vma_merge_new_range``.
* The production image omits the VMA-unmap KUnit selector symbols and reports
  no undefined symbols.
* Disassembly shows the C state wrapper calling
  ``rust_vma_merge_new_range()``. Rust then calls the validation, predicate,
  state, range, iterator, expansion, khugepaged, and success helpers in the
  documented order.
* The C fallback image contains no Rust merge helper symbols and no Rust call
  in ``vma_merge_new_range()``.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, kernel warning, KASAN
  report, RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* VMA compatibility predicates gain stable Rust-safe field access.
* Structural expansion gains a Rust-native maple-tree and reverse-map
  interface.
* Reverse-map, anon-VMA, uprobe, or lock ordering changes.
* khugepaged registration moves to another layer.
* A helper starts selecting merge policy instead of performing one predicate
  or structural operation.
