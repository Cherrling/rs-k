.. SPDX-License-Identifier: GPL-2.0

Native Rust B+Tree visitors
===========================

Status and scope
----------------

The ``CONFIG_RUST_BTREE`` option selects native Rust implementations of
``btree_visitor()`` and ``btree_grim_visitor()``. These functions provide the
production recursive visitor, entry accounting, and destructive tree reaping
paths.

Rust owns:

* Depth-first traversal from the root to each leaf.

* Termination at the first empty pair in each node.

* Preorder visitation of the leaf entries.

* Entry index accounting and total result counts.

* Callback dispatch and the count-only behavior when the callback is NULL.

* Postorder release of every visited node for grim visitors.

* Reset of the tree node and height after a grim visitor.

The original visitor algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public ``btree_visitor()`` and ``btree_grim_visitor()`` interfaces and
their GPL export classes do not change. With this option enabled, Rust
directly defines both public entry points. The C fallback retains the complete
implementations.

The typed ``btree_visitor32()``, ``btree_visitor64()``,
``btree_visitor128()``, and matching grim visitor interfaces keep their
existing signatures. Their C callbacks unwrap and widen the selected key type
before invoking the caller's function.

Rust directly reads node keys and child or value slots. It controls recursion,
visit order, counting, and reclamation.

Retained C boundaries
---------------------

The B+Tree head, geometry, and node layout definitions remain in C. Rust uses
the retained geometry to address keys and value slots in each node.

Node allocation, zeroing, release, and the mempool remain C primitives. A grim
visitor returns each node through the tree's mempool and leaves the mempool
owned by the tree head.

The small typed visitor callback adapters remain in C. They translate native
machine-word keys into the public 32-bit, 64-bit, or 128-bit callback
signatures.

Callers continue to serialize visitors with writers under the existing B+Tree
contract. A visitor callback must not mutate the tree.

Important invariants
--------------------

A non-grim visitor leaves the root, height, keys, values, and mempool
unchanged. It reports the number of visited leaf entries, even when the
callback is NULL.

Each node contributes occupied pairs up to the first empty slot. Internal
pairs select child subtrees. Leaf pairs supply the callback value and key.

Traversal visits pair zero first in every node. In this B+Tree layout, that
order starts with the greatest key and proceeds toward the lowest key.

The entry index starts at zero and increases by one for every visited leaf
entry. The callback receives the current index before Rust increments it.

A grim visitor invokes callbacks in the same order, releases every visited
node after processing its entries, and resets the head to an empty tree. The
tree can be reused after the grim visitor returns.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Multi-level 32-bit, 64-bit, and 128-bit trees with sparse keys.

* Descending visitor order, stable indices, callback values, and total counts.

* A NULL callback, which counts entries without changing the tree.

* Grim visitor traversal, node release, head reset, and reuse after draining.

* The same tests with native Rust B+Tree operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* The same Rust-enabled runs with KASAN enabled.

* The same Rust-enabled runs with lockdep enabled.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that Rust defines both visitor entry points, that the
  native C object defines neither, and that the fallback and C-only objects
  contain the complete implementations and GPL exports.

Validation results
------------------

Validation of the native algorithm migration on 2026-11-17 used parent commit
``b8dbff5b61ee``. Direct public-entry validation is documented in
:doc:`native-btree-entry`.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_BTREE=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_BTREE=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration also enabled ``CONFIG_BTREE=y``,
``CONFIG_BTREE_TRAVERSAL_KUNIT_TEST=y``,
``CONFIG_BTREE_STRUCTURAL_KUNIT_TEST=y``, and
``CONFIG_BTREE_VISITOR_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
four traversal passes, six structural passes, and four visitor passes.

The visitor tests used 768 sparse entries in multi-level 32-bit, 64-bit, and
128-bit trees. They checked descending order, stable indices, keys, values,
total counts, NULL-callback counting, grim traversal, node release, tree-head
reset, and tree reuse.

The KASAN and lockdep logs contained no KASAN report, BUG, warning, Oops,
lock inversion, recursive locking report, or RCU stall.

At that migration stage, ``rust/kernel.o`` defined all nine
``rust_btree_*`` entry points. The native ``lib/btree.o`` referenced both
visitor entry points. The Rust-fallback and pure C ``lib/btree.o`` objects
referenced neither visitor entry point.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and ``git diff --check``
passed. Strict checkpatch reported no errors or checks. It retained the generic
new-file warning after the patch added MAINTAINERS entries for both new files.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* B+Tree node layout, pair order, geometry, or empty-slot termination changes.

* Visitor ordering, index, count, or NULL-callback semantics change.

* Grim visitor reclamation or tree reset semantics change.

* Mempool allocation, release, or ownership semantics change.

* Rust stops directing visitor traversal, counting, callback dispatch, or
  reclamation.

* A fallback object starts to use a Rust B+Tree visitor symbol.
