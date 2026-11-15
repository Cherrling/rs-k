.. SPDX-License-Identifier: GPL-2.0

Native Rust New VMA Allocation
==============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_NEW_VMA`` option selects native Rust for the
production ``__mmap_new_vma()`` high-level path. It depends on the Rust region
creation entry. The original C implementation remains available as a fallback.

Rust owns these production decisions:

1. VMA allocation success and failure handling.
2. Iterator preallocation failure cleanup.
3. File-backed, shared-anonymous, or anonymous callback selection.
4. Mapping callback error propagation and cleanup selection.
5. Conditional KSM flag update selection.
6. Architecture validation and VMA installation ordering.
7. Conditional khugepaged registration selection.

Retained C boundary
-------------------

The C adapter exposes the existing opaque region state. Each helper performs
one operation or exposes one narrow predicate.

The retained boundary contains:

* Anonymous mapping classification.
* Existing VMA allocation.
* Existing VMA field and iterator initialization.
* Existing iterator preallocation.
* Existing file mapping and shmem setup operations.
* Existing KSM flag update and architecture validation.
* Existing iterator insertion, map count update, and file linking.
* Existing khugepaged registration.
* Existing iterator and VMA release.

These helpers do not select an error, callback, cleanup, or registration
branch. Rust performs each selection and orders the helper calls.

When ``CONFIG_RUST_MMAP_REGION_NEW_VMA`` is disabled, the region creation
entry uses the original C implementation described in
:doc:`mmap-region-create`.

When ``CONFIG_RUST_MMAP_REGION_FILE`` is enabled, file attachment and mapping
callback control flow are provided by the Rust entry described in
:doc:`mmap-region-file`.

Ordering and invariants
-----------------------

Rust classifies the mapping before it allocates the VMA. An allocation failure
returns ``-ENOMEM`` without cleanup.

After allocation, Rust initializes the VMA and preallocates the iterator. A
preallocation failure releases only the VMA. A mapping callback failure
releases the iterator first and then the VMA.

Rust chooses the file callback for file-backed mappings. It chooses shmem setup
for shared anonymous mappings. Other anonymous mappings use no callback.

When the early KSM decision is false, Rust updates the KSM flags before VMA
installation. Installation locks, stores, counts, and links the VMA in the
existing order. Rust requests khugepaged registration only for a non-anonymous
VMA.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* Private anonymous mappings that require a new VMA.
* Shared anonymous mappings that require shmem setup.
* File-backed and memfd mappings that use the file callback.
* Fixed replacements and adjacent mappings where setup selected another path.
* Mapping-count rejection before new-VMA allocation.
* Locked, secret-memory, policy, and populated mappings.

Validation record
-----------------

Validation date: 2026-11-15. Baseline: ``b9be7aa5cea4``.

The final staged source passed these gates:

* Nine LLVM x86_64 builds. The set contained the native Rust path, the C
  new-VMA fallback, the setup, create, region, and entry fallbacks, KASAN,
  lockdep, and PREEMPT_RT. KASAN used ``KASAN_GENERIC`` and ``KASAN_INLINE``.
  lockdep used ``PROVE_LOCKING``. PREEMPT_RT used ``PREEMPT_RT``.
* Twenty-seven ``mmap_entry`` boots. Each of the nine configurations passed
  three consecutive runs.
* Fifteen adjacent-path boots. Each of ``copy_vma``, ``dup_mmap``,
  ``exit_mmap``, ``vma_unmap``, and ``vma_merge`` passed three runs.
* No target or adjacent log contained ``FAIL``, ``BUG:``, ``WARNING:``,
  ``KASAN:``, ``Oops``, ``lockdep``, an RCU stall, a call trace, or a kernel
  panic.
* GCC 16.2.0 and Clang 21.1.8 compiled the userspace test with
  ``-Wall -Wextra -Werror``.
* ``rustfmtcheck``, ``CLIPPY=1``, ``rustdoc``, and the Rust Sphinx build
  passed. The Sphinx build used ``-W --keep-going``.
* The production image exported ``rust_mmap_region_new_vma`` and all 14
  helpers. The new-VMA fallback exported neither the Rust entry nor a helper.
  The create fallback exported neither the create nor new-VMA Rust layer.
* ``llvm-objdump`` showed that production
  ``rust_helper_mmap_create_new_vma`` jumps directly to
  ``rust_mmap_region_new_vma``.
* ``checkpatch.pl --strict`` reported no errors, warnings, or checks.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Anonymous mapping classification or VMA allocation changes.
* VMA initialization order or iterator ownership changes.
* File callback, shmem setup, or callback cleanup semantics change.
* KSM update eligibility or architecture validation changes.
* VMA installation, map count, file linking, or khugepaged ordering changes.
* Iterator or VMA release ownership changes.
* A helper starts selecting an error, callback, cleanup, or registration
  branch.
