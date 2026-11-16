.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray node deletion and root shrinking
===================================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_DELETE_NODE`` option selects an experimental native
Rust implementation of empty-node deletion and root shrinking for the native
Rust XArray store path. The option depends on the Rust subtree reclamation path
and currently supports x86_64.

Rust owns the production implementation of these operation groups:

* The loop that removes consecutive empty nodes.
* Parent accounting propagation after removing a child.
* The decision to clear the root when the final node becomes empty.
* The repeated promotion loop that shrinks a single-child root.
* The decision to preserve or clear the root free mark during shrinking.
* The order of root publication, update notification, and node reclamation.

The C store and deletion fallback retain their existing algorithms.

Entry points and ownership
--------------------------

With ``CONFIG_RUST_XARRAY_DELETE_NODE`` enabled, ``xarray_store.rs`` applies
the node count and value deltas itself. It enters
``xarray_delete_node::delete_node()`` when the count delta is negative.

The production implementation is in
``rust/kernel/xarray_delete_node.rs``. The compiler can inline the deletion and
shrinking loops into ``rust_xarray_store()``. The resulting object still calls
the narrow C helpers for locked access and destructive primitives.

The C definition of ``rust_xarray_update_node()`` is compiled only when this
unit is disabled. That adapter retains the C accounting, deletion, and shrinking
policy for the fallback.

Retained C boundaries
---------------------

The ``xa_node`` and ``xa_state`` layouts remain C. C helpers provide locked slot
reads and publication, protected parent lookup, accounting validation, root
bounds, and parent clearing.

Mark bitmap and root flag storage remain C. C helpers provide the free-mark bit
read, root mark clear, and track-free and zero-busy flag reads.

RCU publication remains C. Rust selects when to clear or replace the root and
when to clear a parent slot. C helpers perform the corresponding
``rcu_assign_pointer()`` operations.

Node update callbacks remain C. Rust owns the order and calls the retained
``xas_update()`` adapter after changing visible accounting.

RCU reclamation remains C. Rust detaches nodes, then calls the retained
``xa_node_free()`` adapter. The original C deletion functions remain available
for the C store fallback.

Important invariants
--------------------

The caller must hold the XArray lock. The deletion entry point receives a live
cursor whose current node count changed by a negative delta.

Rust removes a node only when its count is zero. For each removed child, Rust
clears the parent slot, decrements the parent count, validates accounting, and
notifies the update callback before examining the parent.

If an empty node has no parent, Rust publishes a null root and sets the cursor
to ``XAS_BOUNDS``. Otherwise, the deletion loop continues through consecutive
empty parents.

Rust starts root shrinking only when the surviving node has no parent. It
promotes the root only when the count is one and slot zero contains a usable
entry. A zero entry becomes null only when the XArray has zero-busy semantics.

For each promoted root, Rust publishes the child, handles the free mark, resets
node accounting, sets a retry marker when required, notifies the callback,
reclaims the old root, and clears the promoted child's parent pointer.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Dedicated KUnit coverage for empty-node removal, parent chains, root clear,
  consecutive shrinking, zero-busy entries, and free-mark handling.
* The same four runs with this Rust unit disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict checkpatch,
  and ``git diff --check``.
* Symbol and disassembly checks proving that the production store path enters
  Rust deletion and shrinking rather than the C fallback adapter.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each passed
one SMP1 run and three repeated SMP4 runs. Every run reported:

.. code-block:: text

   xarray_delete_node: pass:6 fail:0 skip:0 total:6
   XArray: 158825682 of 158825682 tests passed

The focused tests cover:

* Root shrinking with one surviving direct entry.
* Parent-chain deletion while unrelated entries remain.
* Clearing a deep root.
* Repeated promotion of a root through multiple node levels.
* Conversion of a zero-busy root entry to a null root while index zero remains
  unavailable to the allocator.
* Clearing the root free mark when the promoted source slot is not free.

No accepted validation log contained a BUG, Oops, KASAN report, non-test
warning, call trace, kernel panic, or RCU stall.

Symbol inspection showed that the Rust object defines ``rust_xarray_store()``
and contains the deletion and shrinking control flow. The production store path
does not call ``rust_xarray_update_node()``. The fallback store path calls that
adapter, which is defined by the C object.

Disassembly showed that the Rust ``rust_xarray_store()`` path directly contains
the deletion and repeated shrinking control flow. It calls retained C helpers
for parent lookup, root and slot publication, zero-busy and free-mark handling,
accounting validation, update notification, and RCU reclamation.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node or state layout changes.
* Root, parent, or slot publication no longer requires the retained locking or
  RCU protocol.
* The zero-busy or free-mark representation changes.
* The node update callback or node reclamation lifecycle changes.
* The C fallback starts using a different deletion or shrinking policy.
* The Rust store path stops entering ``xarray_delete_node::delete_node()``.
* A supported target needs different parent, slot, or root primitives.
