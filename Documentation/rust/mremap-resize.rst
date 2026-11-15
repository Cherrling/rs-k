.. SPDX-License-Identifier: GPL-2.0

Native Rust Resize Orchestration for mremap
===========================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_RESIZE`` option selects native Rust for the production
``mremap_at()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. No-resize success without changing the mapping.
2. In-place shrink dispatch and error propagation.
3. In-place expansion eligibility.
4. Expansion charge success and rollback.
5. VMA extension failure and cleanup.
6. Expansion accounting after a successful merge.
7. Rejection when the mapping cannot expand in place and cannot move.
8. Destination selection for an expansion move.
9. Final VMA movement and return.

Retained C boundary
-------------------

The C adapter initializes one opaque resize state. The Rust entry receives the
opaque state pointer and does not access the remap layout.

The C helpers expose one field projection, one predicate, or one existing
operation. The boundary contains:

* Remap-type projection into Rust-visible constants.
* Source address access.
* The existing source shrink operation.
* End-of-VMA range alignment.
* The existing expansion-space predicate.
* Memory charge, uncharge, and statistics accounting.
* The existing VMA extension and merge operation.
* ``MREMAP_MAYMOVE`` flag inspection.
* Destination lookup through ``vrm_set_new_addr()``.
* The existing Rust-backed ``move_vma()`` entry.
* The impossible-type warning.

No helper selects the no-resize, shrink, in-place expansion, rejection, or
move path. Rust owns every branch and failure order.

Ordering and invariants
-----------------------

A no-resize request returns the source address without changing the VMA.

A shrink request calls the existing in-place shrink operation. Rust returns the
source address only after that operation succeeds.

An expansion first tests whether the source range reaches the end of its VMA
and whether enough contiguous space is available. If both checks pass, Rust
charges the expansion, extends the VMA, restores the charge on failure, and
accounts for the new size on success.

If an in-place expansion is not possible, Rust returns ``-ENOMEM`` unless
``MREMAP_MAYMOVE`` is set. A movable expansion selects a destination and calls
the existing move path.

The mmap write lock remains held until the operation returns or the shrink path
completes.

Testing
-------

The ``copy_vma`` userspace suite covers no-resize success, in-place shrinking,
in-place expansion, extension failure, rejection without ``MREMAP_MAYMOVE``,
and a non-fixed expansion that must move to a new address. The suite also
checks that an in-place extension stops at an adjacent VMA and preserves it.

Validation record
-----------------

Validation completed on 2026-11-15. Five x86_64 kernel configurations built
with these Rust settings: production, C fallback, KASAN, lockdep, and
PREEMPT_RT. All configurations except the C fallback enabled
``CONFIG_RUST_MREMAP_RESIZE``.

The ``copy_vma`` userspace suite passed three out of three QEMU runs on all
five configurations. The KASAN runs executed the existing partial-move
fault-injection case and passed it in all three runs. KASAN used generic
instrumentation with stack instrumentation disabled because Clang stack
instrumentation exceeds the kernel frame limit in ``mmap_region()``.

The production configuration passed three out of three QEMU runs for each
adjacent userspace suite: ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and
``vma_merge``.

The test source passed GCC and Clang builds with ``-Wall -Wextra -Werror``.
The Rust checks passed ``rustfmtcheck``, Clippy, and Rustdoc. Strict
reStructuredText validation and ``git diff --check`` passed.

The production image contains one ``rust_mremap_resize`` entry and all 13 C
helper functions. The C fallback image contains no resize Rust or helper
symbols. Disassembly shows a direct ``rust_mremap_resize`` call from
``__do_sys_mremap()``. The Rust function calls the helpers in the documented
order.

The final scan covered 27 QEMU logs. It found no ``BUG``, ``WARNING``, KASAN
report, kernel oops, lockdep report, or RCU stall.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Remap-type classification gains a new production state.
* The source shrink contract changes.
* Expansion-space validation changes.
* Charge or accounting failure semantics change.
* ``vma_merge_extend()`` starts selecting the success or failure path.
* ``MREMAP_MAYMOVE`` or destination-selection semantics change.
* The Rust-backed ``move_vma()`` contract changes.
* A helper starts selecting a resize or move branch.
