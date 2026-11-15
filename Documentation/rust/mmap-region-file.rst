.. SPDX-License-Identifier: GPL-2.0

Native Rust Mapping File Attachment
===================================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_FILE`` option selects native Rust for the
production ``__mmap_new_file_vma()`` high-level path. It depends on the Rust
new-VMA entry. The original C implementation remains available as a fallback.

Rust owns these production decisions:

1. File attachment and optional reference acquisition.
2. Mapping callback availability and invocation.
3. Partial-mapping failure cleanup ordering.
4. Address and writability warning selection.
5. Mapping-state synchronization after a successful callback.

Retained C boundary
-------------------

The C adapter exposes the existing opaque region state. Each helper performs
one operation or exposes one narrow predicate.

The retained boundary contains:

* The existing VMA file assignment and file reference operation.
* The existing mapping callback availability test and callback invocation.
* The existing file-reference release and VMA file clear operation.
* The existing partial reverse-mapping undo operation.
* The existing address and writability warning operations.
* Writing the callback results back into the mapping state.

These helpers do not select a callback, cleanup, or warning branch. Rust
performs each selection and orders the helper calls.

When ``CONFIG_RUST_MMAP_REGION_FILE`` is disabled, the new-VMA entry uses the
original C implementation described in :doc:`mmap-region-new-vma`.

Ordering and invariants
-----------------------

Rust attaches the mapping file to the VMA. It takes a file reference unless
the preparation callback already supplied a referenced replacement file.

Rust returns without a callback when the file has no mapping callback.
Otherwise it invokes the file mapping callback.

On callback failure, Rust releases the file reference and clears the VMA file
before it undoes the partial reverse mapping. This order preserves the
original file lifetime and cleanup semantics.

After callback success, Rust checks that the callback preserved the requested
address and did not add forbidden writability. It then copies the callback's
file and VMA flags back into the mapping state.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* Successful file-backed mappings through the file mapping callback.
* Secret-memory callback rejection and rollback.
* Shared mappings, sealed memfd rejection, and adjacent VMA merges.
* KASAN, lockdep, and PREEMPT_RT configurations.

Validation record
-----------------

Validation date: 2026-11-15. Baseline: ``9140611d8de8``.

The final source passed these gates:

* Eleven LLVM x86_64 builds. The set contained the native Rust path, the
  file, new-VMA, setup, completion, creation, region, and entry fallbacks,
  KASAN, lockdep, and PREEMPT_RT. Every configuration used
  ``CONFIG_WERROR=y``. KASAN used ``KASAN_GENERIC`` and ``KASAN_INLINE``.
  lockdep used ``PROVE_LOCKING`` and ``LOCKDEP``. PREEMPT_RT used
  ``EXPERT`` and ``PREEMPT_RT``.
* Thirty-three ``mmap_entry`` boots. Each of the eleven configurations passed
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
* The native Rust, KASAN, lockdep, and PREEMPT_RT images each exported one
  ``rust_mmap_region_file`` entry and all 13 file helpers. The file fallback
  exported neither. The new-VMA fallback exported neither the new-VMA nor
  file Rust entry. The creation fallback omitted the creation, new-VMA, and
  file layers. The region, setup, completion, and entry fallbacks each
  omitted their target Rust layer. Every image had no undefined symbols.
* ``llvm-objdump`` showed that production
  ``rust_helper_mmap_new_vma_map_file`` jumps directly to
  ``rust_mmap_region_file`` in all four native and risk images.
* ``checkpatch.pl --strict`` reported no errors, warnings, or checks.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* File reference ownership or preparation replacement semantics change.
* Mapping callback availability or invocation changes.
* Partial reverse-mapping cleanup ownership or ordering changes.
* Address or writability validation changes.
* Mapping-state synchronization changes.
* A helper starts selecting a callback, cleanup, or warning branch.
