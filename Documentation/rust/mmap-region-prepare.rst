.. SPDX-License-Identifier: GPL-2.0

Native Rust Mapping Preparation
===============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_PREPARE`` option selects native Rust for the
production ``call_mmap_prepare()`` high-level path. It depends on the Rust
region creation entry. The original C implementation remains available as a
fallback.

Rust owns these production decisions:

1. File preparation callback success selection.
2. Deferred mmap action callback dispatch and success selection.
3. Replacement-file selection.
4. Descriptor result synchronization.

Retained C boundary
-------------------

The C adapter exposes the existing opaque region state. Each helper performs
one operation or exposes one narrow predicate.

The retained boundary contains:

* The existing VFS preparation callback invocation.
* The existing deferred mmap action callback.
* The existing replacement-file predicate.
* The existing replacement-file ownership and mapping-file update.
* Writing the callback results into the mapping state.

These helpers do not select a callback, error, replacement, or synchronization
branch. Rust performs each selection and orders the helper calls.

When ``CONFIG_RUST_MMAP_REGION_PREPARE`` is disabled, the creation entry uses
the original C implementation described in :doc:`mmap-region-create`.

Ordering and invariants
-----------------------

Rust invokes the VFS preparation callback first. It invokes the deferred mmap
action callback only after VFS preparation succeeds.

After both callbacks succeed, Rust adopts a replacement file only when it
differs from the current mapping file. Adoption records that the replacement
file already carries the reference required by the new mapping.

Rust then copies the file offset, VMA flags, page protection, operations, and
private data from the descriptor into the mapping state.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* File mappings with preparation callbacks.
* Secret-memory preparation rejection and accounting rollback.
* Shared mappings, sealed memfd rejection, and adjacent VMA merges.
* KASAN, lockdep, and PREEMPT_RT configurations.

Validation record
-----------------

The migration gates passed on 2026-11-15 against baseline commit
``95f25ea636a5``. All configurations use ``CONFIG_WERROR=y`` and LLVM.

The production Rust image, preparation fallback, file fallback, new-VMA
fallback, setup fallback, completion fallback, creation fallback, region
fallback, mmap-entry fallback, KASAN, lockdep, and PREEMPT_RT images built
successfully. The KASAN image uses ``CONFIG_KASAN_GENERIC=y`` and
``CONFIG_KASAN_INLINE=y``. The lockdep image uses
``CONFIG_PROVE_LOCKING=y`` and ``CONFIG_LOCKDEP=y``. The PREEMPT_RT image
uses ``CONFIG_PREEMPT_RT=y``.

The ``mmap_entry`` suite passed three times in each of the 12
configurations. It maps ``/dev/zero`` with ``MAP_SHARED`` and verifies
shared writes after the replacement shmem file is adopted. It also verifies
that a private ``/dev/zero`` mapping remains writable after descriptor
synchronization. The suite continues to cover file preparation callbacks,
secret-memory rejection and accounting rollback, shared mappings, sealed
memfd rejection, and adjacent VMA merges.

The production image passed the ``copy_vma``, ``dup_mmap``, ``exit_mmap``,
``vma_unmap``, and ``vma_merge`` suites three times each. The 36 target runs
and 15 regression runs had no ``FAIL``, ``BUG``, ``WARNING``, KASAN report,
Oops, lockdep report, RCU stall, call trace, or kernel panic.

The test program compiled with GCC and Clang using
``-Wall -Wextra -Werror``. Git whitespace checks, strict checkpatch, Rust
format, ``rustfmtcheck``, Clippy, rustdoc, and Sphinx with warnings treated
as errors passed.

Each enabled image contains one ``rust_mmap_region_prepare`` entry and the
five preparation helpers. The preparation fallback and creation fallback
images contain neither the Rust entry nor its helpers. Every image has no
undefined symbols. Disassembly shows that ``rust_helper_mmap_create_prepare``
jumps directly to ``rust_mmap_region_prepare`` when the Rust entry is enabled.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* VFS preparation callback arguments or ordering changes.
* Deferred mmap action callback ownership changes.
* Replacement-file reference transfer semantics change.
* Any synchronized descriptor field changes ownership.
* A helper starts selecting an error, replacement, or synchronization branch.
