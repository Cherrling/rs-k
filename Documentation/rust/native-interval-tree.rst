.. SPDX-License-Identifier: GPL-2.0

Native Rust interval tree operations
====================================

Status and scope
----------------

The ``CONFIG_RUST_INTERVAL_TREE`` option selects native Rust implementations
of interval insertion, augmented subtree maintenance, overlap search,
overlap iteration, and used or hole span iteration.

The option depends on x86_64. Other architectures use the complete C fallback
until their Rust builds and KUnit runs have passed the same validation gate.

The original C algorithms remain available when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

The public ``interval_tree_insert()``, ``interval_tree_remove()``,
``interval_tree_subtree_search()``, ``interval_tree_iter_first()``,
``interval_tree_iter_next()``, and span iterator interfaces keep their
existing signatures. With this option enabled, the C entry points forward to
the matching Rust functions.

Rust owns:

* Interval ordering by start endpoint and the placement of equal starts in the
  right subtree.

* Maintenance of the maximum endpoint in every subtree crossed during
  insertion.

* Fast rejection of disjoint ranges.

* First-match and next-match overlap search.

* Used and hole span construction.

* Merging of overlapping or adjacent used intervals.

* Span advance and iterator completion decisions.

Rust reads and updates the intrusive interval and red-black tree nodes
directly. Callers continue to serialize tree mutations under the existing
interval tree contract.

Retained C boundaries
---------------------

The ``interval_tree_node``, ``rb_node``, ``rb_root_cached``, and
``interval_tree_span_iter`` layouts remain in C. These layouts are shared by
MMU notifier, KVM, memory failure, IOMMU, and other kernel users.

Small C helpers retain ``rb_link_node()``, cached-leftmost insertion, and
cached augmented erase. The common augmented red-black tree callbacks and
rotation primitives remain at their existing boundary. With
``CONFIG_RUST_RBTREE`` enabled, the common insertion, erase, and color
rebalancing algorithms run in Rust.

The public C entry points retain the exported ABI. The complete C interval
tree implementation remains compiled in fallback configurations.

Important invariants
--------------------

Intervals use inclusive endpoints. A node overlaps query range
``[start, last]`` when ``node->start <= last`` and
``start <= node->last``.

The red-black tree is ordered by increasing start endpoint. Equal starts are
inserted into the right subtree.

``__subtree_last`` is the greatest last endpoint in each subtree. Insertion
updates this value along the search path, and the augmented callbacks update
it after rotations.

The cached leftmost node is the interval with the smallest start endpoint.
The root subtree maximum and the cached leftmost node reject disjoint queries
before the recursive search.

Overlap iteration returns each matching interval once and in nondecreasing
start order.

Used spans merge intervals that overlap or are adjacent. Hole spans are the
complement of used spans inside the requested range. Span arithmetic uses
wrapping unsigned endpoints, matching the C implementation.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Ordered insertion, red-black tree invariants, subtree maxima, cached
  leftmost maintenance, overlap queries, subtree search, and removal.

* Used and hole spans with leading, interior, trailing, and absent holes.

* Merging of overlapping and adjacent intervals.

* A seeded model comparison after every mutation and query.

* The same tests with native Rust interval tree operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Rust-enabled KASAN and lockdep runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that the native C object calls the Rust entry points and that
  fallback objects reference no Rust interval tree symbol.

Validation results
------------------

Validation on 2026-11-17 used parent commit ``8dfaebd1fa04``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_INTERVAL_TREE=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_INTERVAL_TREE=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, and ``CONFIG_INTERVAL_TREE_RUST_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
three of three KUnit cases passed.

The ordered case inserted nine intervals and checked red-black tree and
augmentation invariants after every insertion and removal. The span case
checked leading, interior, trailing, and absent holes and adjacent interval
merging. The model case performed 2,000 seeded operations on a pool of 97
nodes and compared every overlap query with the complete model.

The KASAN run initialized generic KASAN. The KASAN and lockdep logs contained
no KASAN report, BUG, warning, Oops, lock inversion, recursive locking report,
or RCU stall.

In the native configuration, ``lib/interval_tree.o`` referenced all eight
``rust_interval_tree_*`` entry points. ``rust/kernel.o`` defined all eight
symbols. The Rust-fallback and pure C ``lib/interval_tree.o`` objects
referenced no Rust interval tree symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported no errors, warnings,
or checks. Focused Sphinx rendered this page and the updated index. It
reported only the existing unknown ``kernel-docs`` document and
``getting_llvm`` label references.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Interval node, red-black tree node, cached root, or span iterator layout
  changes.

* Start ordering, equal-start placement, or inclusive overlap semantics
  changes.

* Subtree maximum maintenance or augmented callback behavior changes.

* Cached leftmost maintenance changes.

* Overlap iteration order or span adjacency rules change.

* Rust stops controlling ordering, search, overlap iteration, or span state.

* A fallback object starts to use a Rust interval tree symbol.

* A second architecture passes the complete validation gate.
