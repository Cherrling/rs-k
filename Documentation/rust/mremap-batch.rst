.. SPDX-License-Identifier: GPL-2.0

Native Rust Orchestration for Batched mremap Moves
==================================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_BATCH`` option selects native Rust for the production
``remap_move()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. VMA iteration continuation.
2. Rejection of a leading source gap.
3. Per-VMA source and destination range calculation.
4. Source-gap preservation in the destination address.
5. Multi-VMA eligibility enforcement.
6. Per-VMA remap-state configuration.
7. Source preparation and error propagation.
8. Fixed-target move dispatch.
9. First-VMA result selection and invariant checks.
10. Iterator invalidation after a source split.
11. Destination progression after every successful move.

Retained C boundary
-------------------

The C adapter owns one private iterator and remap-state pointer. Rust receives
an opaque state pointer and does not access the VMA iterator or remap layout.

The C helpers expose one iterator operation, field projection, predicate, or
existing operation. The boundary contains:

* VMA iterator initialization and advancement.
* Source range, VMA range, and new-length projection.
* Per-VMA remap-state updates.
* The ``vma_multi_allowed()`` predicate.
* The existing ``check_prep_vma()`` and ``mremap_to()`` entries.
* Kernel error-value inspection.
* Existing invariant diagnostics.
* The VMA iterator invalidation primitive.

No helper selects a successful iteration, gap error, eligibility error,
preparation error, move error, or iterator invalidation path. Rust owns every
branch and the operation order.

Ordering and invariants
-----------------------

The source end remains fixed for the whole operation. Each iterator result is
clipped to the requested source range before Rust configures the remap state.

The first VMA must cover the source start. A gap after the first VMA is valid.
Rust preserves that gap by adding the offset from the previous source VMA end
to the next destination address.

A VMA rejected by ``vma_multi_allowed()`` is valid only when it is the first
VMA and reaches the source end. A later such VMA, or a first VMA that stops
before the source end, returns ``-EFAULT``.

Source preparation failure returns its error before the fixed-target move.
The fixed-target move error returns without invalidating the iterator or
advancing the destination.

After a successful move, Rust checks the mmap-lock and no-population
invariants. Rust invalidates the iterator when source preparation split a VMA.
Rust then advances the destination using the resulting new length.

The first successful destination becomes the return value. Later VMAs only
advance the destination cursor.

Testing
-------

The ``copy_vma`` userspace suite covers a contiguous batch containing an
anonymous VMA and a file-backed VMA. It verifies both destination VMAs and
their data.

The suite also covers a batch with an internal gap. It verifies gap
preservation, the successful destination layout, and rejection of a leading
source gap.

The KASAN configuration injects a page-allocation failure inside
``move_page_tables()``. The test requires ``ENOMEM`` and verifies that the
source VMA, source pages, and page data are restored. It also verifies that
the destination range is unmapped.

Validation record
-----------------

The production, C fallback, KASAN, PREEMPT_RT, and lockdep configurations
built for x86_64. The ``copy_vma`` suite passed three rounds in each
configuration. The KASAN fault-injection case passed all three rounds.

The ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and ``vma_merge`` suites each
passed three rounds in the production configuration. A case-sensitive scan of
the resulting 27 QEMU logs found no ``BUG``, ``WARNING``, ``KASAN``, ``Oops``,
``lockdep``, or ``RCU stall`` message.

The Rust format, Clippy, and documentation checks passed. GCC and Clang built
the userspace test with ``-Wall -Wextra -Werror``. Strict ``checkpatch``
reported no errors, warnings, or checks.

The production image contains one ``rust_mremap_batch`` symbol and 20 batch
helper symbols. Disassembly shows that ``remap_move()`` calls
``rust_mremap_batch()``. The C fallback image contains neither the batch entry
symbol nor any batch helper symbol.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* VMA iterator initialization or advancement semantics change.
* ``vma_multi_allowed()`` eligibility rules change.
* Leading-gap or internal-gap behavior changes.
* Per-VMA remap-state configuration changes.
* ``check_prep_vma()`` or ``mremap_to()`` ordering changes.
* Iterator invalidation ownership changes.
* Destination progression or return-value selection changes.
* A helper starts selecting a success, failure, or invalidation branch.
