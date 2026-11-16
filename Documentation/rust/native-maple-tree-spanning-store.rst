.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree spanning stores
======================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_SPANNING_STORE`` option selects a native Rust
production implementation of ``mas_wr_spanning_store()``. The option depends
on the native Rust node-store and write-dispatch units.

Rust owns these operations:

* Walk the left and right boundaries of the affected range.
* Extend a NULL write over adjacent NULL ranges.
* Initialize the simulated leaf for the replacement data.
* Select and preserve the participating left and right sources.
* Calculate the one-indexed replacement size.
* Select one, two, or three destination nodes.
* Copy bounded source ranges into each destination.
* Adjust destination boundaries that end in a NULL slot.
* Use the native leaf-gap and allocation-range metadata values for completed
  destination nodes.
* Ascend until the replacement converges or becomes a new root.
* Publish the completed replacement subtree.

The Rust write dispatcher selects ``wr_spanning_store`` directly. Rust does
not call the C implementation of the complete spanning-store strategy.

Retained C boundary
-------------------

The C boundary contains these operations:

* Decode encoded nodes and return concrete pivot and slot arrays.
* Read protected slots and initialize encoded replacement slots.
* Copy and finalize raw node arrays when the node-copy option is disabled.
* Walk a write state to its boundary leaf and ascend it after convergence.
* Discover an adjacent sibling without changing tree contents.
* Obtain a preallocated node from the existing node pool.
* Retain leaf-gap and allocation-range metadata helpers where native options
  are disabled.
* Install parent and root state.
* Publish the complete subtree and release replaced nodes.

When the root-store option is disabled, the whole-range result uses the
retained C ``mas_new_root()`` strategy. When it is enabled, the spanning unit
calls the native Rust root store. The spanning unit does not copy, duplicate,
or delegate to the C spanning-store algorithm. Split and rebalance stores
remain separate migration units.

Source order
------------

The copy engine preserves the C source order:

1. The complete left sibling, when selected.
2. The retained prefix of the left boundary node.
3. The simulated leaf containing the new range.
4. The retained suffix of the right boundary node.
5. The complete right sibling, when selected.

The copy size is one-indexed. Each source records its inclusive start and end
offset plus the maximum range represented by that source.

Destination selection
---------------------

One destination is selected when all data fits in one node. Otherwise, two
destinations are selected with a balanced split.

Exact two-node capacity has a leaf-specific NULL-boundary check. If the first
split ends on a NULL slot, Rust moves the split by one. If neither adjacent
boundary works, Rust selects a balanced three-way split. A larger data size
always uses the balanced three-way split.

After a destination is complete, Rust checks whether its final slot is NULL.
If the destination has not reached the full range, Rust moves the last entry
to the next destination and restores the preceding pivot maximum.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* The right walk starts at the next slot after ``mas->last``.
* A NULL spanning write includes adjacent NULL ranges before copying.
* Every node copy is bounded by both the source range and destination size.
* Replacement nodes are private until the final RCU publication.
* Gap metadata is complete before publication.
* A converged one-node result keeps the current encoded parent.
* A full-range result resets the walk state before installing a new root.
* The C fallback remains complete when the option is disabled.

Testing
-------

The ``maple_tree_store`` KUnit suite covers a non-allocation spanning store,
an allocation-range spanning store, an RCU spanning store, and a NULL spanning
store. Existing cases cover dispatch, slot stores, node stores, gap reuse, and
concurrent RCU exact-fit writes.

The unit validation gate includes:

* Native Rust and C-fallback focused KUnit runs on one and four CPUs.
* The complete Maple Tree self-test in every focused run.
* Native Rust KASAN runs on one and four CPUs.
* Native Rust lockdep runs on one and four CPUs.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* A C-only build with ``CONFIG_RUST=n``.
* Object inspection for the complete C fallback and the Rust production path.

Validation on 2026-11-17 used parent commit
``59ef080affdef6fc1160c593c1582ce0f1e70c20``. Native Rust, C fallback, KASAN,
and lockdep configurations passed all 11 focused KUnit cases on one and four
CPUs. The native Rust, C fallback, and KASAN runs passed 5,357,658 checks on
one CPU and 5,357,709 checks on four CPUs. The lockdep runs passed 3,809,321
checks on both CPU counts.

The KASAN logs contained no KASAN report. The lockdep logs contained no
lockdep warning or call trace. Lockdep used ``CONFIG_DEBUG_MAPLE_TREE=n``.

Rustfmtcheck, Clippy, Rustdoc, strict checkpatch, and ``git diff --check``
passed. A forced Rust Sphinx rebuild reported two existing cross-reference
warnings in ``Documentation/rust/index.rst`` and
``Documentation/rust/quick-start.rst``. It reported no warning for this
document.

A C-only build used ``CONFIG_RUST=n`` and compiled ``lib/maple_tree.o`` plus
``lib/tests/maple_tree_store_kunit.o``. The C object contains the local C
``mas_wr_store_entry`` implementation and no Rust reference. The Rust kernel
object defines ``rust_mas_wr_store_entry`` and references the retained C
primitives.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded-node or parent layout changes.
* Node-pool ownership or replacement-node allocation rules change.
* RCU publication requires a different completion or cleanup sequence.
* Sibling discovery no longer has bounded traversal behavior.
* Gap metadata no longer summarizes a completed destination node.
* The copied source order or split policy changes.
* A C helper starts to implement spanning-store source selection, destination
  sizing, NULL adjustment, or convergence policy.
