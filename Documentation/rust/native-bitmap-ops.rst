.. SPDX-License-Identifier: GPL-2.0

Native Rust bitmap operation migration
======================================

Status and scope
----------------

The ``CONFIG_RUST_BITMAP_OPS`` option selects an experimental native Rust
implementation of the public out-of-line bitmap operations.
The option depends on the native Rust bit-search implementation and currently
supports x86_64.

Rust owns the production implementation of these operation groups:

* Equality, OR-equality, intersection, subset, and complement.
* AND, OR, XOR, AND-NOT, replacement, and weighted logical operations.
* Population counts for one or two inputs.
* Left and right shifts and removal of an interior bit range.
* Contiguous range set and clear.
* Aligned clear-area search.
* Positional remapping, ordinal mapping, translation, and folding.
* Scatter and gather through a selection mask.
* Conversion between bitmap storage and u32 or u64 arrays.

The word traversal, partial-word masks, logical selection, population counts,
range construction, mapping iteration, and array layout conversion are Rust
code.

The original C algorithms remain available when the option is disabled or when
``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines 31 public out-of-line entry points in
``rust/kernel/bitmap_ops.rs``. These entry points own the production
algorithms. The public inline functions in ``include/linux/bitmap.h`` retain
their existing names. With ``CONFIG_RUST_BITMAP_OPS`` enabled, their
small-constant fast paths are disabled. A public call therefore enters the
Rust-defined entry point directly.

Twenty-nine entry points retain their ordinary non-GPL export class. C
retains the ``EXPORT_SYMBOL`` metadata in ``lib/bitmap.c``.
``rust/Makefile`` excludes those names from the automatic Rust GPL exporter.

``bitmap_onto()`` and ``bitmap_fold()`` remain unexported, as in the C
baseline. ``rust/Makefile`` records both names in the unexported-symbol list.

Four entry points are inline-only on x86_64 in the C baseline. The Rust
configuration adds these config-gated out-of-line ABI extensions:

* ``__bitmap_scatter()`` and ``__bitmap_gather()``
* ``bitmap_from_arr64()`` and ``bitmap_to_arr64()``

Rust defines the four entry points, and C retains their ordinary
``EXPORT_SYMBOL`` metadata. Modules compiled against the public inline
interfaces can resolve these calls. They are ABI extensions of the
experimental configuration, not changes to the default C ABI.

Retained C boundaries
---------------------

Bitmap allocation and device-managed allocation remain in C because they use
the kernel allocator and device resource APIs.

String parsing, list parsing, sequence-file formatting, and user-copy parsing
remain in C. These interfaces directly depend on parser, printk, sequence
file, and copy-from-user boundaries.

``bitmap_zero()``, ``bitmap_fill()``, ``bitmap_copy()``, and
``bitmap_copy_clear_tail()`` remain optimized memory operations. They operate
as representation-preserving storage operations rather than bitmap policy.

``bitmap_weight_from()`` remains an inline C range helper for this unit. It
constructs the boundary masks and calls the Rust full-weight function for the
interior words.

The inline ``bitmap_read()``, ``bitmap_write()``, ``bitmap_set_value8()``, and
related bitfield accessors remain C operations. They are tightly coupled to
constant folding and bitfield layout.

The bitmap storage remains an array of ``unsigned long`` words. Rust uses the
same word indexing and little-endian byte layout as the existing x86_64
callers.

Important invariants
--------------------

Boolean and scalar operations ignore tail bits at or above the logical bitmap
size. Operations that produce storage may leave tail values unspecified, as
the existing C contract permits.

Range set and clear operations must not pass through an intermediate value that
changes bits outside the requested range. Shift operations must not read or
write outside the logical storage words.

The caller owns synchronization. Rust preserves the existing requirement that
concurrent access uses atomic bitmap operations or external locking.

Validation gate
---------------

The unit must pass these checks before integration:

* A Rust-enabled x86_64 LLVM build.
* The dedicated bitmap-operation KUnit suite.
* The existing bitmap self-test with all checks passing.
* Rust fallback and C-only builds with the same tests.
* Native Rust with KASAN, lockdep, and PREEMPT_RT.
* Rustfmt, Clippy, Rustdoc, kernel-doc, focused Sphinx generation, strict
  checkpatch, and ``git diff --check``.
* Symbol checks that Rust defines all 31 public entry points, the native C
  object defines none of their bodies, and the fallback and C-only objects
  define the complete implementations.
* Export checks that the 29 exported names remain ordinary non-GPL symbols,
  ``bitmap_onto()`` and ``bitmap_fold()`` remain unexported, and the generated
  Rust GPL export header excludes the 29 exported names.
* Adjacent native find-bit and generic radix tree regressions.

Measured validation results
---------------------------

Validation ran on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_BITMAP_OPS=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``,
``CONFIG_BITMAP_OPS_RUST_KUNIT_TEST=y``, and ``CONFIG_TEST_BITMAP=y``. Native
configurations also used ``CONFIG_RUST=y``, ``CONFIG_RUST_FIND_BIT=y``, and
``CONFIG_RUST_BITMAP_OPS=y``.

All six kernel configurations compiled successfully. All nine QEMU runs passed
all three ``bitmap_ops`` KUnit cases. Native, KASAN, lockdep, and RT runs
passed all 391,549 bitmap self-test checks. Fallback and C-only runs passed
all 391,544 checks.

The KASAN run initialized generic KASAN. The lockdep run enabled lock
dependency tracking and prove-locking. The RT kernel reported an
``SMP PREEMPT_RT`` build. The logs contained no BUG, warning, KASAN report,
lock inversion, Oops, panic, or RCU stall.

``rust/kernel.o`` defined all 31 public entry points. The native
``lib/bitmap.o`` defined none of their bodies and held 29 unresolved
references. The fallback and C-only objects defined all 27 baseline C
algorithms. The four config-gated out-of-line paths remain inline or
configuration-defined in those builds.

``Module.symvers`` recorded 29 ``EXPORT_SYMBOL`` entries for the native
configuration and 25 for each fallback configuration. ``bitmap_onto()`` and
``bitmap_fold()`` had no export entry. The generated Rust GPL export header
excluded all 29 exported names, and no ``rust_bitmap_*`` symbols remained.

Adjacent native SMP4 regressions passed all seven ``find_bit_rust`` cases and
all ten ``genradix`` cases.

Rustfmt, Clippy, Rustdoc, kernel-doc, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation rendered the updated
documentation. It reported only the two existing cross-reference warnings in
``Documentation/rust/index.rst`` and ``Documentation/rust/quick-start.rst``.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The bitmap storage type is no longer an array of ``unsigned long``.
* A supported target uses a different endianness or word size.
* A public inline wrapper starts to execute a C bitmap algorithm again.
* Allocation, parser, format, or user-copy ownership becomes part of a
  reusable pure bitmap algorithm.
* Rust compiler support permits C ABI call targets without automatic module
  exports.
* The four config-gated out-of-line ABI extensions become part of a stable
  interface.
* Performance data shows that an important constant-foldable path requires a
  separate review.
