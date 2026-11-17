.. SPDX-License-Identifier: GPL-2.0

Native Rust B+Tree structural operations
========================================

Status and scope
----------------

The ``CONFIG_RUST_BTREE`` option selects native Rust implementations of the
production B+Tree insertion, removal, rebalancing, and ordinary merge paths.
The option also selects the traversal and leaf-update paths documented in
``native-btree-traversal``.

Rust owns:

* Placement and shifting of entries during insertion and removal.

* Node allocation decisions, splitting, and split rollback.

* Root growth and root shrink.

* Underfull-child detection and sibling merge selection.

* Recursive removal of obsolete parent entries.

* Node-content transfer and node reclaim decisions.

* Entry transfer between two ordinary trees.

* Empty-target merge by direct root transfer.

The original structural algorithms remain available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``btree_insert()``, ``btree_remove()``, and ``btree_merge()``
interfaces and their export classes do not change. With this option enabled,
each C entry point forwards directly to the matching Rust production
function.

Rust reads and writes the node key and value arrays. It owns the B+Tree
algorithm, including every recursive descent, split, merge, and reclamation
decision. The C adapters only provide storage allocation and the public ABI.

Retained C boundaries
---------------------

The ``btree_head``, ``btree_geo``, and node layout definitions remain in C.
The public geometry exposes key length, pair count, and value offset to Rust.

The mempool allocation and release primitives remain C. Rust calls
``btree_rust_node_alloc()`` to obtain a zeroed node and
``btree_rust_node_free()`` to return a node. Mempool policy and the caller's
``gfp_t`` contract remain unchanged.

Callers continue to serialize each writer operation under the existing B+Tree
contract. The native implementation adds no lock or RCU policy.

Two trees joined by an ordinary merge must use compatible mempools. An
empty-target merge transfers the victim root without allocation, and later
operations use the target mempool. This is the same ownership contract as the
C implementation.

Important invariants
--------------------

Values and entry keys are non-NULL. Duplicate keys cause the same fatal
diagnostic as the C implementation, through the kernel panic path used for
unrecoverable B+Tree violations.

Internal-node keys select the first child whose boundary key is less than or
equal to the search key. A missing rightmost boundary is repaired from the
search key before descent.

A full node splits at half its pair capacity. The upper half moves to the new
node, and the old upper boundary is inserted in the parent. If a parent
allocation fails, the unlinked child is released and the tree remains valid.

Removal shifts the remaining entries left and clears the vacated pair. An
underfull child merges with the left sibling when possible, then with the
right sibling. A zero-fill child is removed from its parent and released. A
non-leaf root with one child shrinks to that child.

An ordinary tree merge moves entries from greatest to least. A failure can
leave entries already moved in the target, as permitted by the public API. An
empty target instead transfers the complete victim root.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* KUnit cases covering 32-bit, 64-bit, and 128-bit keys in multi-level
  trees.

* Full removal, partial removal, removal and reinsertion, sparse insertion,
  root growth, root shrink, sibling merges, and ordinary tree merges.

* Allocation failures during initial root allocation, leaf splitting, and
  parent-level growth, including the resulting rollback state.

* The same tests with native Rust B+Tree operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* The same Rust-enabled runs with KASAN enabled.

* The same Rust-enabled runs with lockdep enabled.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that the enabled C object calls all three structural
  Rust entry points and that the fallback objects contain no such calls.

Validation results
------------------

Validation on 2026-11-17 used parent commit
``30f2299a7f10``.

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
``CONFIG_BTREE_TRAVERSAL_KUNIT_TEST=y``, and
``CONFIG_BTREE_STRUCTURAL_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
four traversal passes and six structural passes. The structural tests used
768 sparse entries in multi-level 32-bit, 64-bit, and 128-bit trees. They
covered complete removal and reinsertion, partial removal, ordinary tree
merge, empty-target merge, initial root allocation failure, leaf split
failure, and parent-level growth failure.

The KASAN and lockdep logs contained no KASAN report, BUG, warning, Oops,
lock inversion, recursive locking report, or RCU stall.

``rust/kernel.o`` defined all seven ``rust_btree_*`` entry points. The native
``lib/btree.o`` referenced all seven symbols and defined the two retained node
allocation helpers. The Rust-fallback and pure C ``lib/btree.o`` objects
referenced none of the Rust entry points.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx emitted only the two existing warnings for
``../process/kernel-docs`` and the ``getting_llvm`` label.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* B+Tree key ordering, geometry, or node layout changes.

* Mempool allocation, zeroing, release, or ownership semantics change.

* Root growth, split placement, underfull rebalancing, or reclamation policy
  changes.

* Empty-target merge or partial-failure ownership semantics change.

* Rust stops directing structural algorithm decisions.

* A fallback object starts to use a Rust B+Tree structural symbol.
