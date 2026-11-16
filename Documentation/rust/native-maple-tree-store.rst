.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree write dispatch
=====================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_STORE_ENTRY`` option selects a native Rust
production implementation of the Maple Tree write dispatcher. The option
depends on the native Rust preallocation unit and supports x86_64.

Rust owns the complete switch over ``mas->store_type``. The exact-fit, append,
and slot-store strategies are native Rust implementations. Rust directly
updates the selected slots and pivots for these strategies.

Rust also decides when to run metadata, gap, trace, warning, and complex
strategy primitives. C keeps the primitives that require Maple Tree layout,
RCU annotations, tracepoint context, or the remaining complex algorithms.

The existing dispatcher and all strategies remain available when the option is
disabled.

Entry point and ownership
-------------------------

The internal ``mas_wr_store_entry()`` call sites do not change. With this
option enabled, the C definition is a small adapter that calls
``rust_mas_wr_store_entry()``.

The Rust production implementation is in
``rust/kernel/maple_tree_store.rs``. No C helper selects exact-fit, append, or
slot-store in place of Rust.

In-place strategies
-------------------

An exact-fit write replaces one slot. It updates the gap only when the new
entry and the replaced entry differ in NULL status.

An append write reuses the end of a node. Rust reserves the metadata pivot
before it changes the data end, selects the correct append boundary, publishes
slots in the original order, updates affected gaps, sets the new end, and emits
the write tracepoint.

Append cannot run in RCU mode. The write selector enforces this condition.

A slot-store write replaces or expands a range without changing the node size.
Rust handles both the one-boundary and three-slot forms. It reads replaced
slots under the write lock, preserves the pivot update order, maintains
``mas->offset``, and emits the tracepoint before gap propagation.

The three-slot form retains the one-time RCU warning. RCU mode normally selects
the one-boundary form or a node replacement.

Retained C boundaries
---------------------

Maple Tree node layout remains C. Rust calls helpers for protected slot reads,
RCU slot publication, metadata layout, gap propagation, and the write
tracepoint.

The node-store and spanning-store strategies have separate native Rust
migration units. When those options are enabled, Rust owns the selected store
strategy. The split-store, rebalance, new-root, store-root, and invalid
strategies remain C. Rust selects these strategies and calls one C helper for
only the selected complex strategy. The C helper does not contain exact-fit,
append, or slot-store policy.

The complex strategies remain retained because they cross nodes, replace
subtrees, change tree height, or coordinate multiple allocation and
publication stages. Migrating one strategy requires independent ownership of
those related operations.

Important invariants
--------------------

Slot publication must use the Maple Tree RCU annotation helper. A raw Rust
store to a slot is not sufficient publication.

Pivot writes and metadata writes must remain in the same order as the C
implementation. Readers can observe slot publication before gap propagation.

The exact-fit path updates a gap only on a NULL status change. The append path
updates a gap when either the new entry or the replaced range is NULL. The
slot-store path updates a gap when the new entry is NULL or a replaced slot was
NULL.

Append is an in-place operation and must not execute in RCU mode. A slot-store
operation must retain the RCU warning in the three-slot form.

An unknown store type must reach the retained complex helper and trigger the
impossible-state diagnostic. Rust must not silently ignore it.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for exact-fit NULL transitions, append metadata,
  one-boundary slot stores, three-slot slot stores, gap reuse, and concurrent
  RCU exact-fit writes.

* The same focused cases with the Rust write dispatcher disabled.

* The same focused cases with KASAN enabled, with no KASAN report.

* Focused lockdep runs with one and four virtual CPUs, with no lockdep warning
  or call trace.

* The complete upstream Maple Tree regression suite in every Rust, fallback,
  and KASAN run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and all native Maple Tree options disabled.

* Object and disassembly checks. Rust call sites must reference
  ``rust_mas_wr_store_entry()``. The fallback object must contain the complete C
  dispatcher and no Rust write-dispatch symbols.

Validation results
------------------

The final revision passed the validation matrix on 2026-11-17.

The Rust images passed one-CPU and four-CPU runs. Each run passed six focused
KUnit cases. The Maple Tree self-test passed 5,357,656 checks on one CPU and
5,357,707 checks on four CPUs.

The fallback images, with ``CONFIG_RUST_MAPLE_TREE_STORE_ENTRY`` disabled,
passed the same focused cases and the same self-test counts.

The KASAN images passed one-CPU and four-CPU runs with the same focused cases
and self-test counts. No KASAN report occurred.

The lockdep images enabled RCU lockdep checking and passed one-CPU and
four-CPU runs. Both runs passed six focused cases and 3,809,321 Maple Tree
self-test checks. No lockdep warning or call trace occurred.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and ``git diff --check``
passed. Focused Sphinx generation reported existing cross-reference warnings
outside this document.

Strict checkpatch reported no errors, warnings, or checks with two documented
suppressions. The ``AVOID_BUG`` suppression preserves the existing
``MT_BUG_ON`` diagnostic for an impossible state. The ``FILE_PATH_CHANGES``
suppression covers the added documentation file, which is present in
``MAINTAINERS``.

The C-only build used ``CONFIG_RUST=n``. It compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_store_kunit.o``.

The Rust C object references ``rust_mas_wr_store_entry()`` from every recursive
write call path. The Rust object exports ``rust_mas_wr_store_entry()``. The
disassembly shows direct calls at the insert, store, allocation, preallocated
store, and erase paths.

The fallback and C-only objects contain the complete C ``mas_wr_store_entry()``
and no Rust write-dispatch symbols.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple node layout, pivot layout, metadata layout, or root representation
  changes.

* RCU publication rules or the allowed store strategies in RCU mode change.

* Gap propagation or the NULL status rules change.

* The write tracepoint changes its position or arguments.

* A C helper starts to implement exact-fit, append, or slot-store policy.

* The C adapter stops forwarding directly to Rust.
