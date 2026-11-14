.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA-Tree Duplication
================================

Status and scope
----------------

The ``CONFIG_RUST_DUP_MMAP`` option selects native Rust for the production
``dup_mmap()`` path. The option depends on ``CONFIG_RUST_COPY_MM``. The
original C implementation remains available as a fallback.

This unit covers the production duplication of an address space's VMA tree
and the per-VMA steps needed to make a private child address space. Rust owns
the complete sequence from lock acquisition through final unlock and
userfaultfd completion.

This unit excludes device drivers and peripheral subsystems. It also leaves
the maple-tree implementation, VMA allocator, architecture hooks, reverse
mapping structures, userfaultfd records, and page-table copying in their
existing subsystems.

Entry points and ownership
--------------------------

The Rust ``copy_mm`` implementation calls ``dup_mmap::duplicate()`` directly
when ``CONFIG_RUST_DUP_MMAP`` is enabled. The static C ``dup_mmap()`` entry
also calls ``rust_dup_mmap()``, so other callers enter the same Rust
implementation.

Rust owns these production decisions:

1. The source-then-destination mmap write-lock order.
2. Source cache and uprobe setup and destination executable-file and
   accounting inheritance.
3. The shallow VMA-tree copy and the transition to private replacement
   entries.
4. VMA iteration and killable source write locking.
5. The ``MADV_DONTFORK`` decision and destination accounting adjustment.
6. VM accounting charges and the lifetime of one per-VMA charge.
7. VMA clone, memory-policy duplication, destination attachment, and
   userfaultfd setup.
8. ``MADV_WIPEONFORK`` handling and anonymous-VMA fork setup.
9. Hugetlb-private VMA setup and insertion into the destination tree.
10. VMA open callbacks and file-reference and reverse-mapping setup.
11. Page-range duplication and the choice to skip it for wipe-on-fork VMAs.
12. Architecture duplication and KSM and khugepaged success setup.
13. Failure depth, partial rollback, tree destruction, unstable-state flags,
    final unlock, TLB flush, and userfaultfd completion or failure.

Retained C boundary
-------------------

C helpers expose one established locking, maple-tree, VMA, accounting,
reverse-mapping, userfaultfd, architecture, or page-table operation. Rust
selects when and in what order each operation runs.

The C boundary contains:

* mmap-lock acquisition and release.
* Maple-tree copy, iterator, store, clear, RCU, and destroy operations.
* VMA lock markers, allocation, clone, field access, and free operations.
* VM policy, userfaultfd, anonymous-VMA, hugetlb, and VMA open operations.
* File references and address-space reverse-mapping locks and insertion.
* Page-table range copying.
* Architecture address-space duplication and TLB flushing.
* KSM and khugepaged subsystem entry points.
* Partial unmap and VMA teardown through the existing complex
  ``UNMAP_STATE`` and ``tear_down_vmas()`` interfaces.

These helpers use kernel-private inline functions, C layout rules, subsystem
interfaces, or a lock contract that Rust cannot currently express without
duplicating the implementation. The retained cleanup helper performs one
partial-tree teardown. Rust still decides whether rollback is needed and
computes the exclusive end address.

Locking and invariants
----------------------

The source mmap lock is acquired first and the destination mmap lock is
acquired second with ``SINGLE_DEPTH_NESTING``. The destination is not
published when duplication starts.

The initial tree copy contains source VMA pointers. Rust clears the copied
tree's RCU state and replaces each copyable entry with a private duplicate.
A ``MADV_DONTFORK`` entry is removed from the destination instead of being
cloned. Its copied statistics are subtracted exactly once.

The destination ``map_count`` increments only after a duplicate enters the
tree. A page-range failure advances the iterator once more, so rollback
includes the duplicate that failed page copying.

Rollback keeps the upstream C failure-state behavior:

* A shallow-tree copy failure does not set the unstable flags.
* A failure after the tree copy sets ``MMF_OOM_SKIP`` and
  ``MMF_UNSTABLE``.
* Partial rollback tears down only entries below the failure barrier and then
  destroys the remaining destination tree.
* ``map_count`` remains at the number of inserted duplicates, as in the
  original C path. The destroyed tree and unstable flags prevent further
  normal use.

The final destination unlock occurs before the source TLB flush and source
unlock. Userfaultfd events complete on success and fail on every error path
after event-list initialization.

Testing
-------

The KUnit suite has two cases. The success case builds four VMAs and checks
tree order, ``DONTFORK`` removal, ``WIPEONFORK`` retention, ``map_count``,
``total_vm``, and ``data_vm``. The failure case injects each production
failure stage:

* Shallow-tree copy.
* Killable source VMA write lock.
* ``DONTFORK`` tree removal.
* VM memory accounting.
* VMA clone.
* Memory policy.
* Userfaultfd setup.
* Anonymous-VMA fork setup.
* Page-range duplication.
* Architecture duplication.

Each failure case checks the return code, failure counter, retained
``map_count``, unstable flags, and absence of an iterable destination tree.

The userspace selftest runs 32 fork rounds. Each round contains a private
anonymous VMA, a ``DONTFORK`` VMA, a ``WIPEONFORK`` VMA, a shared anonymous
VMA, and a shared ``memfd`` VMA. It verifies:

* Private writes do not cross the fork boundary.
* ``DONTFORK`` mappings are absent from the child.
* ``WIPEONFORK`` mappings exist in the child and start zero-filled.
* Shared anonymous writes are visible in the parent.
* Shared file writes exercise file-reference and reverse-mapping setup.
* Parent mappings remain unchanged.

The test reports through ``/dev/kmsg`` and powers off QEMU after reporting.

Validation record
-----------------

All final runs used four vCPUs. KUnit used 1024 MiB of RAM. The userspace
test used 1536 MiB of RAM and ran 32 fork rounds per boot.

Each table entry represents three final runs.

.. list-table::
   :widths: 36 12 22 30
   :header-rows: 1

   * - Configuration
     - Runs
     - KUnit result
     - Userspace result
   * - Native Rust
     - 3
     - ``dup_mmap`` 2 of 2 pass and ``copy_mm`` 5 of 5 pass
     - 3 of 3 pass
   * - C fallback
     - 3
     - ``copy_mm`` 5 of 5 pass and ``dup_mmap`` suite not built
     - 3 of 3 pass
   * - KASAN with native Rust
     - 3
     - ``dup_mmap`` 2 of 2 pass
     - 3 of 3 pass
   * - PREEMPT_RT with native Rust
     - 3
     - ``dup_mmap`` 2 of 2 pass
     - 3 of 3 pass
   * - lockdep with native Rust
     - 3
     - ``dup_mmap`` 2 of 2 pass
     - 3 of 3 pass

The final KUnit and userspace logs contain no failed case, BUG, warning,
KASAN report, lockdep report, RCU stall, oops, or unexpected panic.
These runs were repeated after the final checkpatch cleanup. The userspace
cleanup replaced unnecessary ``volatile`` qualifiers with calls that already
provide the required compiler memory barriers.

Build and production checks
---------------------------

The final production configuration enabled ``RUST_COPY_MM``,
``RUST_DUP_MMAP``, and ``WERROR``. KUnit, KASAN, lockdep, and PREEMPT_RT
were disabled.

The production Clippy build completed with C warnings treated as errors.
The resulting ``vmlinux`` contains ``rust_dup_mmap`` and the Rust
``duplicate()`` implementation. It contains no failure-injection or KUnit
symbols and no undefined symbols. Disassembly shows ``rust_dup_mmap``
jumping directly to the Rust implementation.

The production image booted with four vCPUs and no root filesystem. It
reached the expected missing-root panic without an earlier warning, BUG,
memory-safety report, lockdep report, RCU stall, or oops.

Final static checks passed:

* Rustfmt and ``git diff --check``.
* Strict checkpatch on the complete commit patch.
* Sphinx build for ``Documentation/rust/``.
* A full Sphinx build that generated the new page without a diagnostic
  for this unit.
* ``scripts/get_maintainer.pl -f`` matched every new file.

The full documentation build reports pre-existing diagnostics in unrelated
driver and networking documentation. None reference this unit.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* A helper starts selecting duplication, failure-depth, or rollback policy.
* Maple-tree, VMA-lock, or complex VMA teardown operations gain a safe Rust
  representation with the same locking contract.
* Architecture or page-table duplication gains a stable Rust interface.
* Reverse-mapping, userfaultfd, or hugetlb interfaces gain suitable Rust
  wrappers.
* The partial cleanup helper is split into narrower Rust-callable
  operations without increasing error-prone state duplication.
* Measurements show that the helper boundary causes an unacceptable
  task-creation regression.
