.. SPDX-License-Identifier: GPL-2.0

Native Rust Completion of Aligned VMA Unmapping
===============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MUNMAP_COMPLETE`` option selects the direct Rust
definition of ``do_vmi_align_munmap()``. The option depends on
``CONFIG_RUST_VMA_UNMAP``. Rust also owns aligned unmap orchestration,
completion, and rollback. The complete C implementation is selected only when
the option is disabled.

This unit covers the production ``do_vmi_align_munmap()`` entry point. It also
replaces the shared completion and reattachment routines used by overlapping
``mmap()`` and aborted mapping operations.

Rust owns these decisions:

1. The order of detached-tree gather, main-tree clearing, and completion.
2. Error propagation from the gather and main-tree clearing operations.
3. The rollback decision when main-tree clearing fails.
4. Map-count and locked-page accounting before a lock downgrade.
5. The optional mmap write-to-read lock downgrade.
6. The empty-page decision after accounting updates.
7. Page-table cleanup ordering relative to high-water and virtual-memory
   accounting.
8. Detached-VMA removal iteration and per-VMA removal order.
9. Memory-account release, address-space validation, and final read unlock.
10. Detached-tree destruction after all users are gone.
11. VMA reattachment iteration during rollback.

Retained C boundary
-------------------

The C stack-storage helper allocates the opaque aligned-unmap state because it
contains private maple-tree and VMA-operation layouts. The helper initializes
the state and calls one Rust callback. Rust receives no ownership of those
layouts beyond the operation lifetime.

The C boundary contains:

* Opaque state and iterator initialization.
* The sealed-VMA property.
* Detached-tree insertion and VMA isolation.
* Page-table and reverse-map removal.
* Shared region-unmap and VMA release adapters.
* Maple-tree state changes and destruction.
* Mmap lock downgrade and release.
* Virtual-memory charge accounting.
* Address-space validation.

The gather loop, split decisions, and per-VMA accounting are the separate
Rust unit described in :doc:`vma-munmap-gather`. The split operation itself
is described in :doc:`vma-split`. The shared ``unmap_region()`` and
``remove_vma()`` sequences are Rust-owned when
``CONFIG_RUST_VMA_REGION_TEARDOWN`` is enabled, as described in
:doc:`vma-region-teardown`.

Entry points and ownership
--------------------------

``rust/kernel/vma_munmap_complete.rs`` directly defines
``do_vmi_align_munmap()``. The entry sends its public arguments to the C
stack-storage helper. The helper initializes the opaque state and calls the
Rust runner. Rust gathers the VMA set, snapshots the completion values, clears
the main tree, and either completes the operation or reattaches the detached
VMAs. The original C entry in ``mm/vma.c`` compiles only when the option is
disabled.

The C ``vms_complete_munmap_vmas()`` wrapper snapshots a shared VMA-unmap
state and calls ``rust_vma_munmap_complete()``. This routes direct ``munmap()``,
overlapping ``mmap()``, and abort paths through the same Rust completion
implementation.

The C ``reattach_vmas()`` wrapper calls ``rust_vma_munmap_reattach()``. Rust
iterates the detached tree, marks each VMA attached, and destroys the tree.

Ordering and invariants
-----------------------

The gather phase runs before the main tree changes. If it fails, the Rust
gather path performs reattachment and the aligned entry returns the original
error after address-space validation.

After a successful gather, Rust snapshots all accounting fields and detached
tree pointers. If clearing the main tree fails, Rust reattaches the snapshot
and returns ``-ENOMEM``.

After a successful main-tree clear, completion runs in this order:

1. Subtract VMA count and locked-page count.
2. Downgrade the mmap write lock when requested.
3. Return when the range has no pages.
4. Clear page tables when the gather state requires it.
5. Update the high-water mark before reducing ``total_vm``.
6. Subtract page, executable, stack, and data accounting.
7. Remove every VMA from the detached tree.
8. Release memory-account charges.
9. Validate the address space.
10. Release the mmap read lock.
11. Destroy the detached tree.

Rollback uses the same detached-tree cursor. It marks every VMA attached
before destroying the tree. The caller retains the mmap lock and owns the
original error.

Testing
-------

The userspace suite retains all range-unmap coverage and adds three paths that
reach this shared completion code:

* ``MAP_FIXED`` replaces eight pages inside a sixteen-page mapping. The test
  checks that both neighboring fragments remain present and retain their
  values.
* A sixteen-page ``brk`` growth is reduced first to three pages and then to
  the original break. The test checks that both removed ranges disappear.
* A sixteen-page SysV shared-memory attachment is detached. The test checks
  that the complete range disappears and destroys the segment.

The four-thread concurrent workload also runs the completion path under the
KASAN, lockdep, and PREEMPT_RT configurations. The existing VMA-unmap KUnit
case remains enabled to check the outer range operation in every Rust
configuration.

Validation record
-----------------

The native KUnit configuration ran three times at four vCPUs and once at one
vCPU. KASAN, lockdep, and PREEMPT_RT each ran KUnit once at four vCPUs and once
at one vCPU. Every configuration ran the userspace suite once.

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
the fallback. Every final userspace run completed all 64 rounds, including
4096 gate-synchronized map and unmap cycles.

An adjacent native SMP4 boot passed ``copy_mm`` 5 of 5 cases,
``mm_lifetime`` 6 of 6 cases, ``exit_mmap`` 2 of 2 cases, ``dup_mmap``
2 of 2 cases, and ``vma_unmap`` 1 of 1 case.

No completion-specific KUnit selector was added because constructing a real
address space, VMA iterator, and detached tree requires user-task state.
The userspace paths exercise the same production entries and completion
implementation.

The final logs contain no failed case, BUG, warning, KASAN report, lockdep
complaint, RCU stall, oops, or unexpected panic.

Build and production checks:

* Production Clippy and all five diagnostic builds completed with
  ``CONFIG_WERROR=y``.
* ``rustfmtcheck``, shell syntax validation, and ``git diff --check``
  passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks.
* ``scripts/get_maintainer.pl`` matched every added file.
* The Rust Sphinx build passed with only existing cross-reference warnings.
* The native Rust object directly defines ``do_vmi_align_munmap()``. Native C
  caller objects contain only undefined references to the entry.
* Fallback and C-only objects define the entry in ``mm/vma.o`` and none in
  Rust.
* Each linked image has one definition of the entry. The production image
  omits ``rust_do_vmi_align_munmap``, test-only symbols, and the VMA-unmap
  KUnit symbols.
* ``Module.symvers`` and the generated Rust export header omit the production
  entry.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Rust disassembly calls the stack helper once. The Rust body then calls the
  gather, snapshot, main-tree clear, accounting, PTE cleanup, VMA removal,
  validation, unlock, and tree-destroy helpers in the documented order.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, warning, KASAN report,
  RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The detached-tree representation becomes safe for Rust to own directly.
* The completion state grows fields that require a different synchronization
  contract.
* Mmap lock downgrade or release semantics change.
* VMA removal, memory accounting, or address-space validation ordering
  changes.
* The shared completion routine gains a caller that does not operate on the
  current address space.
* A helper starts selecting rollback or completion policy instead of
  performing one primitive operation.
