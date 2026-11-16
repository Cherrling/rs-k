.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree split stores
===================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_SPLIT_STORE`` option selects a native Rust
production implementation of ``mas_wr_split()``. The option depends on the
native Rust spanning-store unit because both strategies use the same copy
engine.

Rust owns these operations:

* Initialize the simulated leaf from the selected write state.
* Calculate the one-indexed replacement size.
* Select an adjacent sibling when it can absorb the replacement data.
* Preserve the left, right, and sibling source order.
* Select one, two, or three destination nodes.
* Adjust destinations that end in a NULL slot.
* Use the native leaf-gap calculation and allocation-range metadata for
  completed destination nodes.
* Ascend until the replacement converges or becomes a new root.
* Publish the completed replacement subtree.

The Rust write dispatcher selects ``wr_split_store`` directly. Rust does not
call the C implementation of the complete split-store strategy.

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
Rust owns the split policy, sibling selection, source selection, destination
sizing, and convergence decisions.

Split algorithm
---------------

Rust first initializes the simulated copy leaf from the selected write state.
At each level it ascends the parent traversal when the current parent is not
the root.

When the replacement data exceeds one node, Rust checks the complete left
sibling before the right sibling. The selected sibling must leave one slot
free unless the two nodes have no remaining capacity. A sibling is used only
when its complete data contributes to the replacement.

The shared copy engine then preserves this source order:

1. The complete left sibling, when selected.
2. The retained prefix of the split node.
3. The simulated leaf containing the new range.
4. The retained suffix of the split node.
5. The complete right sibling, when selected.

Destination selection and NULL-boundary adjustment use the same Rust code as
the native spanning store.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* The parent traversal starts from the selected leaf.
* A root node has no split sibling.
* A left sibling shifts the parent source boundary to the left.
* A right sibling extends the parent source boundary to the right.
* Every node copy is bounded by both the source range and destination size.
* Replacement nodes are private until the final RCU publication.
* A converged one-node result keeps the current encoded parent.
* A full-range result resets the walk state before installing a new root.
* The C fallback remains complete when the option is disabled.

Testing
-------

The ``maple_tree_store`` KUnit suite covers locked and RCU split stores on
range and allocation-range trees. Each case checks the split result and the
unchanged neighboring ranges. The complete Maple Tree self-test also exercises
split and parent-level split paths.

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

The unit starts at commit ``f94b23d51aee``, which implements native root
stores in Rust. All native Rust, C-fallback, KASAN, and lockdep configurations
passed 17 of 17 focused KUnit tests on one and four CPUs.

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
object. The Maple Tree object contains the complete C split path inside
``mas_wr_store_entry()`` and its helper calls. The object has no Rust Maple
Tree symbol references.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded-node or parent layout changes.
* Node-pool ownership or replacement-node allocation rules change.
* RCU publication requires a different completion or cleanup sequence.
* Parent or sibling traversal no longer has bounded behavior.
* Gap metadata no longer summarizes a completed destination node.
* The copied source order or split policy changes.
* A C helper starts to implement sibling selection, destination sizing,
  source selection, NULL adjustment, or convergence policy.
