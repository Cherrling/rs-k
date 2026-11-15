.. SPDX-License-Identifier: GPL-2.0

Native Rust kfifo migration
===========================

Status and scope
----------------

The ``CONFIG_RUST_KFIFO`` option selects an experimental native Rust
implementation of the generic FIFO algorithms in ``lib/kfifo.c``. The option
currently supports x86_64.

Rust owns the production implementation of these operation groups:

* Ring-buffer insertion, removal, peeking, and linear output.
* Wrap-around calculation and element-size scaling.
* Record sizing, framing, peeking, removal, and skipping.
* User-copy range splitting, partial-copy accounting, and index commitment.
* Normal and record FIFO scatterlist preparation.
* Normal and record DMA range splitting.
* Record DMA finish and index advancement.

The existing C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public inline interface and symbol names do not change. With
``CONFIG_RUST_KFIFO`` enabled, the exported adapters in ``lib/kfifo.c`` forward
directly to Rust and do not execute a FIFO algorithm.

Twenty Rust entry points implement the out-of-line FIFO operations:

* ``rust_kfifo_in()``, ``rust_kfifo_out_peek()``, and
  ``rust_kfifo_out_linear()``.
* ``rust_kfifo_out()``, ``rust_kfifo_from_user()``, and
  ``rust_kfifo_to_user()``.
* ``rust_kfifo_dma_in_prepare()`` and ``rust_kfifo_dma_out_prepare()``.
* ``rust_kfifo_max_r()``, ``rust_kfifo_len_r()``, ``rust_kfifo_in_r()``,
  ``rust_kfifo_out_peek_r()``, and ``rust_kfifo_out_linear_r()``.
* ``rust_kfifo_out_r()``, ``rust_kfifo_skip_r()``,
  ``rust_kfifo_from_user_r()``, and ``rust_kfifo_to_user_r()``.
* ``rust_kfifo_dma_in_prepare_r()``, ``rust_kfifo_dma_in_finish_r()``, and
  ``rust_kfifo_dma_out_prepare_r()``.

The C adapters retain all original ``__kfifo_*`` symbol names and export
classes. The production algorithms are in ``rust/kernel/kfifo.rs``.

Retained C boundaries
---------------------

``__kfifo_alloc_node()``, ``__kfifo_free()``, and ``__kfifo_init()`` remain in
C. These operations own allocator selection, NUMA-node placement, devres
release, and caller-provided storage lifetime.

The architecture-specific ``copy_from_user()`` and ``copy_to_user()``
primitives remain in C. Rust owns range splitting, element accounting,
partial-copy policy, memory-barrier placement, and index commitment.

The ``smp_wmb()`` compiler and architecture boundary remains in C. Rust calls
that primitive at the existing publication points.

The scatterlist storage layout and inline ``sg_set_buf()`` mutation remain in
C. The ``rust_helper_kfifo_set_sg()`` helper performs the inline memory and DMA
address updates. Rust owns segment selection, wrap splitting, length clamping,
and the decision to set a mapped DMA address.

The public ``__kfifo_*`` functions remain C ABI adapters. They forward all
out-of-line operations and retain the existing ordinary ``EXPORT_SYMBOL``
interfaces.

Important invariants
--------------------

The FIFO size must be a power of two. Indices are monotonically increasing
``unsigned int`` values that wrap modulo 2^32. A storage position is the
corresponding index bitwise-ANDed with ``mask``.

Ring ranges are calculated in bytes. Element sizes greater than one scale the
FIFO offset, capacity, and requested length before the wrapped ranges are
copied.

Data publication occurs before Rust commits ``in``. Consumed-data publication
occurs before Rust commits ``out``. Concurrent access retains the original
kfifo locking contract.

A record consists of a one-byte or two-byte little-endian length prefix
followed by that many data bytes. Insertion requires space for the complete
prefix and record. Prefix writes do not publish the record until the data copy
and index update complete.

User copies report only complete elements or record bytes. A partial element
or failed record copy is not committed. DMA preparation returns at most the
provided scatterlist entry count and an empty range returns zero entries.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* All twenty kfifo KUnit cases with repeated SMP4 execution.
* A Rust-disabled C fallback with the same KUnit cases.
* A Rust-enabled x86_64 KASAN build with SMP1 and repeated SMP4 execution.
* Rustfmt, Clippy, Rust documentation, strict checkpatch, and
  ``git diff --check``.
* Export-class checks for every public adapter.
* Disassembly checks showing public adapters enter Rust and contain no FIFO
  algorithm.

Measured validation results
---------------------------

The final Rust-enabled x86_64 LLVM build passed all 20 ``kfifo`` KUnit cases
on one virtual CPU and in three repeated four-CPU runs. The Rust-disabled
fallback passed the same 20 cases in the same four configurations.

The Rust-enabled x86_64 KASAN build passed all 20 cases on one virtual CPU and
in three repeated four-CPU runs. None of the 12 boot logs contained a BUG,
Oops, kernel panic, KASAN report, warning, call trace, or RCU stall.

The Rust image contained all 20 ``rust_kfifo_*`` production entry points and
all 23 original ``__kfifo_*`` exports. The C fallback contained no
``rust_kfifo_*`` or ``rust_helper_kfifo_*`` symbols and retained all 23 C
exports.

Object-level disassembly showed 20 direct branches from the C adapters to Rust.
The adapter object contained no ``memcpy()``, user-copy, memory-barrier, or C
FIFO helper call. Allocation, free, and initialization remained the only
non-adapter functions in that object.

Rustfmt, Clippy, Rustdoc, Sphinx HTML generation, and ``git diff --check``
passed. Strict checkpatch passed with the expected file-addition warning
suppressed because this change also updates ``MAINTAINERS``. The Sphinx build
rendered the new documentation page and linked it from the Rust documentation
index.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The FIFO storage representation or element-index contract changes.
* A supported target adds architecture-specific user-copy or DMA requirements.
* An allocator, user-copy, barrier, or scatterlist helper starts to select FIFO
  policy.
* A C adapter stops forwarding directly to Rust.
* A public interface starts to execute a C FIFO algorithm again.
* The one-byte or two-byte record framing contract changes.
* Performance data shows that a direct Rust inline path requires a separate
  review.
