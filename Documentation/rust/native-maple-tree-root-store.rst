.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree root stores
==================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_ROOT_STORE`` option selects native Rust
production implementations for the Maple Tree ``wr_store_root`` and
``wr_new_root`` strategies. The option depends on the native Rust spanning,
node-store, and write-dispatch units.

Rust owns these operations:

* Assign a direct pointer to an empty tree root.
* Clear an empty or direct pointer root.
* Expand an empty or direct pointer root into a leaf node.
* Preserve the previous direct root entry during root expansion.
* Select the ``maple_leaf_64`` layout.
* Initialize root slots, pivots, metadata, state, and leaf height.
* Replace the complete tree with a new leaf root.
* Preserve and release the previous root subtree.

The Rust write dispatcher selects both root strategies directly. Rust does not
call the C implementations of ``mas_store_root()`` or ``mas_new_root()``.

Retained C boundary
-------------------

The C boundary contains these operations:

* Read and publish the protected ``ma_root`` pointer.
* Mark an encoded state node as the root.
* Set the tree parent and height fields.
* Return the concrete pivot and slot arrays.
* Allocate a node from the existing preallocation pool.
* Set leaf metadata and publish protected slots.
* Destroy the detached previous root subtree.

These helpers do not choose a root strategy, select slots, calculate pivots,
or decide when root expansion is required.

Root strategies
---------------

A ``wr_store_root`` operation starts with an empty or direct pointer root.
For a direct pointer root, Rust publishes the new direct entry. For a non-NULL
range that needs boundaries, an encoded internal entry, or any range with a
non-zero endpoint, Rust expands the tree to one leaf level.

Expansion starts with slot zero. A non-zero index preserves an existing direct
entry, adds the preceding pivot, and places the new entry after the leading
gap. A non-maximum final index adds the maximum pivot. Rust records the final
slot in leaf metadata before publishing the encoded root.

A ``wr_new_root`` operation starts with an active tree and replaces the
complete range. A NULL entry publishes an empty root and height zero. A
non-NULL entry allocates one leaf node and publishes a complete full-range
slot. Rust destroys the previous root subtree only after the replacement root
is visible.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* The tree write lock protects every root and node field access.
* A direct root entry is published with the Maple Tree RCU root helper.
* An encoded root is marked as a root only after its node is complete.
* Pivot boundaries remain inclusive.
* A non-maximum root range ends with an ``ULONG_MAX`` pivot.
* Root metadata records the final populated slot.
* The previous subtree is detached before destruction.
* A cleared root leaves the state behavior unchanged from the C implementation.
* The complete C root strategies remain available when the option is disabled.

Testing
-------

The ``maple_tree_store`` KUnit suite includes focused cases for direct root
assignment, direct root clearing, root expansion, and complete-tree root
replacement in locked and RCU modes.

The unit validation gate includes:

* Native Rust and C-fallback focused KUnit runs on one and four CPUs.
* The complete Maple Tree self-test in every focused run.
* Native Rust KASAN runs on one and four CPUs.
* Native Rust lockdep runs on one and four CPUs.
* Rustfmtcheck, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch,
  and ``git diff --check``.
* A C-only build with ``CONFIG_RUST=n``.
* Object inspection for the complete C fallback and the Rust production path.

Validation results
------------------

All eight focused KUnit runs passed 15 of 15 tests. Native Rust, C fallback,
and KASAN passed 5,357,658 Maple Tree self-tests on one CPU and 5,357,709
self-tests on four CPUs. Lockdep passed 3,809,321 self-tests on both CPU
configurations. KASAN and lockdep emitted no report.

Clippy, Rustdoc, rustfmtcheck, focused Rust Sphinx generation,
``git diff --check``, and strict checkpatch passed. Sphinx reported only the
existing warnings in ``Documentation/rust/index.rst`` and
``Documentation/rust/quick-start.rst``. Checkpatch reported zero errors,
zero warnings, and zero checks after ignoring its new-file path warning.
``MAINTAINERS`` already lists the new document.

The ``CONFIG_RUST=n`` build compiled ``lib/maple_tree.o`` and the KUnit object.
The C object contains the emitted ``mas_root_expand()``, ``mas_new_root()``,
and ``mas_wr_store_entry()`` symbols. ``mas_store_root()`` is inlined into the
write dispatcher. The C object has no Rust Maple Tree references.

For the root-store boundary, the root-enabled C object contains only the
retained root helpers. Rust owns ``root_store_expand()`` and
``root_store_new_root()``, and exports ``rust_mas_wr_store_entry()``.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded root marking or parent layout changes.
* The tree-height representation changes.
* Node-pool ownership or preallocation rules change.
* RCU root publication requires a different completion sequence.
* Leaf metadata or protected slot publication changes.
* Previous-root destruction no longer occurs after publication.
* A C helper starts to choose root strategy, slots, pivots, or expansion
  conditions.
