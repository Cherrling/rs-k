.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for Existing-Range VMA Merging
==========================================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MERGE_EXISTING`` option selects native Rust for the
production orchestration of VMA merging after an existing range changes. The
original C implementation remains available as a fallback.

This path is used by operations that modify VMA attributes. Examples include
``mprotect()``, NUMA policy changes, anonymous-VMA name changes, and
userfaultfd attribute changes.

Rust owns these decisions:

1. When an existing-range merge can occur.
2. Which left, right, or two-sided scenario applies.
3. Whether the middle or next VMA can be removed.
4. When a two-sided merge must fall back to a left merge.
5. How the target range and page offsets change.
6. When the iterator moves to a previous or next range.
7. The order of anonymous-VMA duplication and rollback.
8. The ``NOMERGE``, ``NOMEM``, and ``SUCCESS`` state transitions.

The option does not depend on ``CONFIG_RUST_VMA_MERGE_COMPAT``. When
compatibility is disabled, Rust calls the original C compatibility predicates
through narrow helpers.

Retained C boundary
-------------------

The C helpers expose state access, one operation, or one debug invariant.
Rust selects the order of all merge operations.

The C boundary contains:

* VMA iterator positioning and range queries.
* Merge-state and VMA field access.
* VMA range and page-offset writes.
* Sticky-flag extraction and application.
* Per-VMA write locks.
* ``can_merge_remove_vma()``.
* ``dup_anon_vma()`` and ``unlink_anon_vmas()``.
* The individual operations used by the structural commit.
* Maple-tree storage, reverse-map updates, file-map updates, uprobe handling,
  and huge-page adjustment reached by the structural commit.
* khugepaged registration.
* ``VM_WARN_ON_VMG`` and merge-state debug invariants.

The helpers do not select a merge scenario. They do not decide merge
eligibility. They do not decide when rollback occurs.

Entry points and ownership
--------------------------

The C ``vma_merge_existing_range()`` wrapper creates a Rust merge state. The
state contains the merge parameters, initial sticky flags, and an
anonymous-VMA duplication result.

The wrapper calls ``rust_vma_merge_existing()``. The Rust entry updates the
shared merge state and returns the merged VMA or a null pointer.

When compatibility is enabled, Rust calls the compatibility module directly.
The production Rust object does not enter a C compatibility wrapper. When
compatibility is disabled, Rust calls the original C predicates through the
narrow existing-range helpers.

Ordering and invariants
-----------------------

The operation runs with the mmap write lock held. Rust first checks the middle
VMA, the proposed sides, and special flags. A range that does not touch a side
cannot merge.

Rust then follows this order:

1. Test left compatibility.
2. Find the next VMA and restore the iterator when the range touches its right
   side.
3. Test right compatibility.
4. Reposition the iterator at the previous VMA when a left merge applies.
5. Decide whether the middle and next VMAs can be removed.
6. Lock and select the target VMA.
7. Combine sticky flags from all target VMAs.
8. Apply the selected scenario range and page-offset policy.
9. Duplicate anonymous-VMA state when required.
10. Commit the structural merge.
11. Restore the iterator and unlink the duplicate on failure.
12. Apply sticky flags, register the target with khugepaged, and record success.

The two-sided case selects the next VMA for anonymous-VMA duplication when the
next VMA has an ``anon_vma``. Otherwise, it selects the middle VMA. The left
case duplicates from the middle VMA. The right case duplicates from the middle
VMA into the next VMA.

When ``CONFIG_RUST_VMA_MERGE_COMMIT`` is enabled, step 10 calls the native
Rust structural commit described in :doc:`vma-merge-commit`. Otherwise, the
original C commit remains the fallback.

Testing
-------

The VMA merge userspace suite adds existing-range cases for:

* A modified VMA merging into its right neighbor.
* A modified VMA merging into its left neighbor.
* A modified VMA merging into both neighbors and removing both.
* A left-side modification that shrinks the middle VMA start.
* A right-side modification that shrinks the middle VMA end.
* Anonymous-VMA duplication into an unfaulted left target.
* Anonymous-VMA duplication into an unfaulted right target.
* Shared file mappings merging with continuous page offsets.
* Shared file mappings with discontinuous page offsets remaining separate.
* A private file mapping copying anonymous-VMA state into an unfaulted target.
* Existing incompatible flags and anonymous names.

The suite uses ``/proc/self/maps`` to verify VMA boundaries. The VMA-unmap
suite also runs because it exercises mapping, splitting, replacement, heap
changes, and concurrent map and unmap cycles.

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
   * - C fallback
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - Rust orchestration and C compatibility
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3

All QEMU runs used four vCPUs. Userspace runs used 1536 MiB of RAM. KUnit runs
used 1024 MiB of RAM. KVM was the accelerator.

The KASAN configuration also enabled ``CONFIG_DEBUG_VM``,
``CONFIG_DEBUG_VM_MAPLE_TREE``, and ``CONFIG_DEBUG_MAPLE_TREE``. Every KUnit
round reported ``ok 1 vma_unmap``.

The C fallback configuration disables the Rust VMA-unmap implementation. Its
Rust VMA-unmap KUnit suite is therefore not built. Its merge and unmap
userspace suites passed in all three rounds.

The mixed configuration enabled Rust existing-range orchestration and disabled
Rust compatibility. The Rust object calls the C compatibility helpers in this
mode. Both userspace suites passed in all three rounds, and every KUnit round
reported ``ok 1 vma_unmap``.

The log scan found no BUG, kernel warning, KASAN report, lockdep complaint, RCU
stall, oops, failed test, or unexpected kernel panic. The expected
missing-root-filesystem panic from the separate production boot was the only
kernel panic.

Build and production checks:

* All seven configurations used ``CONFIG_WERROR=y`` and completed with LLVM.
  Their logs contain no compiler warnings or undefined references.
* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` passed.
* The VMA merge test program compiled with GCC and Clang using ``-Werror``.
* Strict checkpatch reported zero errors, zero warnings, and zero checks after
  ignoring the expected ``FILE_PATH_CHANGES`` advisory.
* The Sphinx HTML build completed and generated
  ``Documentation/output/rust/vma-merge-existing.html``. The build reported
  only pre-existing documentation warnings and errors outside this unit.
* The production image contains ``rust_vma_merge_existing`` and the
  existing-range helpers.
* The production image omits the old C existing-range orchestration symbols.
  The C fallback image contains the C orchestration and no Rust existing-range
  entry or helpers.
* Both production and fallback images report no undefined symbols.
* Production disassembly shows ``vma_modify()`` calling
  ``rust_vma_modify()``. The Rust modification path enters
  ``rust_vma_merge_existing()`` through the merge helper.
* Production Rust disassembly calls the native Rust compatibility entries
  directly. It does not call a C compatibility policy wrapper.
* Mixed-configuration disassembly calls
  ``rust_helper_vma_merge_existing_can_merge_left()`` and
  ``rust_helper_vma_merge_existing_can_merge_right()``.
* The production image boots to the expected missing-root-filesystem panic.
  The kernel is not tainted. No earlier BUG, kernel warning, KASAN report, RCU
  stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Rust gains stable access to VMA fields and merge state.
* The VMA iterator gains a Rust-native interface.
* The maple tree or reverse maps gain a Rust-native structural interface.
* Per-VMA locking moves to Rust.
* Anonymous-VMA duplication or rollback changes.
* khugepaged registration moves to another layer.
* A helper starts selecting merge policy instead of performing one access or
  one operation.
