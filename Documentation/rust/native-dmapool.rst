.. SPDX-License-Identifier: GPL-2.0

Native Rust DMA coherent pools
==============================

Status and scope
----------------

The ``CONFIG_RUST_DMAPOOL`` option selects native Rust implementations of DMA
coherent pool creation, destruction, allocation, release, and managed-device
lifecycle entry points.

The option depends on x86_64 and DMA support. Other architectures use the
complete C implementation. The C implementation also remains available when
the Rust option or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these public interfaces:

* ``dma_pool_create_node()``

* ``dma_pool_destroy()``

* ``dma_pool_alloc()``

* ``dma_pool_free()``

* ``dmam_pool_create()``

* ``dmam_pool_destroy()``

Rust owns:

* Device, size, alignment, and boundary validation.

* Block size rounding and minimum page allocation selection.

* NUMA node placement of the pool and page metadata.

* Pool registration and sysfs attribute publication decisions.

* Page allocation, block initialization, and page growth.

* Free-list ordering and active, block, and page counters.

* Busy-pool detection and diagnostic selection.

* Page teardown and pool teardown.

* Managed resource slot ownership and release policy.

Retained C boundaries
---------------------

The ``dma_block``, ``dma_page``, and ``dma_pool`` layouts remain in C in
``include/linux/dmapool-rust.h``. C and Rust access the same intrusive lists,
spinlock, counters, and embedded names.

C helpers in ``mm/dmapool.c`` retain:

* The global registration and pool-list mutexes.

* Pool spinlock and interrupt-state operations.

* Coherent DMA page allocation and release.

* Sysfs attribute creation and removal.

* Pool, page, and block diagnostic delivery.

* Debug poisoning, allocation checks, and zeroing policy.

* Device-resource allocation, addition, removal, and release primitives.

The six public symbols keep their original ordinary ``EXPORT_SYMBOL`` class.
The native C object retains those export records while its duplicate function
definitions leave the build.

Important invariants
--------------------

An alignment value of zero becomes one. Every other alignment value must be a
power of two.

The requested size must be nonzero and must fit in a signed integer. The block
size includes the intrusive block header and is rounded up to the requested
alignment. A page allocation is at least one system page.

A zero boundary becomes the page allocation size. A nonzero boundary must be a
power of two and must be at least one block. The boundary is capped at the page
allocation size.

Blocks are linked in ascending page offsets. A newly initialized page becomes
the head of the pool page list and its blocks become the head of the free list.
This preserves the C allocation order.

The pool spinlock protects the free-list head and allocation counters. Page
allocation runs without that lock. Pool registration uses the existing mutex
order: registration mutex first, then pool-list mutex.

Destroying a busy pool keeps its coherent pages allocated. The operation emits
the original ``dma_pool_destroy`` diagnostic and still releases page metadata,
the pool object, and its registration.

Managed pools use the existing C device-resource callback. Explicit destruction
and automatic device cleanup therefore run the same pool destruction path.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Creation validation, zero alignment, alignment rounding, and boundary
  rejection.

* Aligned blocks, DMA addresses, and page-boundary restrictions.

* Page growth and zero-initialized allocation.

* Managed pool explicit release.

* Concurrent allocation and release from four workers.

* The same tests with native Rust DMA pools disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Rust-enabled KASAN, lockdep, and PREEMPT_RT runs.

* Adjacent device-resource or DMA regressions.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks for Rust definitions, C fallback definitions, export class,
  automatic GPL export exclusion, and unresolved references.

Validation results
------------------

Validation on 2026-11-25 used parent commit ``629ddb89453f``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_DMAPOOL=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_DMAPOOL=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, LLVM, ``CONFIG_SMP=y``,
``CONFIG_NR_CPUS=4``, ``CONFIG_KUNIT=y``, and
``CONFIG_DMAPOOL_KUNIT_TEST=y``.

All nine QEMU runs passed all six DMA pool cases. Native, fallback, and C-only
runs used both one and four virtual CPUs. KASAN, lockdep, and RT runs used four
virtual CPUs.

The suite covered invalid device, size, alignment, non-power-of-two
boundaries, oversized sizes, zero alignment, aligned and bounded allocations,
page growth, zero allocation, managed release, and concurrent allocation.

The concurrent case used four kthreads. A start barrier held every worker until
all four were running. Each worker then completed 1000 allocation and release
cycles.

The KASAN run initialized generic KASAN. The lockdep run initialized the lock
dependency validator. The RT kernel reported ``SMP PREEMPT_RT``. Full KASAN,
lockdep, and RT boot logs contained no KASAN report, BUG, warning, Oops, lock
inversion, recursive locking report, kernel panic, or RCU stall.

The adjacent ``root-device-devm`` suite passed 2 of 2 cases. The
``platform-device-devm``, ``platform-device-match``, and
``platform-device-swnode`` suites passed all 8 cases. The existing
``mm/dmapool_test.ko`` module completed compilation and module post-processing
in the native configuration.

A separate production configuration disabled KUnit and all DMA pool test
options. It built with ``CLIPPY=1`` and Rust still defined all six entry points.
The kernel booted to the expected ``VFS: Unable to mount root fs`` panic.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported no errors, warnings,
or checks. Focused Sphinx rendered this page. Its output contained one
cross-reference warning outside this page: ``rust/index.rst`` referred to the
unknown document ``../process/kernel-docs``.

The native, fallback, and C-only ``Module.symvers`` files each contained the
six public DMA pool symbols. Every entry used ``EXPORT_SYMBOL``.

The final native ``rust/kernel.o`` defined all six entry points. The native
``mm/dmapool.o`` defined none of them and left their references unresolved for
the final link. The fallback ``rust/kernel.o`` defined none, while the fallback
and C-only ``mm/dmapool.o`` objects defined the complete C implementation.
``vmlinux`` contained no unresolved DMA pool symbol. The automatically
generated Rust GPL export header contained none of the six names.
