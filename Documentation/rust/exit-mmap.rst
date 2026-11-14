.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Mapping Teardown
==========================================

Status and scope
----------------

The ``CONFIG_RUST_EXIT_MMAP`` option selects native Rust for the production
``exit_mmap()`` path. The option depends on ``CONFIG_RUST_MM_TEARDOWN``. The
original C implementation remains available as a fallback.

This unit covers all user mappings in an address space after its final user
reference has gone. It includes VMA removal, page-table removal, reverse-map
removal, TLB flushing, OOM state publication, and accounting release.

The unit excludes device drivers and the final ``__mmput()`` subsystem
sequence. That sequence is the separate Rust unit described in
:doc:`mm-teardown`. It also leaves individual maple-tree, VMA, TLB, notifier,
architecture, and page-table primitives in C.

Runtime range unmapping is a separate unit described in :doc:`vma-unmap`.
``exit_mmap()`` remains responsible for tearing down the complete address
space.

Entry points and ownership
--------------------------

The C ``exit_mmap()`` adapter allocates one private state object. The object
contains the VMA iterator, unmap descriptor, and TLB gather state because those
types use mm-private layout. The adapter then calls ``rust_exit_mmap()`` and
performs no teardown policy itself.

Rust owns these production decisions:

1. Release of mmu-notifier users before address-space extraction.
2. Initialization of the VMA iterator and the source mmap lock sequence.
3. The architecture exit hook and first-VMA decision.
4. The read-to-write lock transition for an empty address space.
5. The read-lock unmap phase for a populated address space.
6. The point at which ``MMF_OOM_SKIP`` becomes visible.
7. The page-table removal phase under the write lock.
8. The transition from page-table removal to VMA teardown.
9. Iteration, accounting accumulation, VMA detachment, and VMA removal.
10. The final map-count invariant.
11. Tree destruction, trace publication, unlock, and accounting release.

Retained C boundary
-------------------

The helpers expose one existing operation or one indivisible ordering
primitive. Rust selects when each operation runs.

The boundary contains:

* mmu-notifier release.
* mmap read and write lock acquisition and release.
* Architecture mapping exit.
* Maple-tree iterator advance, RCU clearing, and tree destruction.
* Full-mm TLB gather and finish.
* Range unmap and page-table free primitives.
* VMA detached marking, removal, and accounting inspection.
* ``cond_resched()`` and the final map-count diagnostic.
* The mmap-exit tracepoint and VM accounting release.

The C adapter owns the private state layout because ``struct unmap_desc`` is an
mm-private type. It does not select locks, ordering, iteration, rollback, or
accounting.

Locking and invariants
----------------------

The final user reference is gone before this function runs. No other thread
can acquire the address space through ``mm_users``.

The populated path follows the original order:

1. Release mmu notifiers.
2. Take the mmap read lock and call the architecture exit hook.
3. Initialize the full-address-space unmap descriptor.
4. Flush caches, gather the full-mm TLB state, and unmap every VMA.
5. Drop the read lock and set ``MMF_OOM_SKIP``.
6. Take the write lock, clear the maple tree from RCU, and free page tables.
7. Finish the TLB gather.
8. Remove each VMA and accumulate its accounted pages.
9. Destroy the tree, trace, unlock, and release accounting.

The empty path exists for an address space left unusable by a failed
duplication. It replaces the read lock with the write lock and destroys the
empty tree without running the unmap or page-table phases.

Rust's VMA teardown loop checks the account bit, marks each VMA detached,
removes it, permits rescheduling, and advances the iterator. After the loop,
the number of removed VMAs must equal ``mm->map_count``.

Testing
-------

The KUnit suite has two cases:

* An empty address space takes the empty-tree path.
* A populated address space removes four VMAs, including two accounted VMAs,
  and checks the removed count and accounted page count.

The userspace selftest performs 64 fork and exit rounds. Each child creates 32
four-page writable regions separated by guard VMAs. It touches all private
pages and adds shared anonymous and shared ``memfd`` mappings before exit.

The test reports through ``/dev/kmsg`` and powers off QEMU after reporting.

Validation record
-----------------

All final runs used four vCPUs. KUnit used 1024 MiB of RAM. Each userspace run
used 1536 MiB of RAM and performed 64 fork and exit rounds.

Each table entry represents three final runs.

.. list-table::
   :widths: 34 12 24 30
   :header-rows: 1

   * - Configuration
     - Runs
     - KUnit result
     - Userspace result
   * - Native Rust
     - 3
     - ``exit_mmap`` 2 of 2 pass
     - 3 of 3 pass
   * - C fallback
     - 3
     - ``exit_mmap`` suite not built
     - 3 of 3 pass
   * - KASAN with native Rust
     - 3
     - ``exit_mmap`` 2 of 2 pass
     - 3 of 3 pass
   * - lockdep with native Rust
     - 3
     - ``exit_mmap`` 2 of 2 pass
     - 3 of 3 pass
   * - PREEMPT_RT with native Rust
     - 3
     - ``exit_mmap`` 2 of 2 pass
     - 3 of 3 pass

The final logs contain no failed case, BUG, warning, KASAN report, lockdep
report, suspicious RCU access, RCU stall, oops, or unexpected panic.

Build and production checks
---------------------------

The production configuration enabled ``RUST_MM_TEARDOWN``,
``RUST_EXIT_MMAP``, ``RUST_COPY_MM``, ``RUST_DUP_MMAP``, and ``WERROR``.
KUnit, KASAN, lockdep, and PREEMPT_RT were disabled.

The production Clippy build completed with C warnings treated as errors. The
resulting ``vmlinux`` contains ``exit_mmap`` and ``rust_exit_mmap`` but no
failure-injection or KUnit symbols. It has no undefined symbols.

Disassembly shows the C ``exit_mmap()`` adapter calling ``rust_exit_mmap()``.
The Rust function then follows the expected notifier, lock, unmap,
page-table, VMA-removal, tree-destroy, trace, unlock, and accounting sequence.

The production image reached the expected missing-root panic without an
earlier warning, BUG, memory-safety report, lockdep report, RCU stall, or
oops.

Final static checks passed:

* Rustfmt and ``git diff --check``.
* The userspace C test with ``-Wall -Wextra -Werror``.
* Strict checkpatch on the complete commit patch.
* Sphinx build for ``Documentation/rust/``.
* ``scripts/get_maintainer.pl -f`` matched every new file.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* ``struct unmap_desc`` becomes a stable public Rust-callable layout.
* Maple-tree or TLB operations gain safe Rust representations with the same
  locking contract.
* Architecture mapping exit gains a suitable Rust interface.
* VMA detached marking or removal gains a safe Rust abstraction.
* ``exit_mmap()`` gains a new lock transition or failure path.
* Reverse-map removal or page-table freeing changes its mmap-lock contract.
* A helper starts selecting teardown policy instead of performing one
  primitive operation.
