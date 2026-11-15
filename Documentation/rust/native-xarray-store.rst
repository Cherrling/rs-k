.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray store migration
==================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_STORE`` option selects an experimental native Rust
implementation of XArray store mutation in ``lib/xarray.c``. The option
currently supports x86_64 and depends on the native Rust XArray load path.

Rust owns the production implementation of these operation groups:

* Replacement of the selected slot.
* Expansion of a store across a multi-index sibling range.
* Node count and value accounting for every changed slot.
* Control flow for freeing nodes removed by the store.
* Control flow for creating nodes needed by the store.

The existing C algorithm remains available when the option is disabled.

Entry points and ownership
--------------------------

The public ``xas_store()`` interface and its ``EXPORT_SYMBOL_GPL`` export class
do not change. With ``CONFIG_RUST_XARRAY_STORE`` enabled, the definition in
``lib/xarray.c`` forwards directly to ``rust_xarray_store()``.

The Rust production implementation is in ``rust/kernel/xarray_store.rs``. It
calls narrow C adapters for operations that depend on the XArray node layout,
locking protocol, RCU publication rules, or mark storage.

Retained C boundaries
---------------------

Node creation and allocation remain C. ``xas_create()`` expands the tree,
allocates nodes, records parent links, and leaves the cursor at the selected
slot.

Node release remains C. ``xas_free_nodes()`` replaces reachable entries with
retry markers and schedules RCU reclamation.

Node accounting propagation remains C. ``update_node()`` applies the Rust
count and value deltas, invokes the update callback, and removes empty nodes.

Mark storage remains C. ``xas_squash_marks()`` handles multi-index mark
removal, and ``xas_init_marks()`` clears marks before a NULL entry is visible.

RCU slot publication remains C. The assignment helper uses
``rcu_assign_pointer()`` for both a node slot and the array root.

The XArray node layout and tagged entry encoding remain C. Rust calls the
existing helpers for value, node, and sibling classification, sibling
construction, locked slot reads, and node decoding.

Important invariants
--------------------

The caller must hold the XArray lock. The lock serializes slot replacement,
node accounting, and publication.

The implementation classifies the requested entry before replacing later
slots with sibling entries. The value delta for every changed slot uses this
original classification.

The first changed slot receives the requested entry. Later slots in a
multi-index range receive sibling entries that point back to the first slot.

The count delta changes only when a slot changes between NULL and non-NULL.
The value delta changes only when a slot changes between a value entry and a
non-value entry. Siblings of a value entry count as values.

Before publishing a NULL entry, the implementation clears the marks for the
range. The release barrier in ``rcu_assign_pointer()`` makes the mark clearing
visible before a lockless reader can observe the NULL slot.

A replaced node entry is detached before its subtree is scheduled for RCU
reclamation. An RCU reader that still holds the old node receives retry
entries and restarts its traversal.

Storing a single entry inside an existing multi-index entry replaces the
entry for every index in that multi-index range. This is existing XArray
semantics and the multi-index KUnit case checks it explicitly.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* All six focused XArray store KUnit cases with repeated SMP4 execution.
* A Rust-disabled C fallback with the same KUnit cases.
* The complete upstream runtime XArray regression suite for Rust and the C
  fallback.
* A Rust-enabled x86_64 KASAN build with SMP1 and repeated SMP4 execution.
* Rustfmt, Clippy, Rust documentation, strict checkpatch, and
  ``git diff --check``.
* An export-class check for ``xas_store()``.
* Disassembly checks showing the C adapter enters Rust directly.
* Symbol checks showing that the C fallback contains no new Rust store path.

Validation record
-----------------

The Rust-enabled image passed all six focused KUnit cases on one virtual CPU
and in three repeated four-CPU runs. The Rust-disabled C fallback and the
Rust-enabled KASAN image passed the same four runs.

Each Rust, C fallback, and KASAN run also passed the complete upstream runtime
suite. Every run reported ``158825682 of 158825682`` XArray checks passed.

The focused tests cover deep store, replace, and erase operations, reserved
zero entries, compare-and-exchange and insert behavior, mark policy,
multi-index replacement, and concurrent store and load operations.

The concurrent test uses one writer and four readers. The writer alternates
store and erase operations across shallow and deep indexes. This exercises
node reconstruction and RCU reclamation while readers traverse the same
array.

No validation log contained a BUG, Oops, KASAN report, warning, call trace,
kernel panic, or RCU stall. The KASAN build disabled errors for existing
frame-size warnings only. KASAN instrumentation remained enabled.

Object-level disassembly showed a direct jump from ``xas_store()`` to
``rust_xarray_store()``. The Rust production symbol owns slot replacement,
sibling expansion, and accounting control flow.

The C fallback image contained no ``rust_xarray_store`` symbol and none of the
new store helper symbols.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node layout or tagged entry encoding changes.
* The XArray lock or RCU publication protocol changes.
* RCU node-lifetime or retry-entry rules change.
* A C helper starts to select store policy instead of providing a primitive
  operation.
* The C adapter stops forwarding directly to Rust.
* A supported target needs a different root or sibling representation.
* Performance data shows that a retained primitive needs a different
  implementation.
