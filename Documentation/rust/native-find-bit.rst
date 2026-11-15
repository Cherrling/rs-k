.. SPDX-License-Identifier: GPL-2.0

Native Rust find_bit migration
==============================

Status and scope
----------------

The ``CONFIG_RUST_FIND_BIT`` option selects an experimental native Rust
implementation of the generic bit search algorithms. The migration currently
supports x86_64 and keeps the existing ``unsigned long`` bitmap layout and
caller-facing API.

The Rust code owns the production implementation of these operations:

* First set-bit and zero-bit searches.
* First AND, AND-NOT, and three-input AND searches.
* Next set-bit, zero-bit, AND, AND-NOT, and OR searches.
* Nth set-bit, AND, and AND-AND-NOT searches.
* Last set-bit search.
* Eight-bit clump search.
* Random set-bit selection.

Rust owns the word traversal, range masks, partial-word filtering, population
counts, nth-bit selection, trailing and leading bit selection, and clump
extraction.

The original C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The inline callers in ``include/linux/find.h`` keep their existing names. When
the Rust option is enabled, the inline small-constant fast paths are disabled.
All public calls therefore enter the existing out-of-line entry points instead
of executing a second, C-owned algorithm.

The production algorithms are in ``rust/kernel/find_bit.rs``. The existing
``lib/find_bit.c`` file provides the exported C ABI adapters. The adapters
keep the original symbol names and ordinary ``EXPORT_SYMBOL`` classes:

* ``_find_first_bit()``
* ``_find_first_and_bit()``
* ``_find_first_andnot_bit()``
* ``_find_first_and_and_bit()``
* ``_find_first_zero_bit()``
* ``_find_next_bit()``
* ``_find_next_and_bit()``
* ``_find_next_andnot_bit()``
* ``_find_next_or_bit()``
* ``_find_next_zero_bit()``
* ``__find_nth_bit()``
* ``__find_nth_and_bit()``
* ``__find_nth_and_andnot_bit()``
* ``_find_last_bit()``
* ``find_next_clump8()``
* ``find_random_bit()``

Retained C and architecture boundary
------------------------------------

The C ABI adapters preserve the existing call and module interfaces. They
forward directly to Rust and do not select a result or implement a search.

The bitmap storage remains an array of ``unsigned long`` words. Rust reads
these words with the same layout and indexing rules as the rest of the
x86_64 kernel. A separate Rust type for bitmap storage would change callers
and is outside this migration unit.

The random set-bit operation uses ``__get_random_u32_below()`` as the kernel
random-number primitive. Rust owns the weight calculation and the mapping from
the random ordinal to a bitmap position.

The C fallback remains compiled only when ``CONFIG_RUST_FIND_BIT`` is disabled.
This unit does not change architecture assembly files.

Important invariants
--------------------

All searches ignore bits at or above ``size``. A search that finds no matching
bit returns ``size``.

The first and next searches mask the first and last words as required. The nth
search subtracts the population count of each complete word and masks the
partial last word. The last search scans complete words from the end and masks
the partial last word before selecting its highest set bit.

A clump search first finds a set bit, rounds its position down to an eight-bit
boundary, and copies the byte that contains that position.

The random search returns ``size`` for an empty bitmap. It returns the only set
bit directly for a singleton. For a larger bitmap, it selects an ordinal in the
set-bit count and resolves that ordinal with the native nth search.

Validation gate
---------------

The unit must pass these checks before integration:

* A Rust-enabled x86_64 LLVM build.
* The Rust KUnit suite and the existing bitmap self-test.
* A Rust-disabled C fallback build and the same tests.
* A Rust-enabled x86_64 build with KASAN.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.
* Export-class checks for every adapter.
* A check that production callers enter Rust when the option is enabled.

Measured validation results
---------------------------

The Rust-enabled x86_64 LLVM build completed before the final boot tests. The
KUnit suite passed all seven test cases with one and four virtual CPUs. A
repeated four-CPU run also passed all seven cases. Each run passed all 391,544
checks in the existing bitmap self-test.

The x86_64 LLVM build with ``CONFIG_KASAN=y`` passed all seven KUnit cases on
four virtual CPUs. The bitmap self-test passed all 391,544 checks in that run.
The boot log contained no KASAN report, BUG, Oops, or panic.

The Rust-disabled C fallback built and booted successfully. With the option
disabled, the Rust find-bit symbols were absent, and the bitmap self-test
passed all 391,544 checks. This result shows that the retained C fallback
remains functional.

Review conditions
-----------------

Reopen the boundary decision if any of these conditions changes:

* The kernel adopts a bitmap storage type that is not an array of
  ``unsigned long``.
* The project supports a big-endian target.
* A C adapter starts to select a result or contain search policy.
* A public inline wrapper starts to execute a C search algorithm again.
* The random-number primitive or its range contract changes.
* Performance data shows that the out-of-line transition requires a separate
  review.
