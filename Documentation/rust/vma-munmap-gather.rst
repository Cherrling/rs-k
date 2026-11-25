.. SPDX-License-Identifier: GPL-2.0

Native Rust Gathering for Aligned VMA Unmapping
===============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MUNMAP_GATHER`` option selects native Rust for the
production VMA gather loop used by aligned unmapping. The option depends on
``CONFIG_RUST_VMA_MUNMAP_COMPLETE``. The original C implementation remains
available as a fallback.

This unit is shared by ``do_vmi_align_munmap()`` and overlapping ``mmap()``.
Rust owns split decisions, VMA iteration, detached-tree insertion order,
accounting accumulation, userfaultfd preparation, iterator rewind, and
rollback.

Rust owns these decisions:

1. Whether the first VMA needs a lower split.
2. The map-count check before a two-sided split.
3. The sealed-VMA rejection before any split.
4. When the upper VMA split is required.
5. The order of per-VMA locking, detached-tree insertion, and isolation.
6. Page, locked-page, memory-account, executable, stack, and data counts.
7. Userfaultfd preparation and its error response.
8. Debug comparison of the main-tree and detached-tree VMA sets.
9. Iterator rewind to the first VMA in the unmap range.
10. Detached-VMA rollback after a loop failure.

Retained C boundary
-------------------

The helpers expose one existing VMA, maple-tree, lock, accounting, or event
operation. Rust selects when each operation runs.

The C boundary contains:

* VMA start and end access.
* Address-space map-count access.
* The sealed-VMA property.
* The ``__split_vma()`` adapter.
* VMA iterator search, previous, next, and rewind primitives.
* Per-VMA write locking.
* Detached maple-tree insertion and VMA isolation.
* VMA page and flag decoding.
* Reverse-map and page-table effects reached through existing VMA primitives.
* Userfaultfd event allocation.
* Debug-only main-tree validation.

The original debug build contains deliberate ``BUG_ON()`` checks for
corrupted VMA ranges and mismatched tree contents. The Rust path calls one
debug-only C helper to retain those checks.

The split operation called by this loop is a separate Rust unit. Its ownership
and rollback contract are described in :doc:`vma-split`.

Entry points and ownership
--------------------------

The C ``vms_gather_munmap_vmas()`` wrapper snapshots the shared VMA-unmap
state and calls ``rust_vma_munmap_gather()``. It copies all accounting and
iterator results back before returning.

The ``do_vmi_align_munmap()`` Rust entry invokes this wrapper before clearing
the main tree. The overlapping ``mmap()`` path invokes the same wrapper while
preparing an in-place replacement.

On success, Rust leaves a populated detached tree and sets ``clear_ptes``.
Completion then consumes the detached tree. On failure after the loop starts,
Rust reattaches every inserted VMA and destroys the detached tree.

Failure before the first split uses no rollback because no VMA was detached.
An initial split failure leaves the partially changed first VMA in the same
state as the C implementation.

Ordering and invariants
-----------------------

The first VMA is split below the unmap start when necessary. A split that
would create a second boundary is rejected first when the address space is at
its map-count limit. Sealed VMAs return ``-EPERM`` before modification.

The gather loop then runs in address order:

1. Find the next VMA overlapping the aligned end.
2. Reject a sealed VMA.
3. Split the VMA at the aligned end when necessary.
4. Write-lock the VMA.
5. Insert it into the detached tree at the next VMA index.
6. Mark it detached.
7. Add its page and accounting counts.
8. Prepare a userfaultfd unmap event when requested.
9. Run the debug range invariant.

After the loop, Rust records the following VMA and its unmap-page boundary.
It performs the debug tree comparison, rewinds the iterator to the unmap
start, and marks page-table cleanup as pending.

Testing
-------

The userspace suite exercises the Rust gather through:

* Middle, front, tail, repeated, and multi-VMA range unmapping.
* ``MAP_FIXED`` replacement with lower and upper VMA splits.
* ``brk`` shrink with both partial and complete removal.
* SysV shared-memory detach.
* Four concurrent workers performing repeated map, split, and unmap cycles.

The KASAN, lockdep, and PREEMPT_RT configurations run the same userspace suite.
The existing VMA-unmap KUnit case remains enabled for the outer range path.

Verification results
--------------------

The final validation matrix passed:

.. list-table::
   :widths: 34 20 20 26
   :header-rows: 1

   * - Configuration
     - KUnit runs
     - KUnit result
     - Userspace
   * - Native Rust
     - SMP4: 3, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - C fallback
     - Not built
     - Not built
     - 1 pass
   * - C-only fallback
     - Not built
     - Not built
     - 1 pass
   * - KASAN
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - lockdep
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - PREEMPT_RT
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass

Userspace runs used 1536 MiB of RAM. KUnit runs used 1024 MiB of RAM, except
for KASAN, which used 1536 MiB. KVM was the primary accelerator and TCG was
the fallback. Every userspace run completed all 64 rounds, including 4096
gate-synchronized map and unmap cycles.

An adjacent native SMP4 boot passed ``copy_mm`` 5 of 5 cases,
``mm_lifetime`` 6 of 6 cases, ``exit_mmap`` 2 of 2 cases, ``dup_mmap``
2 of 2 cases, and ``vma_unmap`` 1 of 1 case.

No gather-specific KUnit selector was added because constructing a real address
space, VMA iterator, and detached tree requires user-task state. The userspace
paths exercise the production entry and gather loop, and the existing KUnit
case validates the outer range path.

Build and production checks:

* Production Clippy and all five diagnostic builds completed with
  ``CONFIG_WERROR=y``.
* ``rustfmtcheck`` and ``git diff --check`` passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks.
* The Rust Sphinx build passed with only existing cross-reference warnings.
* The native Rust object directly defines ``do_vmi_align_munmap()`` and the
  Rust gather entry. Native C caller objects contain only undefined references
  to the aligned entry.
* Fallback and C-only objects define the aligned entry in ``mm/vma.o`` and no
  Rust gather entry.
* Each linked image has one definition of the aligned entry. The production
  image omits the old adapter, test-only symbols, and VMA-unmap KUnit symbols.
* ``Module.symvers`` and the generated Rust export header omit the production
  entry.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Disassembly shows the Rust-defined aligned entry calling the stack helper
  once. The Rust gather body then calls the gather, split, detached-tree,
  userfaultfd, and debug-validation helpers in the documented order.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, kernel warning, KASAN
  report, RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The maple-tree representation becomes safe for Rust to own directly.
* Map-count or sealed-VMA policy moves to another layer.
* Userfaultfd unmap preparation changes its allocation or ordering contract.
* Debug tree validation stops being a deliberate fatal invariant.
* A helper starts selecting gather policy instead of performing one primitive
  operation.
