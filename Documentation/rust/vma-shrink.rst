.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for Shrinking VMAs
============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_SHRINK`` option selects native Rust for the production
``vma_shrink()`` path. The original C implementation remains available as a
fallback.

The main production caller is exec stack relocation. Rust owns the iterator
configuration, preallocation failure decision, VMA write-lock ordering,
preparation, huge-page adjustment, range update, completion, and validation.

Retained C boundary
-------------------

The C helpers expose one field access or one existing primitive. The boundary
contains:

* End-range validation.
* VMA start and end access.
* Iterator configuration, preallocation, and clearing.
* VMA write locking.
* Preparation-state initialization and execution.
* Transparent huge-page adjustment.
* VMA range update and completion.
* Debug address-space validation.

No helper chooses success, failure, or operation order.

Entry points and ownership
--------------------------

The C ``vma_shrink()`` adapter calls ``rust_vma_shrink()``. The Rust entry
requires the mmap write lock, a live iterator, and a live VMA.

The caller may only reduce the VMA end. A proposed end beyond the current end
triggers the original one-time warning. The callers rely on this precondition.

Ordering and invariants
-----------------------

The operation preserves this order:

1. Warn when the requested end extends beyond the current end.
2. Read the current end.
3. Configure the iterator from the requested end to the current end.
4. Preallocate the iterator update.
5. Return ``-ENOMEM`` without changing the VMA when preallocation fails.
6. Write-lock the VMA.
7. Initialize and run VMA preparation.
8. Adjust transparent huge-page state at the new end.
9. Clear the old iterator range.
10. Set the VMA end.
11. Complete the structural update.
12. Validate the address space.

The range update and completion have no failure return. Therefore no rollback
is required after preallocation succeeds.

Testing
-------

The VMA merge userspace suite ends with a second ``execve`` of the same static
binary from ``/resize-child``. Exec stack relocation first expands the
temporary stack VMA and then calls ``vma_shrink()`` after moving its page
tables.

The child reports ``VMA_RESIZE exec: PASS`` only after the relocation has
completed and control reaches user space. A failed exec returns through the
error path and reports failure.

The suite also runs the complete VMA merge and split coverage before the exec.
The VMA KUnit range test and the ``tools/testing/vma`` shrink test provide
additional surrounding and C fallback coverage.
The host VMA suite passes all 26 test cases.

Verification results
--------------------

.. list-table::
   :header-rows: 1

   * - Configuration
     - Shrink path
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
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - Rust shrink only
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
* Production contains ``rust_vma_shrink`` and every shrink helper.
* The C fallback omits the Rust shrink entry and helpers.
* Expand-only omits the Rust shrink entry while shrink-only contains it.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Production disassembly shows the C adapter entering Rust and Rust issuing
  validation, iterator, preallocation, locking, preparation, range,
  completion, and validation helpers in order.
* The production image boots to the expected missing-root-filesystem panic.
  With KUnit disabled at boot, the kernel is not tainted.
* Final logs contain no failed test, BUG, kernel warning, KASAN report,
  lockdep complaint, RCU stall, oops, or unexpected panic.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The shrink-only-end precondition changes.
* Iterator preallocation gains a different failure contract.
* Mmap or VMA write-lock ordering changes.
* Huge-page adjustment moves relative to preparation or completion.
* The range update or completion gains a failure return.
* A helper starts selecting failure or operation order.
