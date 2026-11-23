.. SPDX-License-Identifier: GPL-2.0

Native Rust kfifo migration
===========================

Status and scope
----------------

The ``CONFIG_RUST_KFIFO`` option selects native Rust implementations of the
public out-of-line generic FIFO algorithms. The option currently supports
x86_64.

Rust owns the production implementation of these operation groups:

* Ring-buffer insertion, removal, peeking, and linear output.
* Wrap-around calculation and element-size scaling.
* Record sizing, framing, peeking, removal, and skipping.
* User-copy range splitting, partial-copy accounting, and index commitment.
* Normal and record FIFO scatterlist preparation.
* Normal and record DMA range splitting.
* Record DMA finish and index advancement.

The existing C algorithms remain available when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines the twenty public out-of-line entry points and retains
their original ordinary non-GPL export class. The public inline interface and
symbol names do not change.

Rust directly defines:

* ``__kfifo_in()``, ``__kfifo_out_peek()``, and
  ``__kfifo_out_linear()``.
* ``__kfifo_out()``, ``__kfifo_from_user()``, and
  ``__kfifo_to_user()``.
* ``__kfifo_dma_in_prepare()`` and ``__kfifo_dma_out_prepare()``.
* ``__kfifo_max_r()``, ``__kfifo_len_r()``, ``__kfifo_in_r()``,
  ``__kfifo_out_peek_r()``, and ``__kfifo_out_linear_r()``.
* ``__kfifo_out_r()``, ``__kfifo_skip_r()``, ``__kfifo_from_user_r()``, and
  ``__kfifo_to_user_r()``.
* ``__kfifo_dma_in_prepare_r()``, ``__kfifo_dma_in_finish_r()``, and
  ``__kfifo_dma_out_prepare_r()``.

The native ``lib/kfifo.o`` defines no out-of-line FIFO algorithm function
bodies. C retains the original ordinary export metadata. The production
algorithms are in ``rust/kernel/kfifo.rs``.

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

The native C object retains ordinary ``EXPORT_SYMBOL`` metadata for the
twenty Rust-defined symbols. ``rust/Makefile`` excludes those names from the
automatic Rust GPL exporter so the export class stays unchanged.

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

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_KFIFO=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep and prove-locking.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Ring, wrap, element-size, record, user-copy, and DMA tests.

* Symbol checks that Rust defines all twenty public entry points, the native C
  object defines none of their bodies, and the fallback and C-only objects
  define the complete implementations.

* Export checks that all twenty-three names remain ordinary non-GPL symbols
  and the generated Rust GPL export header excludes the twenty Rust entries.

* Rustfmt, Clippy, Rustdoc, kernel-doc, focused Sphinx generation, strict
  checkpatch, and ``git diff --check``.

* Adjacent generic radix tree and B+Tree regressions.

Validation results
------------------

Validation runs on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_KFIFO=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_KFIFO_KUNIT_TEST=y``. Rust configurations selected
``CONFIG_RUST=y`` and ``CONFIG_RUST_KFIFO=y`` except for the fallback.

All six kernel configurations compiled successfully. All nine QEMU runs passed
20 of 20 tests.

The KASAN run initialized generic KASAN. The lockdep run enabled lock
dependency tracking and prove-locking. The RT kernel reported an
``SMP PREEMPT_RT`` build. The logs contained no BUG, warning, KASAN report,
lock inversion, Oops, panic, or RCU stall.

``rust/kernel.o`` defined all twenty public entry points. The native
``lib/kfifo.o`` defined only ``__kfifo_alloc_node()``, ``__kfifo_free()``,
and ``__kfifo_init()``. The fallback and C-only objects defined all
twenty-three bodies.

``vmlinux.symvers`` recorded twenty-three ``EXPORT_SYMBOL`` entries. The
generated Rust GPL export header excluded all twenty public names, and no
``rust_kfifo_*`` symbols remained.

Adjacent native SMP4 regressions passed: ten generic radix tree cases, four
B+Tree traversal cases, six B+Tree structural cases, and four B+Tree visitor
cases.

Rustfmt, Clippy, Rustdoc, kernel-doc, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation rendered the updated
page and linked it from the Rust documentation index.

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
