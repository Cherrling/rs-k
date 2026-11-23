.. SPDX-License-Identifier: GPL-2.0

Native Rust interval tree operations
====================================

Status and scope
----------------

The ``CONFIG_RUST_INTERVAL_TREE`` option selects native Rust implementations
of interval insertion, augmented subtree maintenance, overlap search,
overlap iteration, and used or hole span iteration. Rust directly defines
the eight public production entry points.

The option depends on x86_64. Other architectures use the complete C fallback
until their Rust builds and KUnit runs have passed the same validation gate.

The original C algorithms remain available when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

The public ``interval_tree_insert()``, ``interval_tree_remove()``,
``interval_tree_subtree_search()``, ``interval_tree_iter_first()``,
``interval_tree_iter_next()``, ``interval_tree_span_iter_first()``,
``interval_tree_span_iter_next()``, and
``interval_tree_span_iter_advance()`` interfaces keep their existing
signatures and GPL export class. With this option enabled, Rust directly
defines all eight functions.

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

The native ``lib/interval_tree.o`` defines no public interval tree entry
point. It retains only ``rust_interval_tree_link_insert()`` for intrusive
node linking and cached-leftmost insertion, and ``rust_interval_tree_erase()``
for cached augmented erase. The augmented callbacks and rotation primitives
remain at their existing shared boundary. With ``CONFIG_RUST_RBTREE``
enabled, the common insertion, erase, and color rebalancing algorithms run
in Rust.

The complete C interval tree implementation remains compiled in Rust
fallback and C-only configurations.

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

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* The adjacent Rust red-black tree KUnit suite.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that Rust defines all eight public entry points, the native C
  object contains no corresponding function body, the fallback and C-only
  objects contain the complete C implementation, and the GPL export class is
  unchanged.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``18e51565a5a2``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_INTERVAL_TREE=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_INTERVAL_TREE_RUST_KUNIT_TEST=y``.

All nine QEMU runs reported three of three KUnit cases passed.

The ordered case inserted nine intervals and checked red-black tree and
augmentation invariants after every insertion and removal. The span case
checked leading, interior, trailing, and absent holes and adjacent interval
merging. The model case performed 2,000 seeded operations on a pool of 97
nodes and compared every overlap query with the complete model.

The KASAN run initialized generic KASAN. The KASAN and lockdep logs contained
no KASAN report, BUG, warning, Oops, lock inversion, recursive locking report,
or RCU stall. The lockdep run initialized expanded lock dependency state. The
RT kernel reported ``SMP PREEMPT_RT``.

The adjacent Rust red-black tree suite passed all four cases. The same
regression image also passed all three interval tree cases.

In the native configuration, ``rust/kernel.o`` defined all eight public
``interval_tree_*`` symbols. The native ``lib/interval_tree.o`` defined only
``rust_interval_tree_link_insert()`` and ``rust_interval_tree_erase()``, and
it referenced ``rust_rb_erase_augmented()``. It defined no public interval
tree function body.

``vmlinux.symvers`` recorded all eight public symbols as
``EXPORT_SYMBOL_GPL``. The generated Rust GPL export header contained the same
eight symbols. The Rust-fallback and pure C ``lib/interval_tree.o`` objects
defined the complete C algorithms, and the fallback Rust kernel object
contained no public interval tree symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported no errors, warnings,
or checks. Focused Sphinx generation emitted only the existing unknown
``kernel-docs`` document and ``getting_llvm`` label warnings.

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

* Rust-generated GPL export classification changes.

* A second architecture passes the complete validation gate.
