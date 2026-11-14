.. SPDX-License-Identifier: GPL-2.0

Native Rust Compatibility Checks for VMA Merging
=================================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MERGE_COMPAT`` option selects native Rust for the
production compatibility policy of VMA merging. The original C predicates
remain available as a fallback.

The policy is shared by new-range and existing-range merges. This includes the
``mmap()``, ``brk``, ``mprotect()``, and ``mremap()`` paths. The native Rust
existing-range orchestration calls this policy directly when both options are
enabled. Rust owns these decisions:

1. Whether a VMA may have inherited an anonymous mapping from a parent.
2. Which anonymous-VMA duplication or sharing case applies.
3. Whether an adjacent VMA has a compatible memory policy and VMA flags.
4. Whether an adjacent VMA has the same file, userfaultfd context, and
   anonymous-VMA name.
5. Whether file and anonymous page offsets are continuous.
6. Whether a proposed range can merge before or after an adjacent VMA.
7. Whether an existing range can merge to its left or right.
8. Whether a two-sided merge has mutually compatible left and right anonymous
   VMAs.

Retained C boundary
-------------------

The C helpers expose one field or one existing side-effect-free predicate.
Rust selects the order of all checks and combines their results.

The boundary contains:

* Merge-state, VMA, file, and anonymous-VMA pointer access.
* VMA start, end, and page-offset access.
* VMA flag bitmap comparison and ignored flag removal.
* ``mpol_equal()`` and memory-policy access.
* Userfaultfd context comparison.
* Anonymous-VMA name comparison.
* The ``anon_vma_chain`` singularity test.
* The debug invariant that the merge-state anonymous VMA matches its middle
  VMA.

The helpers do not decide whether a left, right, or two-sided merge is
possible.

Entry points and ownership
--------------------------

When the option is enabled, the C ``can_vma_merge_left()`` and
``can_vma_merge_right()`` wrappers call the exported Rust policy entries. The
Rust new-range orchestration calls the Rust policy directly and does not route
through a C policy wrapper. The Rust existing-range orchestration follows the
same direct-call rule.

When the option is disabled, the original C compatibility functions remain the
production policy. The Rust new-range orchestration uses its original C
compatibility helpers only in this fallback configuration.

Ordering and invariants
-----------------------

For each adjacent VMA, Rust checks memory policy, VMA flags, file identity,
userfaultfd context, and anonymous-VMA name. It then performs the
anonymous-VMA compatibility case analysis.

The before and after checks add file page-offset continuity. Private
copy-on-write file mappings also require anonymous page-offset continuity.
The left and right checks add virtual-address adjacency. A two-sided merge
requires the left and right anonymous VMAs to be absent or identical.

All checks run under the mmap write lock. The retained debug helper preserves
the original warning for an inconsistent middle VMA and merge-state anonymous
VMA.

Testing
-------

The VMA merge userspace suite exercises:

* New ranges merging to the left, right, and both sides.
* Incompatible VMA flags and a forked anonymous-VMA target.
* Contiguous file page offsets merging.
* Non-contiguous file page offsets remaining separate.
* Different file objects remaining separate.
* Equal anonymous-VMA names merging through ``mprotect()``.
* Different anonymous-VMA names remaining separate through ``mprotect()``.
* ``brk`` expansion through the expand-only path.

The existing VMA unmap suite runs with the same configurations.

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

The KASAN configuration also enabled ``CONFIG_DEBUG_VM``,
``CONFIG_DEBUG_VM_MAPLE_TREE``, and ``CONFIG_DEBUG_MAPLE_TREE``. Every KUnit
round reported ``ok 1 vma_unmap``.

An additional diagnostic configuration enabled Rust new-range orchestration
with the C compatibility fallback. It completed the build and passed both
userspace suites in three rounds. This exercised the disabled-compatibility
Rust branch without treating it as the target production configuration.

The log scan found no BUG, kernel warning, KASAN report, lockdep complaint, RCU
stall, oops, failed test, or unexpected kernel panic. The expected
missing-root-filesystem panic from the separate production boot was the only
kernel panic.

Build and production checks:

* All six configurations used ``CONFIG_WERROR=y`` and completed with LLVM.
  Their logs contain no compiler warnings or undefined references.
* Production Clippy, ``rustfmtcheck``, ``git diff --check``, and shell syntax
  validation passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks after
  ignoring the expected ``FILE_PATH_CHANGES`` advisory.
* The Rust Sphinx build passed with only existing Graphviz and
  cross-reference warnings.
* The production image contains ``vma_merge_new_range``,
  ``rust_vma_merge_compat_can_merge_left``, and
  ``rust_vma_merge_compat_can_merge_right``.
* Disassembly shows the existing-range path calling the Rust compatibility
  entries. The production Rust object has no reference to the old new-range
  compatibility wrappers.
* The C fallback image contains the C ``vma_merge_new_range`` implementation
  and no Rust compatibility or new-range entry symbol.
* Both production and fallback images report no undefined symbols.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, kernel warning, KASAN
  report, RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* VMA flags, files, anonymous VMAs, or page offsets gain stable Rust-safe field
  access.
* Memory-policy equality or userfaultfd context access moves to a Rust-safe
  abstraction.
* Anonymous-VMA names gain an ownership model that Rust can use directly.
* A helper starts selecting merge policy instead of performing one field access
  or side-effect-free predicate.
