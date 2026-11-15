.. SPDX-License-Identifier: GPL-2.0

Native Rust Destination Orchestration for mremap
=================================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_TO`` option selects native Rust for the production
``mremap_to()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. Whether a fixed destination must be released.
2. Source invalidation after releasing a fixed destination.
3. Source reload when destination release can change the source VMA.
4. Whether the operation first shrinks the source range.
5. Post-shrink state recalculation.
6. The ``MREMAP_DONTUNMAP`` virtual-memory expansion check.
7. Destination address selection.
8. Final VMA movement and return.

Retained C boundary
-------------------

The C adapter initializes one opaque destination state. The Rust entry
receives the opaque state pointer and does not access the remap layout.

The C helpers expose one field access, one decision input, or one existing
operation. The boundary contains:

* Destination range unmapping.
* Source VMA invalidation and lookup.
* The existing source shrink operation.
* The existing post-shrink calculated-field update.
* The ``may_expand_vm()`` expansion decision input.
* Destination lookup through ``vrm_set_new_addr()``.
* The existing Rust-backed ``move_vma()`` entry.

No helper selects the fixed, shrink, ``MREMAP_DONTUNMAP``, destination
selection, or move path. Rust owns every branch and the operation order.

Ordering and invariants
-----------------------

Rust follows this fixed-destination order:

1. Release the destination range.
2. Invalidate the previous source pointer and mark the iterator invalid.
3. Return the destination-unmap error without reloading the source.
4. Reload the source range from its original address.
5. Return ``-EFAULT`` if the source no longer exists.

Rust follows this remaining order:

1. Shrink the source range when required.
2. Set the old length to the new length and recalculate the remap state.
3. Check the virtual-memory limit when ``MREMAP_DONTUNMAP`` keeps the source.
4. Select the destination address.
5. Move the source VMA to the destination.

The mmap write lock remains held until the move or an error returns.
Destination release can split or replace the source VMA even when the ranges
come from the same original mapping, so the source is always reloaded after a
successful release.

Testing
-------

The ``copy_vma`` userspace suite covers fixed moves over existing
destinations, fixed-address expansion and shrinking, ``MREMAP_DONTUNMAP``,
copied-VMA self-merges, and merge moves. A sealed destination verifies that a
failed fixed-target release leaves the source mapping intact.

Validation record
-----------------

Validation completed on 2026-11-15. Five x86_64 kernel configurations built
with ``CONFIG_RUST_MREMAP_TO`` enabled: production, KASAN, lockdep, and
PREEMPT_RT. A separate C fallback build did not enable the option.

The ``copy_vma`` userspace suite passed three out of three QEMU runs on all
five configurations. The KASAN runs executed the partial-move fault-injection
case and passed it in all three runs. The suite also verified the sealed
destination case.

The production configuration passed three out of three QEMU runs for each
adjacent userspace suite: ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and
``vma_merge``.

The Rust checks passed ``rustfmtcheck``, Clippy, and Rustdoc. The final scan
covered 27 QEMU logs. It found no ``BUG``, ``WARNING``, ``KASAN`` report,
kernel oops, lockdep report, or RCU stall.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Fixed-destination unmap or source reload ordering changes.
* The source shrink contract or post-shrink state changes.
* ``MREMAP_DONTUNMAP`` virtual-memory accounting changes.
* Destination lookup has a new side effect on the VMA iterator.
* The ``move_vma()`` contract changes.
* A helper starts selecting a fixed, shrink, expansion, destination, or move
  branch.
