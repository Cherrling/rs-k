.. SPDX-License-Identifier: GPL-2.0

Native Rust find_bit migration
==============================

Status and scope
----------------

The ``CONFIG_RUST_FIND_BIT`` option selects native Rust implementations of the
generic bit search algorithms. The migration currently supports x86_64 and
keeps the existing ``unsigned long`` bitmap layout and caller-facing API.

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

Rust directly defines the sixteen public out-of-line entry points and retains
their original ordinary non-GPL export class. The inline callers in
``include/linux/find.h`` keep their existing names. When the Rust option is
enabled, the inline small-constant fast paths are disabled. All public calls
therefore enter the Rust-defined entry points instead of executing a second,
C-owned algorithm.

Rust directly defines these entry points:

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

The native ``lib/find_bit.o`` defines no search function bodies. C retains the
ordinary ``EXPORT_SYMBOL`` metadata for the sixteen Rust-defined symbols.
``rust/Makefile`` excludes those names from the automatic Rust GPL exporter so
the export class stays unchanged.

The bitmap storage remains an array of ``unsigned long`` words. Rust reads
these words with the same layout and indexing rules as the rest of the
x86_64 kernel. A separate Rust type for bitmap storage would change callers
and is outside this migration unit.

The random set-bit operation uses ``__get_random_u32_below()`` as the kernel
random-number primitive. Rust owns the weight calculation and the mapping from
the random ordinal to a bitmap position.

The complete C algorithms remain compiled when ``CONFIG_RUST_FIND_BIT`` is
disabled or when ``CONFIG_RUST`` is disabled. This unit does not change
architecture assembly files.

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

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_FIND_BIT=n`` and
  ``CONFIG_RUST_BITMAP_OPS=n`` and runs the same tests.

* C-only builds with ``CONFIG_RUST=n`` and runs the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep and prove-locking.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* The dedicated search KUnit suite and the existing bitmap self-test.

* Symbol checks that Rust defines all sixteen public entry points, the native
  C object defines none of their bodies, and the fallback and C-only objects
  define the complete implementations.

* Export checks that all sixteen names remain ordinary non-GPL symbols and
  the generated Rust GPL export header excludes the sixteen Rust entries.

* Rustfmt, Clippy, Rustdoc, kernel-doc, focused Sphinx generation, strict
  checkpatch, and ``git diff --check``.

* Adjacent bitmap-operation and generic radix tree regressions.

Validation results
------------------

Validation runs on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_FIND_BIT=n`` and
                  ``CONFIG_RUST_BITMAP_OPS=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``,
``CONFIG_FIND_BIT_RUST_KUNIT_TEST=y``, and ``CONFIG_TEST_BITMAP=y``. Native
configurations also used ``CONFIG_RUST=y``, ``CONFIG_RUST_FIND_BIT=y``, and
``CONFIG_RUST_BITMAP_OPS=y``.

All six kernel configurations compiled successfully. All nine QEMU runs passed
all seven search KUnit cases. Native, KASAN, lockdep, and RT runs passed all
391,549 bitmap self-test checks. Fallback and C-only runs passed all 391,544
checks.

The KASAN run initialized generic KASAN. The lockdep run enabled lock
dependency tracking and prove-locking. The RT kernel reported an
``SMP PREEMPT_RT`` build. The logs contained no BUG, warning, KASAN report,
lock inversion, Oops, panic, or RCU stall.

Fallback validation found that the C nth search could return a value above
``size`` when no requested bit existed. The fallback now clamps that result to
``size``, which matches the stricter Rust sentinel behavior.

``rust/kernel.o`` defined all sixteen public entry points. The native
``lib/find_bit.o`` defined none of their bodies and held sixteen unresolved
references. The fallback and C-only objects defined all sixteen bodies.

``vmlinux.symvers`` recorded sixteen ``EXPORT_SYMBOL`` entries. The generated
Rust GPL export header excluded all sixteen public names, and no
``rust_find_*`` symbols remained.

Adjacent native SMP4 regressions passed: three bitmap-operation cases and ten
generic radix tree cases.

Rustfmt, Clippy, Rustdoc, kernel-doc, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation rendered the updated
page and linked it from the Rust documentation index.

Review conditions
-----------------

Reopen the boundary decision if any of these conditions changes:

* The kernel adopts a bitmap storage type that is not an array of
  ``unsigned long``.
* The project supports a big-endian target.
* A C search body starts to select a result or contain search policy.
* A public inline wrapper starts to execute a C search algorithm again.
* The random-number primitive or its range contract changes.
* Performance data shows that the out-of-line transition requires a separate
  review.
