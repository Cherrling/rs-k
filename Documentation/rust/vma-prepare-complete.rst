.. SPDX-License-Identifier: GPL-2.0

Native Rust Preparation and Completion for VMA Updates
======================================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_PREPARE_COMPLETE`` option selects native Rust for VMA
preparation-object initialization, ``vma_prepare()``, and ``vma_complete()``.
The original C implementations remain available as a fallback.

These paired operations are shared by VMA merge, split, resize, and unmap
paths. Rust owns the complete lock-acquisition and lock-release order:

1. Target, adjustment, file, anonymous-VMA, and uprobe field selection.
2. Middle and next removal-slot composition.
3. File-map uprobe removal and interval-tree locking.
4. Insert-VMA file-map linking under the interval-tree lock.
5. Anonymous-VMA interval-tree locking and removal.
6. File-map reverse-map removal.
7. Reverse-map and file-map reinsertion after the range update.
8. Maple-tree insertion and map-count accounting for a split VMA.
9. Anonymous-VMA reverse-map reinsertion and unlock.
10. File-map unlock and adjacent-VMA uprobe registration.
11. Detached-VMA uprobe removal, file release, and anonymous-VMA unlinking.
12. Map-count, memory-policy, and VMA object release.
13. Repeated removal of a second detached VMA when both middle and next VMAs
    are removed.

The completion path reads and advances the two removal slots. Rust decides when
to repeat the removal sequence and when to emit the original final-end warning.

Retained C boundary
-------------------

The C helpers expose one field access, callback, lock operation, map-tree
operation, or lifecycle primitive. Rust reads the preparation object fields
directly and selects all operation ordering.

Most helpers wrap static inline functions, macros, or typed callbacks that
bindgen cannot expose as stable Rust calls. The helpers do not contain VMA
policy or multi-step operation sequences.

The C boundary contains:

* VMA start and end access.
* Uprobe and anonymous-VMA callbacks.
* File-map and anonymous-VMA locking.
* Reverse-map and file-map tree operations.
* Map-count updates.
* Maple-tree iterator storage.
* Memory-policy release and VMA release.
* Debug warning primitives.

No helper selects a file, anonymous-VMA, removal, or uprobe policy.

Entry points and ownership
--------------------------

The C ``init_multi_vma_prep()``, ``vma_prepare()``, and ``vma_complete()``
names remain static adapters. With the option enabled, they enter
``rust_vma_init_prepare()``, ``rust_vma_prepare()``, and
``rust_vma_complete()``. With the option disabled, the original C bodies are
compiled and the Rust entries and helpers are absent.

The caller must hold the mmap write lock. A preparation object must be
initialized before ``vma_prepare()``. The same object, its live VMA pointers,
the iterator, and the address space must remain valid until
``vma_complete()`` returns.

Ordering and invariants
-----------------------

Preparation preserves the original order:

1. Remove uprobes from the target file mapping.
2. Remove uprobes from an adjacent file mapping.
3. Write-lock the file mapping.
4. Link an inserted file-backed VMA before the range update.
5. Write-lock the anonymous VMA and remove target and adjacent reverse maps.
6. Lock the file-map reverse-map tree and remove target and adjacent entries.

Completion preserves the paired release order:

1. Reinsert adjacent-target and target file reverse maps.
2. Unlock the file-map reverse-map tree.
3. Remove file-map state for one or two VMAs, or insert a split VMA.
4. Reinsert anonymous-VMA reverse maps and unlock the anonymous VMA.
5. Unlock the file mapping and register target and adjacent uprobes.
6. Remove all detached VMAs, including uprobes, files, anonymous state,
   accounting, policy, and the VMA object.
7. Register the inserted split VMA's uprobe.

The completion path has no failure return. Its map-count and policy updates
must occur exactly once per affected VMA.

Testing
-------

The VMA merge userspace suite exercises anonymous and file-backed expansion,
merge, split, shrink, detached-VMA removal, and exec stack relocation. The
host VMA suite exercises the shared C fallback algorithm and its failure paths.

KASAN, lockdep, and PREEMPT_RT configurations exercise the Rust path under
memory, lock-order, and real-time instrumentation. The VMA-unmap KUnit case
checks aligned range behavior in those configurations.

Verification results
--------------------

.. list-table::
   :header-rows: 1

   * - Configuration
     - Preparation path
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

All five configurations use LLVM and ``CONFIG_WERROR=y``. The host VMA suite
passes 26 of 26 cases against the shared C fallback algorithm. The serial
console can split a userspace marker when a kernel message arrives between
writes. The result check removes timestamped kernel records and normalizes
whitespace before matching each marker.

All nine final KUnit runs reached ``ok 1 vma_unmap``.

Build and ownership checks:

* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` pass.
* Strict checkpatch reports no errors or warnings after ignoring the expected
  new-file advisory.
* The Rust Sphinx build has no warning from this document.
* The production image contains ``rust_vma_init_prepare``,
  ``rust_vma_prepare``, ``rust_vma_complete``, and all 36 helpers.
* The C fallback omits all three Rust entries and every helper.
* KASAN, lockdep, and PREEMPT_RT images contain all three entries.
* ``llvm-nm -u vmlinux`` reports no undefined symbols in any configuration.
* Production disassembly shows the three C adapters entering Rust. Rust
  controls initialization, lock acquisition, reverse-map updates, iterator
  storage, detached-VMA removal, accounting, policy release, and VMA free
  order.
* The production image boots to the expected missing-root-filesystem panic.
  With KUnit disabled at boot, the kernel is not tainted.
* Final runtime logs contain no failed test, BUG, kernel warning, KASAN
  report, lockdep complaint, RCU stall, oops, or unexpected panic.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* File-map or anonymous-VMA lock ordering changes.
* The two-slot detached-VMA removal contract changes.
* Uprobe ordering changes between preparation and completion.
* Split-VMA insertion or map-count accounting moves.
* A helper starts selecting policy or operation order.
