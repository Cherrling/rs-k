.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Region Setup
============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_REGION_SETUP`` option selects native Rust for the
production ``__mmap_setup()`` high-level path. It depends on the Rust region
creation entry. The original C implementation remains available as a fallback.

Rust owns these production decisions:

1. Overlapping-VMA lookup result selection.
2. Unmap gathering and gathered-page error handling.
3. Adjacent VMA selection after overlap collection.
4. Address-space limit rejection.
5. Accountable mapping and remaining-charge selection.
6. Memory-availability error propagation.
7. Charge ownership transfer and account-flag selection.
8. Pre-cleanup ordering and descriptor synchronization.

Retained C boundary
-------------------

The C adapter exposes the existing opaque region state. Each helper performs
one operation or exposes one narrow predicate.

The retained boundary contains:

* First overlapping-VMA lookup.
* Unmap-state and detached-maple-tree initialization.
* Existing unmap gathering and gathered-page clearing.
* Existing overlap-neighbor adoption and adjacent-VMA selection.
* Address-space expansion, accounting, and charge predicates.
* Existing memory-availability charging.
* Charge ownership and account-flag transitions.
* Existing VMA cleanup and descriptor synchronization.

These helpers do not select an overlap, error, accounting, cleanup, or
success branch. Rust performs each selection and orders the helper calls.

Ordering and invariants
-----------------------

Rust looks up the first overlap and initializes unmap state before it selects
the overlap or no-overlap path.

On the overlap path, Rust initializes the detached tree and gathers the
overlapping VMAs. A gather error clears the gathered page count before it
returns the error. A successful gather exposes its previous and next VMAs.
The no-overlap path selects the next VMA and rewinds the previous iterator
position.

Rust rejects an address-space limit failure before memory accounting.
Accountable mappings charge only the pages not already charged by replaced
mappings. A charge failure returns its existing error.

After a successful charge, Rust transfers ownership of the entire mapping
charge and sets the account flag. Rust then cleans the gathered area and
synchronizes the VMA descriptor.

Testing
-------

The ``mmap_entry`` userspace suite exercises this path through normal mmap
system calls. It covers:

* Anonymous private mapping charge and unmap release.
* Fixed replacement without duplicate committed-memory charge.
* Address-space limit failure after overlap collection.
* VMA restoration after that failure.
* Existing replacement, adjacent merge, file, secret-memory, policy, locking,
  and populated-mapping cases.

Validation record
-----------------

The migration gates passed on 2026-11-15 against baseline commit
``af9f6d36ec65``. All configurations used ``CONFIG_WERROR=y`` and LLVM.

The production Rust image, the setup C fallback image, the creation C fallback
image, the region C fallback image, the mmap entry C fallback image, a KASAN
image, a lockdep image, and a PREEMPT_RT image built successfully. The KASAN
image uses ``CONFIG_KASAN_GENERIC=y`` and ``CONFIG_KASAN_INLINE=y``. The
lockdep image uses ``CONFIG_PROVE_LOCKING=y`` and ``CONFIG_LOCKDEP=y``. The
PREEMPT_RT image uses ``CONFIG_PREEMPT_RT=y``.

The ``mmap_entry`` userspace suite passed three times in each of the eight
configurations. The suite verified committed-memory charge, fixed replacement
without duplicate charge, and unmap release. It also exercised an address-space
limit failure after overlap collection and verified that the original mapping
and contents remained.

The production image passed the ``copy_vma``, ``dup_mmap``, ``exit_mmap``,
``vma_unmap``, and ``vma_merge`` suites three times each. The 24 mapping-entry
runs and 15 regression runs had no ``BUG``, ``WARNING``, KASAN report, Oops,
lockdep report, RCU stall, call trace, or kernel panic.

The test program compiled with GCC and Clang using
``-Wall -Wextra -Werror``. Rust format, Clippy, rustdoc, and Sphinx checks
passed with ``rustfmtcheck``, ``CLIPPY=1``, ``rustdoc``, and ``htmldocs``.

The production ``System.map`` contains one ``rust_mmap_region_setup`` entry
and 14 setup helpers. Disassembly shows that the C adapter directly calls
``rust_mmap_region_setup``.

The setup fallback image contains neither the Rust setup entry nor its helpers.
The creation fallback image contains neither the setup nor creation entries.
The region fallback image contains the setup and creation entries but no
``rust_mmap_region`` entry. The mmap entry fallback image contains the setup,
creation, and region entries.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Overlapping-VMA lookup or unmap-state initialization changes.
* Gather failure or gathered-page ownership changes.
* Neighbor selection or iterator position changes.
* Address-space limit semantics change.
* Accountable mapping or remaining-charge calculation changes.
* Charge failure, ownership transfer, or account-flag semantics change.
* Cleanup or descriptor synchronization ordering changes.
* A helper starts selecting an error or control-flow branch.
