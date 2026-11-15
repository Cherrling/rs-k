.. SPDX-License-Identifier: GPL-2.0

Native Rust Mapping Completion
==============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_COMPLETE`` option selects native Rust for the
production ``__mmap_complete()`` high-level path. It depends on the Rust region
creation entry. The original C implementation remains available as a fallback.

Rust owns these production decisions:

1. Completion operation ordering.
2. Locked-VMA handling selection.
3. Locked-memory accounting selection.
4. File-backed uprobe callback selection.
5. Soft-dirty update selection.
6. Final page-protection selection.

Retained C boundary
-------------------

The C adapter exposes the existing opaque region state. Each helper performs
one operation or exposes one narrow predicate.

The retained boundary contains:

* The existing performance-event completion.
* The existing overlapping-VMA unmap completion.
* The existing VMA statistics accounting.
* Existing locked-VMA predicates, accounting, and flag clearing.
* The existing file predicate and uprobe callback.
* The existing soft-dirty capability test and flag update.
* The existing page-protection operation.

These helpers do not select an operation branch. Rust performs each selection
and orders the helper calls.

When ``CONFIG_RUST_MMAP_REGION_COMPLETE`` is disabled, the region creation
entry uses the original C implementation. When
``CONFIG_RUST_MMAP_REGION_CREATE`` is disabled, completion configuration is
also disabled by the Kconfig dependency.

Ordering and invariants
-----------------------

Rust records the performance event before it completes the overlapping-VMA
unmap. It then accounts the completed VMA statistics.

For a locked VMA, Rust uses locked-memory accounting when the VMA supports
mlock. Otherwise, Rust clears the locked flags.

Rust invokes the uprobe callback only when the completed VMA has a file.
Soft-dirty status is set only when the page table supports it. Rust sets the
final page protection after all earlier completion operations.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* Anonymous, file-backed, shared, private, fixed, and populated mappings.
* Successful locked mappings and locked-memory accounting.
* File mappings that use the mapping preparation callback.
* Adjacent anonymous mapping and merge paths.
* Mapping replacement and completed overlap removal.
* KASAN, lockdep, and PREEMPT_RT configurations.

Validation record
-----------------

Validation date: 2026-11-15. Baseline: ``d5faf24ab368``.

The final source passed these gates:

* Ten LLVM x86_64 builds. The set contained the native Rust path, the
  completion, new-VMA, setup, creation, region, and entry fallbacks, KASAN,
  lockdep, and PREEMPT_RT. Every configuration used ``CONFIG_WERROR=y``.
  KASAN used ``KASAN_GENERIC`` and ``KASAN_INLINE``. lockdep used
  ``PROVE_LOCKING``. PREEMPT_RT used ``PREEMPT_RT``.
* Thirty ``mmap_entry`` boots. Each of the ten configurations passed three
  consecutive runs. The locked mapping test observed the expected ``VmLck``
  increase. The ``VmLck`` value returned to its pre-mapping value after
  ``munmap``.
* Fifteen adjacent-path boots. Each of ``copy_vma``, ``dup_mmap``,
  ``exit_mmap``, ``vma_unmap``, and ``vma_merge`` passed three runs.
* No target or adjacent log contained ``FAIL``, ``BUG:``, ``WARNING:``,
  ``KASAN:``, ``Oops``, ``lockdep``, an RCU stall, a call trace, or a kernel
  panic.
* GCC 16.2.0 and Clang 21.1.8 compiled the userspace test with
  ``-Wall -Wextra -Werror``.
* ``rustfmtcheck``, ``CLIPPY=1``, ``rustdoc``, and the Rust Sphinx build
  passed. The Sphinx build used ``-W --keep-going``.
* The native Rust image exported ``rust_mmap_region_complete`` and all 12
  helpers. The completion fallback exported neither the Rust entry nor a
  helper. Each other fallback image omitted its target Rust layer.
* ``llvm-objdump`` showed that production
  ``rust_helper_mmap_create_complete`` jumps directly to
  ``rust_mmap_region_complete``.
* ``checkpatch.pl --strict`` reported no errors, warnings, or checks.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Performance-event or overlap-unmap completion order changes.
* VMA statistics or locked-memory accounting semantics change.
* Locked-VMA eligibility or flag clearing changes.
* File uprobe callback selection changes.
* Soft-dirty capability or update semantics change.
* Page-protection finalization changes.
* A helper starts selecting an operation branch.
