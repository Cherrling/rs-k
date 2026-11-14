.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for Expanding VMAs
============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_EXPAND`` option selects native Rust for the production
``vma_expand()`` path. The original C implementation remains available as a
fallback.

The path is called by new-range merges, exec stack relocation, and other VMA
operations that extend a target over adjacent ranges. Rust owns these
decisions:

1. Read the target, next VMA, proposed end, and copied-from VMA.
2. Acquire the target write lock.
3. Combine sticky flags from the request and target.
4. Decide whether the next VMA is fully covered and must be removed.
5. Run the debug write-lock and range invariants.
6. Duplicate anonymous-VMA state from a removed next VMA.
7. Duplicate anonymous-VMA state from a copied-from VMA.
8. Lock the removed next VMA.
9. Record next-VMA removal and combine its sticky flags.
10. Commit the structural expansion.
11. Apply the combined sticky flags to the target.
12. Roll back anonymous-VMA duplication after a commit failure.

Retained C boundary
-------------------

The C helpers expose a single-purpose field access, predicate, callback,
allocation, or existing primitive. Rust selects when each operation runs.

The C boundary contains:

* Merge-state and VMA field access.
* Mmap write-lock and debug range assertions.
* VMA write-lock primitives.
* Sticky-flag extraction and application.
* Anonymous-VMA duplication and unlinking.
* The merge commit adapter.
* Merge-state transition to ``VMA_MERGE_ERROR_NOMEM``.

The merge commit remains a separate unit. When
``CONFIG_RUST_VMA_MERGE_COMMIT`` is enabled, the commit adapter enters Rust.
Otherwise, the original C structural commit is used.

Entry points and ownership
--------------------------

The C ``vma_expand()`` adapter calls ``rust_vma_expand()``. The Rust entry
returns zero after expansion or the original negative error code.

``vma_merge_new_range()`` calls expansion through its own Rust path when that
option is enabled. ``relocate_vma_down()`` calls the same C adapter directly.
The function address is unchanged for existing callers.

The caller must hold the mmap write lock and provide a valid target. The next
VMA may be null. The proposed range must contain the target range.

Ordering and invariants
-----------------------

Rust preserves the original C order:

1. Assert the mmap write lock.
2. Write-lock the target.
3. Extract sticky flags from the request and target.
4. Mark removal when a distinct next VMA ends at the proposed end.
5. Validate the target and range.
6. Duplicate the next VMA anonymous state when removal is required.
7. Duplicate copied-from anonymous state when a copied-from VMA exists.
8. Write-lock the next VMA before removal.
9. Set the merge-state removal marker.
10. Add next-VMA sticky flags.
11. Commit the range and tree update.
12. Apply sticky flags to the expanded target.

An anonymous-VMA duplication error returns immediately. It does not start the
structural commit.

If the commit fails, Rust unlinks the anonymous-VMA duplicate when one exists.
Rust sets ``VMA_MERGE_ERROR_NOMEM`` unless the caller requested best-effort
behavior with ``give_up_on_oom``. It then returns ``-ENOMEM``. The commit
contract guarantees that no structural change requires rollback.

Testing
-------

The VMA merge userspace suite covers:

* New-range right, left, and two-sided expansion.
* Removal of the next VMA during a two-sided merge.
* Anonymous-VMA propagation from a removed VMA.
* Sticky-flag preservation.
* File-backed and anonymous mappings.
* Forked and non-mergeable anonymous-VMA state.
* ``brk`` expansion.
* Exec stack relocation through a second ``execve``.

The exec test packs the same static binary as ``/resize-child``. The parent
runs the merge and split tests before the kernel relocates its exec stack.
Reaching child mode proves that expansion and the paired shrink completed.

The ``tools/testing/vma`` merge tests also include direct expansion,
anonymous-VMA duplication, sticky flags, and iterator preallocation failure
coverage for the shared fallback algorithm. The host suite passes all 26 test
cases.

Verification results
--------------------

.. list-table::
   :header-rows: 1

   * - Configuration
     - Expand path
     - Userspace result
     - Exec relocation
     - KUnit result
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
   * - Rust expand only
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - Rust shrink only
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

All seven configurations use LLVM and ``CONFIG_WERROR=y``. Each userspace run
reports both ``VMA_RESIZE exec: PASS`` and ``vma_merge userspace: PASS``.
The serial console can interleave kernel messages with either marker. The
result check removes timestamped kernel records and normalizes whitespace
before matching both strings.

The KASAN, lockdep, and PREEMPT_RT configurations enable
``CONFIG_VMA_UNMAP_KUNIT_TEST`` and pass the ``vma_unmap`` suite three times.

Build and ownership checks:

* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` pass.
* Strict checkpatch reports no errors or warnings after ignoring the expected
  new-file advisory.
* The Rust Sphinx build has no warning from this document.
* Production contains ``rust_vma_expand`` and all expansion helpers.
* The C fallback omits the Rust expansion entry and helpers.
* Expand-only contains the Rust entry while shrink-only omits it.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Production disassembly shows the C adapter entering Rust and Rust issuing
  the lock, sticky, anonymous-VMA, commit, and final flag helpers in order.
* The production image boots to the expected missing-root-filesystem panic.
  With KUnit disabled at boot, the kernel is not tainted.
* Final logs contain no failed test, BUG, kernel warning, KASAN report,
  lockdep complaint, RCU stall, oops, or unexpected panic.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The target or next-VMA selection rule changes.
* Anonymous-VMA duplication or unlinking changes.
* Sticky-flag composition changes.
* Mmap or VMA write-lock ordering changes.
* The merge commit failure contract changes.
* The copied-from VMA contract changes.
* A helper starts selecting the removal or rollback policy.
