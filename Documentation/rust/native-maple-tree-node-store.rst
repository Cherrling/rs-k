.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree node stores
==================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_NODE_STORE`` option selects a native Rust
production implementation of ``mas_wr_node_store()``. The option depends on
the native Rust write dispatcher.

Rust owns these operations:

* Recompute the replacement node end.
* Preserve ranges before the insertion point.
* Retain the start or end of a partially overwritten range.
* Copy ranges after the overwritten region.
* Clear unused storage in a locked reuse node.
* Set leaf metadata and update gaps.
* Install the replacement node in RCU or locked mode.
* Update the state end and emit the write tracepoint.

The Rust write dispatcher selects this implementation for ``wr_node_store``.
Rust does not call a C implementation of the complete strategy.

Retained C boundary
-------------------

The C boundary contains these operations:

* ``mas_pop_node()`` obtains a preallocated node from the existing node pool.
* The parent-field accessor copies the node parent.
* The raw node copy applies a completed locked reuse node.
* ``mas_replace_node()`` publishes an encoded RCU replacement.
* Layout helpers return pivot and slot arrays.
* Helpers publish individual slots, set metadata, update gaps, and emit
  tracepoints.

These helpers do not calculate the new node size, choose retained ranges,
copy retained ranges, or select the read-copy-update (RCU) strategy.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* A locked store builds a complete reuse node before copying it into the live
  node.
* An RCU store uses one preallocated node and publishes it only after all
  slots and pivots are complete.
* Slot publication uses the existing RCU assignment primitive.
* The parent encoded node does not change until the replacement is complete.
* Pivot and slot copies preserve inclusive range boundaries.
* Unused locked-node storage is cleared before publication.
* The state end changes after the gap update, matching the C sequence.
* Tracepoint placement remains unchanged.

Testing
-------

The ``maple_tree_store`` KUnit suite runs with native Rust and with the C
fallback. It covers a locked node reuse, an RCU node replacement, exact-fit
writes, appends, slot stores, gap reuse, and concurrent RCU readers.

The Maple Tree self-test and the focused KUnit suite run on one and four CPUs.
The test matrix also runs KASAN and lockdep configurations.

Validation on 2026-11-17 used parent commit
``6f2c98bfbd97cc429e12627d1288c1fe9647b3c0``. All configurations passed
the focused KUnit suite:

.. list-table::
   :header-rows: 1

   * - Configuration
     - CPUs
     - Focused KUnit
     - Maple Tree self-test
   * - Native Rust
     - 1
     - 8/8
     - 5,357,656/5,357,656
   * - Native Rust
     - 4
     - 8/8
     - 5,357,707/5,357,707
   * - C fallback
     - 1
     - 8/8
     - 5,357,656/5,357,656
   * - C fallback
     - 4
     - 8/8
     - 5,357,707/5,357,707
   * - KASAN with native Rust
     - 1
     - 8/8
     - 5,357,656/5,357,656
   * - KASAN with native Rust
     - 4
     - 8/8
     - 5,357,707/5,357,707
   * - Lockdep with native Rust
     - 1
     - 8/8
     - 3,809,321/3,809,321
   * - Lockdep with native Rust
     - 4
     - 8/8
     - 3,809,321/3,809,321

The KASAN logs contained no KASAN reports. The lockdep logs contained no
lockdep warnings or call traces. Lockdep requires
``CONFIG_DEBUG_MAPLE_TREE=n``, and all lockdep runs used that setting.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
The focused Sphinx build produced no warning for this document.

A C-only build with ``CONFIG_RUST=n`` compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_store_kunit.o``. The C-only object contained the C
``mas_wr_store_entry`` and no Rust dispatcher or node-store helper references.
The native Rust object exported ``rust_mas_wr_store_entry`` and referenced the
five retained node-store helpers.

Reopen conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* The encoded-node or parent layout changes.
* The node-pool ownership rules change.
* RCU replacement stops accepting a complete replacement node.
* Pivot or slot layout no longer matches the Rust size calculation.
* Locked node reuse requires an atomic update instead of a copy.
* A native Rust helper replaces the C RCU publication primitive.
