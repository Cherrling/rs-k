.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Move Orchestration for mremap
=============================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_MOVE`` option selects native Rust for the production
``move_vma()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. Move preparation and charge success handling.
2. The source VMA write-protection start.
3. The VMA copy and page-table move result.
4. Early return when no destination VMA was created.
5. ``MREMAP_DONTUNMAP`` completion on success.
6. Memory statistics accounting after the copy.
7. Source release on a normal move or after a recoverable copy error.
8. High-watermark restoration after source release.
9. Error or new-address return selection.

Retained C boundary
-------------------

The C adapter initializes one opaque move state. The Rust entry receives the
opaque state pointer and does not access the remap or VMA layouts.

The C helpers expose one field access, one decision input, or one existing
operation. The boundary contains:

* Move splitting, callback, KSM, and pre-split preparation.
* Memory charge calculation.
* The initial source VMA write lock.
* The existing VMA copy and page-table move entry.
* Current mm high-watermark access.
* ``MREMAP_DONTUNMAP`` completion.
* Memory statistics accounting.
* Source unmapping and accounting repair.
* New-address publication from the remap state.

No helper selects the normal move, recoverable failure, or ``DONTUNMAP``
success path. Rust owns every branch and the operation order.

Ordering and invariants
-----------------------

Rust follows this successful order:

1. Prepare the source VMA for splitting and KSM.
2. Charge accountable memory.
3. Start write protection for the source VMA.
4. Copy the VMA and move its page tables.
5. Save the current mm high-watermark.
6. Complete ``MREMAP_DONTUNMAP`` state when requested.
7. Account the newly mapped range.
8. Release the source only for a normal move.
9. Restore the high-watermark and return the destination address.

Rust follows this failure order:

1. Return the preparation error before changing the VMA.
2. Return ``-ENOMEM`` when memory charge fails.
3. Return the copy error when no destination VMA exists.
4. Preserve the source when no destination was created.
5. Account the destination and select it for cleanup when the copy created a
   destination but failed later.
6. Release the destination instead of the source.
7. Restore the high-watermark and return the copy error.

The source VMA write lock and mmap write lock remain held across this path.
The high-watermark is restored after the temporary destination accounting and
source release.

Testing
-------

The ``copy_vma`` userspace suite exercises normal anonymous and file-backed
moves, fixed-address expansion and shrinking, ``MREMAP_DONTUNMAP``, copied-VMA
self-merges, adjacent merges, a failing file callback, and an injected
partial page-table move. The move-specific checks verify that a normal move
removes the source VMA, that ``MREMAP_DONTUNMAP`` preserves the source VMA,
that virtual-size accounting follows the move mode, and that
``MREMAP_DONTUNMAP`` clears the source VMA lock state.

Validation record
-----------------

The implementation was validated on 2026-11-15 with these results:

* The native Rust production, C fallback, KASAN with ``DEBUG_VM``, lockdep,
  and PREEMPT_RT configurations each passed the dedicated suite three times.
* The KASAN configuration passed the injected partial page-table move case
  three times. Each run moved the first page, failed the second PGD
  allocation, restored both source page frames, retained soft-dirty state on
  the first source page, and removed the destination VMA.
* The native Rust production configuration passed the ``dup_mmap``,
  ``exit_mmap``, ``vma_unmap``, and ``vma_merge`` suites three times each.
* The test source passed GCC and Clang builds with ``-Wall -Wextra
  -Werror``.
* Production Clippy, ``rustfmtcheck``, Rustdoc, ``git diff --check``, and
  strict reStructuredText validation passed.
* Strict checkpatch reports no errors, warnings, or checks after ignoring the
  expected new-file and SPDX advisory types.
* The production image has no undefined symbols, contains one
  ``rust_mremap_move`` entry and all 12 C helper functions, and calls
  ``rust_mremap_move`` from the production move sites.
* The C fallback image contains none of these Rust move symbols and retains
  the C ``move_vma`` path.
* QEMU logs contain no ``BUG``, ``WARNING``, KASAN report, ``Oops``, lockdep
  complaint, or RCU stall marker.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Move preparation or KSM unmerge ordering changes.
* Accountable-memory charge or repair rules change.
* ``MREMAP_DONTUNMAP`` source-release or lock-state rules change.
* VMA copy or page-table rollback ownership changes.
* High-watermark restoration moves or gains a new failure point.
* A helper starts selecting success, failure, or recovery behavior.
