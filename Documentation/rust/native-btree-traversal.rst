.. SPDX-License-Identifier: GPL-2.0

Native Rust B+Tree traversal
============================

Status and scope
----------------

The ``CONFIG_RUST_BTREE`` option selects native Rust implementations of the
production B+Tree lookup, predecessor, last-entry, and leaf-update paths.

Rust owns:

* Search-key navigation through internal nodes.

* Key word access, lexicographic comparison, copying, and zero detection.

* Leaf selection and exact lookup.

* Leaf value publication for updates.

* Descending to the greatest entry.

* Predecessor key decrement, ancestor-path fallback, and previous-entry
  selection.

The original C traversal and leaf-update paths remain available when the
option is disabled. Structural insertion, removal, merging, visitor
traversal, and node reclamation remain separate migration units.

Entry points and ownership
--------------------------

The public entry points ``btree_lookup()``, ``btree_update()``,
``btree_last()``, and ``btree_get_prev()`` and their export classes do not
change. With this option enabled, each C entry point forwards directly to
the matching Rust production function.

Retained C boundaries
---------------------

The B+Tree head, node, and geometry layout definitions remain in C. The public
geometry exposes the word count, pair count, and value offset to Rust. Rust
interprets these fields, accesses keys and values directly, and owns every
traversal decision and key operation.

Node allocation, zeroing, the mempool, and structural tree modification
remain in C. Callers continue to serialize each tree operation under the
existing B+Tree contract.

Important invariants
--------------------

Keys compare as arrays of native machine words in decreasing significance.
The first node key less than or equal to the search key selects the child or
leaf candidate.

An exact leaf match is required for ``btree_lookup()`` and
``btree_update()``. A missing key returns ``NULL`` or ``-ENOENT`` without
changing a caller-provided value.

``btree_last()`` returns ``NULL`` for an empty tree without changing the
caller's key.

``btree_get_prev()`` treats an all-zero key as having no predecessor. It
decrements the search key, follows the greatest qualifying subtree, and
backtracks through the recorded ancestor path to the next lower subtree. It
updates the caller's key only after finding an entry.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases using 32-bit, 64-bit, and 128-bit keys across
  multi-level sparse trees.

* Tests for empty trees, exact looks, missing keys, leaf updates, greatest
  entries, full predecessor walks, and predecessor retry gaps.

* The same tests with native Rust B+Tree traversal disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* The same Rust-enabled runs with KASAN enabled.

* The same Rust-enabled runs with lockdep enabled.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that the enabled C object calls the Rust traversal
  entry points and that the fallback objects contain no such calls.

Validation results
------------------

The final x86_64 LLVM build matrix used these configurations:

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_BTREE=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_BTREE=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration also enabled ``CONFIG_BTREE=y`` and
``CONFIG_BTREE_TRAVERSAL_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
``pass:4 fail:0``. The tests inserted 768 shuffled entries, asserted a tree
height greater than two, and covered 32-bit, 64-bit, and lexicographic 128-bit
keys. The 128-bit test includes a decrement that borrows across machine words.

The KASAN and lockdep logs contained no KASAN report, BUG, warning, Oops,
lock inversion, recursive locking report, or RCU stall.

``rust/kernel.o`` defined ``rust_btree_get_prev``, ``rust_btree_last``,
``rust_btree_lookup``, and ``rust_btree_update``. The native
``lib/btree.o`` referenced all four symbols. The Rust-fallback and pure C
``lib/btree.o`` objects referenced none of them.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx emitted only the two existing warnings for
``../process/kernel-docs`` and the ``getting_llvm`` label.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* B+Tree key ordering, geometry, or node layout changes.

* The zero-key predecessor rule changes.

* Parent-key retry or predecessor cursor semantics change.

* Leaf updates become structural modifications.

* Rust stops directing key storage access and traversal policy.

* The fallback starts to use a Rust B+Tree symbol.
