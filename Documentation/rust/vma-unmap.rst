.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Range Unmapping
=========================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_UNMAP`` option selects native Rust for the production
``munmap()`` orchestration. The option depends on ``CONFIG_RUST_EXIT_MMAP``.
The original C implementation remains available as a fallback.

This unit covers the public ``munmap()`` entry point and the internal
``do_vmi_munmap()`` entry point. Rust owns range validation, page alignment,
the first-overlap lookup, empty-range lock handling, mmap write locking, and
the userfaultfd completion order.

The aligned unmap entry point, completion sequence, and rollback loops are the
separate Rust unit described in :doc:`vma-munmap-complete`. The gather loop and
partial-split decisions are described in :doc:`vma-munmap-gather`.

Entry points and ownership
--------------------------

The C ``__vm_munmap()`` adapter passes ``current->mm`` to
``rust_vma_unmap()``. Rust creates the private VMA iterator and userfaultfd
list, acquires the write lock, runs the range operation, completes the
required lock-release path, and reports the userfaultfd events.

The C ``do_vmi_munmap()`` adapter passes its existing iterator, mm, and
userfaultfd list to ``rust_do_vmi_munmap()``. Rust then applies the same range
validation and first-overlap decision. This preserves all existing callers
that already hold the mmap write lock.

Rust owns these decisions:

1. Page-aligned start validation.
2. Address-space limit validation.
3. Length overflow and page alignment.
4. Zero-length rejection.
5. First overlapping VMA lookup.
6. Whether an empty range releases the write lock.
7. The killable write-lock acquisition for the syscall path.
8. The conditional write unlock after errors.
9. Userfaultfd completion after the lock contract finishes.

Retained C boundary
-------------------

The C boundary contains:

* Current-address-space selection.
* VMA iterator and userfaultfd list initialization.
* Mmap write-lock acquisition and release.
* ``TASK_SIZE`` evaluation for the active thread.
* First-overlap maple-tree lookup.
* Userfaultfd unmap completion.

``do_vmi_align_munmap()`` now gathers, splits, detaches, completes, and rolls
back through Rust. C retains the individual VMA and maple-tree primitives.

Ordering and invariants
-----------------------

The syscall path initializes an iterator at ``start`` and an empty
userfaultfd list. It takes the mmap write lock with a killable acquisition.

For an invalid range, Rust returns ``-EINVAL`` before touching the iterator.
For a valid range, it rounds the length up to a page boundary and rejects a
zero resulting range.

Rust uses the iterator to find the first VMA that overlaps the end address. If
no VMA exists, the operation succeeds. With the unlock contract enabled, Rust
releases the write lock and still completes userfaultfd processing.

When a VMA overlaps, Rust calls the existing aligned munmap implementation.
After that call, Rust preserves the original lock rules:

* An error leaves the write lock held for Rust to release.
* A non-unlocking caller leaves the write lock held for Rust to release.
* A successful unlocking caller delegates the final release to the aligned
  munmap implementation.

Userfaultfd completion runs after the final lock decision.

Testing
-------

KUnit checks aligned ranges, unaligned length rounding, the maximum valid
range, unaligned starts, starts beyond ``TASK_SIZE``, oversized lengths,
zero-length requests, and arithmetic wraparound.

The userspace test runs 64 rounds. Each round checks:

* Repeated unmapping of an empty range.
* Partial front, middle, and tail unmapping.
* Removal of multiple remaining VMAs with holes in one call.
* Unaligned start rejection.
* Zero-length and overflow-length rejection.
* Page-aligned rounding of an unaligned length.
* Shared anonymous and shared ``memfd`` mappings.
* A gate-synchronized four-thread workload with 16 unmap cycles per worker.

The test reports through ``/dev/kmsg`` and powers off QEMU.

Validation record
-----------------

The KUnit suite contains one Rust range-operation case. It ran three
consecutive times under the native Rust, KASAN, lockdep, and PREEMPT_RT
configurations.

The userspace suite ran three consecutive times under every configuration in
the table. Each run covered the 64 single-threaded rounds and 4096 concurrent
map and unmap cycles.

.. list-table::
   :widths: 30 18 20 20
   :header-rows: 1

   * - Configuration
     - Production path
     - Userspace result
     - KUnit result
   * - Native Rust
     - Rust
     - 3 of 3
     - 3 of 3
   * - C fallback
     - C
     - 3 of 3
     - Not built
   * - KASAN
     - Rust
     - 3 of 3
     - 3 of 3
   * - lockdep
     - Rust
     - 3 of 3
     - 3 of 3
   * - PREEMPT_RT
     - Rust
     - 3 of 3
     - 3 of 3

All QEMU runs used four vCPUs. Userspace runs used 1536 MiB of RAM. KUnit
runs used 1024 MiB of RAM. KVM was the primary accelerator and TCG was the
fallback. One RT KUnit launch stopped before KTAP. A retry started normally.
All three final RT runs passed.

The final logs contain no failed case, BUG, warning, KASAN report, lockdep
complaint, RCU stall, oops, or unexpected panic.

Build and production checks:

* Production Clippy and all five diagnostic builds completed with
  ``CONFIG_WERROR=y``.
* ``rustfmtcheck`` and ``git diff --check`` passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks.
* ``scripts/get_maintainer.pl`` matched every added file.
* The Rust Sphinx build passed with only existing cross-reference and missing
  Graphviz warnings.
* The production image contains ``rust_vma_unmap`` and
  ``rust_do_vmi_munmap``.
* The production image omits ``rust_vma_unmap_test_range`` and the VMA-unmap
  KUnit test symbols.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Disassembly shows ``do_vmi_munmap()`` jumping directly to
  ``rust_do_vmi_munmap()``.
* Disassembly shows ``__vm_munmap()`` jumping directly to
  ``rust_vma_unmap()``.
* Rust disassembly calls the lock, iterator, range-lookup, aligned-munmap,
  unlock, and userfaultfd helpers in the documented order.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, warning, KASAN report,
  RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* ``do_vmi_align_munmap()`` gains a stable Rust-callable split primitive.
* The detached maple-tree representation becomes safe for Rust to own.
* The mmap lock release contract changes.
* The first-overlap lookup changes to another tree or index.
* Userfaultfd unmap completion changes its ordering contract.
* A helper starts selecting munmap policy instead of performing one primitive
  operation.
