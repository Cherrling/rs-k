.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for Splitting VMAs
============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_SPLIT`` option selects native Rust for the production
``split_vma()`` and ``__split_vma()`` paths. The original C implementation
remains available as a fallback.

Rust owns these decisions:

1. The map-count limit check in the public split path.
2. The invalid-boundary warnings before any callback or allocation.
3. The ``may_split`` callback decision and its error result.
4. VMA duplication and the range of the new leading or trailing VMA.
5. Page-offset adjustment for a trailing VMA.
6. Iterator range configuration and maple-tree preallocation.
7. Policy duplication before the anonymous-VMA clone.
8. File-reference acquisition and the ``vm_ops->open`` decision.
9. Write-lock order for the original and new VMAs.
10. Preparation-state initialization.
11. Huge-page and hugetlb boundary handling.
12. The original VMA range and page-offset update.
13. Structural completion, debug validation, and iterator repositioning.
14. Reverse-order cleanup after each allocation or initialization failure.

The public path checks the map-count limit before the internal split. The
internal path skips that check because its callers have completed the check or
the split cannot fail for that reason.

Retained C boundary
-------------------

The C helpers expose a single-purpose field access, callback, allocation, or
existing primitive. Rust selects when each operation runs, including the
callback and file-open conditions, and selects the cleanup order.

The C boundary contains:

* VMA and address-space field access.
* The ``may_split``, ``vm_ops->open``, and policy callbacks.
* VMA allocation and release.
* Policy duplication and release.
* Anonymous-VMA cloning.
* File-reference acquisition.
* Mmap write-lock primitives.
* Maple-tree iterator and preallocation primitives.
* VMA preparation and completion.
* Transparent huge-page and hugetlb operations.
* Debug address-space validation.

These helpers must not select split policy. A helper that adds a branch for
leading versus trailing splits must move that branch into Rust.

Entry points and ownership
--------------------------

The C ``split_vma()`` adapter calls ``rust_vma_split()`` with the map-count
check enabled. The C ``__split_vma()`` adapter calls the same Rust entry with
the check disabled.

The exported Rust entry is ``rust_vma_split()``. When the option is disabled,
the two original C functions are compiled and the Rust entry and helpers are
absent.

The caller must hold the mmap write lock. The original VMA and iterator must
remain live for the complete operation. The split address must lie strictly
inside the original VMA.

Ordering and rollback invariants
--------------------------------

The public path performs the map-count check first. Both paths then warn about
an invalid boundary and call ``may_split`` before allocation.

Rust uses this successful operation order:

1. Duplicate the original VMA.
2. Set the new VMA to the leading or trailing range.
3. Adjust the trailing VMA page offset.
4. Configure the iterator for the new range.
5. Preallocate the iterator insertion.
6. Duplicate the memory policy.
7. Clone the anonymous-VMA state.
8. Acquire the file reference and invoke ``vm_ops->open``.
9. Write-lock the original and new VMAs.
10. Initialize and run VMA preparation.
11. Split transparent huge-page and hugetlb state at the boundary.
12. Adjust the original VMA range and leading-VMA page offset.
13. Complete the structural update.
14. Validate the address space.
15. Move the iterator to the next VMA for a leading split or to the previous
    VMA for a trailing split.

Failure cleanup preserves the original C order:

.. list-table::
   :header-rows: 1

   * - Failure point
     - Cleanup
   * - Map-count limit
     - Return ``-ENOMEM`` before any mutation.
   * - ``may_split``
     - Return the callback error before allocation.
   * - VMA duplication
     - Return ``-ENOMEM`` without a new VMA.
   * - Iterator preallocation
     - Release the new VMA.
   * - Policy duplication
     - Release the iterator preallocation, then the new VMA.
   * - Anonymous-VMA clone
     - Release the new policy, iterator preallocation, and new VMA.

No allocation or callback is repeated during cleanup. The structural commit
has no failure return after preparation starts, so the success path has no
post-commit rollback.

Data and huge-page cases
------------------------

For file-backed VMAs, Rust acquires a reference for the new VMA before calling
``vm_ops->open``. For anonymous VMAs, policy duplication precedes the
anonymous-VMA clone. Both cases use the same range, locking, completion, and
iterator ordering.

Every split calls the transparent huge-page boundary helper with the original
start and split address. Hugetlb VMAs also call the hugetlb split helper after
normal preparation. A hugetlb ``may_split`` rejection returns before any VMA
allocation or range change.

Testing
-------

The VMA merge userspace suite exercises leading, trailing, and two-sided
splits for anonymous and file-backed mappings. It covers shared and private
``memfd`` mappings, faulted anonymous mappings, anonymous names, and forked
anonymous-VMA state.

The VMA unmap userspace suite exercises splits caused by partial front, middle,
and tail unmapping, multiple VMA removal, ``MAP_FIXED`` replacement, ``brk``
shrink, and concurrent map and unmap loops.

Each configuration ran both userspace suites three times. The KASAN, lockdep,
and PREEMPT_RT configurations also ran the VMA-unmap KUnit case three times.
The matrix built with ``CONFIG_TRANSPARENT_HUGEPAGE`` disabled, so it does not
claim a runtime THP split case. The hugetlb branch was compiled because hugetlb
support was enabled, but the matrix did not reserve huge pages for a separate
hugetlb runtime case.

Implementation coverage checks inspect the production and fallback images.
They verify the production adapter calls the Rust entry, the split helper call
order, the C fallback symbols, and the option-isolated build.

Verification results
--------------------

.. list-table::
   :header-rows: 1

   * - Configuration
     - Split path
     - Merge suite
     - Unmap suite
     - KUnit
   * - Native Rust production
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - C fallback
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - Rust split only
     - Rust
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

All six configurations completed with LLVM and ``CONFIG_WERROR=y``. All QEMU
runs used four vCPUs. Userspace runs used 1536 MiB of RAM, and KUnit runs used
1024 MiB except for KASAN, which used 1536 MiB. One extra RT KUnit launch
stopped before KTAP. An immediate retry reached the test and passed.

Build and ownership checks:

* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` passed.
* Strict checkpatch reported no errors or warnings after ignoring the expected
  ``FILE_PATH_CHANGES`` advisory.
* The Rust Sphinx build completed without new warnings.
* The production image contains ``rust_vma_split`` and every split helper.
* The C fallback image omits the Rust split entry and helpers.
* The split-only image contains the Rust split path and omits the other Rust
  VMA modify and unmap entries.
* ``llvm-nm -u vmlinux`` reports no undefined symbols in any configuration.
* Production disassembly shows the public and internal C adapters entering
  ``rust_vma_split`` and the Rust body issuing helpers in the documented order.
* The production image boots to the expected missing-root-filesystem panic.
  The kernel is not tainted.
* The final logs contain no failed test, BUG, kernel warning, KASAN report,
  lockdep complaint, RCU stall, oops, or unexpected panic.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Rust gains sound and stable access to VMA fields and mmap-lock ownership.
* The anonymous-VMA clone or memory-policy order changes.
* The file-reference or ``vm_ops->open`` contract changes.
* Hugetlb split validation or THP adjustment ordering changes.
* VMA preparation or completion gains a failure return.
* The map-count or invalid-boundary contract changes.
* The iterator repositioning rules change.
* A helper starts selecting leading or trailing split behavior.
