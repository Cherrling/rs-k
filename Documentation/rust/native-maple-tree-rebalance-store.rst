.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree rebalance stores
=======================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_REBALANCE_STORE`` option selects a native Rust
production implementation of ``mas_wr_rebalance()``. The option depends on the
native Rust spanning-store unit because all replacement strategies share the
same copy engine.

Rust owns these operations:

* Calculate the one-indexed replacement size.
* Detect a sparse node that cannot retain its current layout.
* Select a sibling that can donate data or absorb the sparse node.
* Prefer the right sibling for sparse-node rebalancing.
* Select an adjacent sibling for a large replacement when it has room.
* Preserve the left, current, and right source order.
* Select one, two, or three destination nodes.
* Adjust destinations that end in a NULL slot.
* Select destination boundaries used for gap metadata.
* Ascend until the replacement converges or becomes a new root.
* Publish the completed replacement subtree.

The Rust write dispatcher selects ``wr_rebalance`` directly. Rust does not call
the C implementation of the complete rebalance-store strategy.

Retained C boundary
-------------------

The C boundary contains these operations:

* Test whether an encoded node is the root.
* Ascend a parent traversal and find the final data slot.
* Descend a traversal state into a selected child.
* Copy the selected parent location into the write state.
* Decode encoded nodes and return concrete pivot and slot arrays.
* Read protected slots and initialize encoded replacement slots.
* Copy and finalize raw node arrays when the node-copy option is disabled.
* Obtain a preallocated node from the existing node pool.
* Store parent fields and tree height.
* Publish the complete subtree and release replaced nodes.

These operations depend on encoded-node layout or protected state transitions.
Rust owns sparse-node detection, sibling direction, source selection,
destination sizing, and convergence decisions.

Rebalance algorithm
-------------------

Rust first initializes the simulated copy leaf from the selected write state.
At each level it ascends the parent traversal when the current parent is not
the root.

When the replacement data exceeds one node, Rust checks the complete left
sibling before the right sibling. The selected sibling must leave one slot
free unless the two nodes have no remaining capacity.

When the replacement data fits below the minimum node occupancy, Rust selects
one sibling. It prefers the right sibling so data can move left. If no right
sibling exists, it selects the left sibling. A write over the complete tree
range does not select a sibling.

The shared copy engine then preserves this source order:

1. The complete left sibling, when selected.
2. The retained prefix of the current node.
3. The simulated leaf containing the new range.
4. The retained suffix of the current node.
5. The complete right sibling, when selected.

Destination selection and NULL-boundary adjustment use the same Rust code as
the native spanning and split stores.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* The parent traversal starts from the selected leaf.
* A root node has no rebalance sibling.
* A complete-range write cannot select a sibling.
* A right sibling move keeps the current minimum and extends the maximum.
* A left sibling move extends the minimum and starts the parent source early.
* Every node copy is bounded by both the source range and destination size.
* Replacement nodes are private until the final RCU publication.
* A converged one-node result keeps the current encoded parent.
* A full-range result resets the walk state before installing a new root.
* The C fallback remains complete when the option is disabled.

Testing
-------

The ``maple_tree_store`` KUnit suite covers locked and RCU rebalance stores on
range and allocation-range trees. Each case checks the sparse-node result and
the unchanged neighboring ranges. The complete Maple Tree self-test also
exercises rebalance and parent-level convergence paths.

The unit validation gate includes:

* Native Rust and C-fallback focused KUnit runs on one and four CPUs.
* The complete Maple Tree self-test in every focused run.
* Native Rust KASAN runs on one and four CPUs.
* Native Rust lockdep runs on one and four CPUs.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* A C-only build with ``CONFIG_RUST=n``.
* Object inspection for the complete C fallback and the Rust production path.

Validation results
------------------

The unit starts at commit ``3e7aeefe87fd``, which implements native split
stores in Rust. All native Rust, C-fallback, KASAN, and lockdep configurations
passed 19 of 19 focused KUnit tests on one and four CPUs.

The rebalance cases cover the right-sibling path and the left-sibling path.
Each path runs on a range tree and an allocation-range tree in locked and RCU
modes.

Native Rust, C fallback, and KASAN passed 5,357,658 complete Maple Tree
self-tests on one CPU and 5,357,709 self-tests on four CPUs. Lockdep passed
3,809,321 self-tests on both CPU configurations. KASAN and lockdep emitted no
report.

The KASAN build used
``KCFLAGS=-Wno-error=frame-larger-than`` for existing Clang frame-size warnings
in ``fs/coredump.c``, ``net/ethtool/cmis_fw_update.c``,
``drivers/block/loop.c``, and the existing spanning-store test. The changed
Rust code emitted no warning.

The Rust source passed Clippy, Rustdoc, and ``rustfmtcheck``.
``git diff --check`` and strict checkpatch passed. The focused Rust Sphinx
build generated this document. Sphinx reported only the existing warnings in
``Documentation/rust/index.rst`` and ``Documentation/rust/quick-start.rst``.

The ``CONFIG_RUST=n`` build compiled the Maple Tree object and the store KUnit
object. The Maple Tree object contains the complete C rebalance path. The
object has no Rust Maple Tree symbol references.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded-node or parent layout changes.
* Node-pool ownership or replacement-node allocation rules change.
* RCU publication requires a different completion or cleanup sequence.
* Parent or sibling traversal no longer has bounded behavior.
* Gap metadata no longer summarizes a completed destination node.
* The copied source order or rebalance policy changes.
* A C helper starts to implement sparse-node detection, sibling direction,
  source selection, NULL adjustment, or convergence policy.
