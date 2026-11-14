.. SPDX-License-Identifier: GPL-2.0

Native Rust Structural Commit for VMA Merging
=============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MERGE_COMMIT`` option selects native Rust for the
structural commit shared by VMA merge paths. The original C implementation
remains available as a fallback.

The commit is used by existing-range merges, new-range merges, and
``vma_expand()``. When the existing-range orchestration is native Rust, it
calls the Rust commit directly. The C ``commit_merge()`` adapter calls the
same Rust entry for C merge orchestration.

Rust owns these decisions:

1. Whether to commit the middle VMA or the target VMA.
2. Which start and end addresses configure the VMA iterator.
3. The lifetime of ``struct vma_prepare``.
4. When to initialize the multi-VMA preparation state.
5. When to perform maple-tree preallocation.
6. Whether preallocation failure aborts before VMA mutation.
7. When to prepare locks and depart from the preallocation-only path.
8. When to adjust huge pages and write the final VMA range.
9. When to adjust the retained middle or next VMA range.
10. When to overwrite the maple-tree entry and complete the operation.

The preallocation ordering preserves the ``give_up_on_oom`` contract. A
preallocation failure returns the original error before any VMA changes.

Retained C boundary
-------------------

The C helpers expose one state access, one setter, or one structural
primitive. Rust selects the order of the operations.

The C boundary contains:

* Merge-state and VMA field access.
* VMA iterator configuration.
* Maple-tree preallocation and storage.
* VMA preparation and completion.
* VMA range and page-offset updates.
* Transparent huge-page adjustment.
* Reverse-map, file-map, uprobe, and lock updates reached by VMA completion.
* Architecture hooks reached by VMA completion.
* Debug and lock invariants.

The caller continues to hold the mmap write lock. C owns the implementation of
the individual primitives. C does not choose the commit flow.

Entry points and ownership
--------------------------

The exported Rust entry is ``rust_vma_merge_commit()``. It creates a
Rust-owned ``struct vma_prepare`` value on the kernel stack and passes a
pointer to the narrow C operations.

The C definition of ``struct vma_prepare`` is available through
``include/linux/vma-merge-commit-rust.h``. Rust does not access its fields.
Rust owns its storage and passes it to C only while the commit is active.

The C ``commit_merge()`` function becomes an adapter when the option is
enabled. The original C implementation is compiled when the option is
disabled.

Ordering and invariants
-----------------------

Rust reads the internal ``__adjust_next_start`` decision. For that case, Rust
selects the middle VMA and configures the iterator for the retained next-VMA
range. Otherwise, Rust selects the target VMA and configures the iterator for
the proposed merge range.

Rust then follows this order:

1. Initialize the multi-VMA preparation object.
2. Preallocate maple-tree storage for the selected VMA.
3. Return immediately if preallocation fails.
4. Prepare the VMA locks and adjacent-VMA state.
5. Adjust huge pages.
6. Write the final range and page offsets to the selected VMA.
7. Adjust the retained middle or next VMA.
8. Overwrite the maple-tree entry.
9. Complete file, reverse-map, uprobe, lock, and accounting work.

Rust returns zero after completion. Past successful preallocation, the commit
path does not report an error.

Testing
-------

The existing VMA merge suite exercises every structural branch:

* Existing-range left, right, and two-sided merges.
* Existing-range middle and next-VMA range adjustments.
* New-range left, right, and two-sided merges.
* ``brk`` expansion through ``vma_expand()``.
* Anonymous-VMA duplication and sticky-flag preservation.
* Shared and private file mappings.
* Incompatible flags, file offsets, and anonymous names.

The VMA-unmap suite exercises mapping, splitting, replacement, heap changes,
and concurrent map and unmap cycles. The VMA-unmap KUnit suite checks aligned
range operations.

Verification results
--------------------

Each configuration ran the merge and unmap suites three times.

.. list-table::
   :header-rows: 1

   * - Configuration
     - Commit path
     - Merge result
     - VMA-unmap result
     - KUnit result
   * - Native Rust production
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - C commit fallback
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - Native Rust KUnit
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
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
   * - C merge orchestration with Rust commit
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3

All QEMU runs used four vCPUs. Userspace runs used 1536 MiB of RAM. KUnit runs
used 1024 MiB, except KASAN which used 1536 MiB. KVM was the accelerator.

The KASAN configuration also enabled ``CONFIG_DEBUG_VM``,
``CONFIG_DEBUG_VM_MAPLE_TREE``, and ``CONFIG_DEBUG_MAPLE_TREE``. Every KUnit
round reported ``ok 1 vma_unmap``. The log scan found no BUG, kernel warning,
KASAN report, lockdep complaint, RCU stall, oops, failed test, or unexpected
panic.

Build and ownership checks:

* All seven configurations used ``CONFIG_WERROR=y`` and completed with LLVM.
* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks after
  ignoring the expected ``FILE_PATH_CHANGES`` advisory.
* Sphinx generated ``Documentation/output/rust/vma-merge-commit.html``.
* The production image contains ``rust_vma_merge_commit`` and every commit
  helper.
* The C fallback image omits the Rust commit entry and helpers.
* The C-orchestration configuration omits the Rust existing-range,
  new-range, and compatibility entries while retaining the Rust commit entry.
* Production disassembly shows ``vma_expand()`` and the native existing-range
  path calling the Rust commit.
* C-orchestration disassembly shows ``vma_expand()`` calling the Rust commit.
* Rust commit disassembly calls the iterator, preallocation, preparation,
  huge-page, range, storage, and completion helpers in the documented order.
* ``nm -u vmlinux`` reports no undefined symbols.
* The production image boots to the expected missing-root-filesystem panic.
  The kernel is not tainted.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Rust gains a native VMA iterator or maple-tree storage interface.
* ``struct vma_prepare`` gains Rust-managed fields or changes ownership.
* Maple-tree preallocation loses its before-mutation guarantee.
* ``vma_complete()`` becomes safe to implement as a Rust-native operation.
* VMA locking, reverse-map updates, file handling, or uprobe handling moves to
  Rust.
* Architecture hooks or huge-page adjustment gain a sound Rust interface.
* A helper starts selecting the commit path instead of performing one
  primitive.
