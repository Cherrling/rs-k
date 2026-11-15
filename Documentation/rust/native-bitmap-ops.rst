.. SPDX-License-Identifier: GPL-2.0

Native Rust bitmap operation migration
======================================

Status and scope
----------------

The ``CONFIG_RUST_BITMAP_OPS`` option selects an experimental native Rust
implementation of the computational bitmap operations in ``lib/bitmap.c``.
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

Entry points and ownership
--------------------------

The public inline functions retain their existing names. With
``CONFIG_RUST_BITMAP_OPS`` enabled, the small-constant and aligned-memory fast
paths in ``include/linux/bitmap.h`` are disabled. Calls enter the existing C
ABI symbols, and the adapters in ``lib/bitmap.c`` forward directly to Rust.

The production algorithms are in ``rust/kernel/bitmap_ops.rs``. The adapters
retain the original export classes for the 27 symbols that already had
out-of-line definitions, including:

* ``__bitmap_equal()``
* ``__bitmap_or_equal()``
* ``__bitmap_and()``
* ``__bitmap_or()``
* ``__bitmap_xor()``
* ``__bitmap_andnot()``
* ``__bitmap_replace()``
* ``__bitmap_intersects()``
* ``__bitmap_subset()``
* ``__bitmap_weight()``
* ``__bitmap_set()``
* ``__bitmap_clear()``
* ``bitmap_cut()``
* ``bitmap_find_next_zero_area_off()``
* ``bitmap_remap()`` and ``bitmap_bitremap()``
* ``bitmap_onto()`` and ``bitmap_fold()``
* ``bitmap_from_arr32()`` and ``bitmap_to_arr32()``

Four operations are inline-only on x86_64 in the C baseline. The Rust
configuration creates config-gated out-of-line adapters for these operations:

* ``__bitmap_scatter()`` and ``__bitmap_gather()``
* ``bitmap_from_arr64()`` and ``bitmap_to_arr64()``

The four adapters use ordinary ``EXPORT_SYMBOL`` classes. Modules compiled
against the public inline interfaces can therefore resolve the calls. These
are ABI extensions of the experimental configuration, not changes to the
default C ABI.

The 31 ``rust_bitmap_*`` call targets are also listed as
``EXPORT_SYMBOL_GPL`` because the current Rust compiler support automatically
exports unmangled Rust symbols. They remain config-gated implementation
details and are not stable interfaces.

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
* A Rust-disabled fallback build and the same bitmap self-test.
* A Rust-enabled x86_64 KASAN build.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.
* Export-class checks for every public adapter.
* Disassembly checks showing public bitmap wrappers call the C adapters, and
  the adapters forward to Rust.

Measured validation results
---------------------------

The final normal x86_64 LLVM build passed. All enabled KUnit tests passed on
one virtual CPU, four virtual CPUs, and a repeated four-CPU run. Each run
passed all three ``bitmap_ops`` cases and all seven ``find_bit_rust`` cases.
Each run also passed all 391,549 checks in the existing bitmap self-test.

The Rust-disabled C fallback passed all enabled KUnit tests and all 391,544
checks in the existing bitmap self-test. The image contained no
``rust_bitmap_*`` production symbols.

The x86_64 KASAN build passed all three ``bitmap_ops`` cases and all seven
``find_bit_rust`` cases on four virtual CPUs. The existing bitmap self-test
passed all 391,549 checks. The boot log contained no KASAN report, BUG, Oops,
or panic.

Object-level disassembly showed the public bitmap wrappers select the C ABI
adapters, and the adapters tail-call the 31 Rust implementations. The 27
pre-existing exports matched their baseline export classes.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The bitmap storage type is no longer an array of ``unsigned long``.
* A supported target uses a different endianness or word size.
* A C adapter starts to select a result or contain bitmap policy.
* A public inline wrapper starts to execute a C bitmap algorithm again.
* Allocation, parser, format, or user-copy ownership becomes part of a
  reusable pure bitmap algorithm.
* Rust compiler support permits C ABI call targets without automatic module
  exports.
* The four config-gated out-of-line ABI extensions become part of a stable
  interface.
* Performance data shows that an important constant-foldable path requires a
  separate review.
