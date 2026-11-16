.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree preallocation
====================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_PREALLOC`` option selects a native Rust
production implementation of ``mas_preallocate()``. The option supports
x86_64 and depends on the native Rust Maple Tree forward find unit.

Rust owns the complete preallocation operation:

* Normalization of active, start, paused, empty, root, overflow, underflow,
  and partial write states.

* Preservation or reset of the entry replaced by the planned write.

* Selection of the write strategy for root, exact-fit, append, slot, node,
  spanning, split, and rebalance operations.

* Calculation of the required node count from tree height, vacant depth,
  sufficient depth, RCU mode, entry encoding, and selected strategy.

* Preallocation lifecycle, including the preallocated-state flag.

* Cleanup and state reset after allocation failure.

The existing C implementation remains available when the option is disabled.

Entry point and ownership
-------------------------

The public ``mas_preallocate()`` interface and its ``EXPORT_SYMBOL_GPL`` class
do not change. With this option enabled, the C definition is an ABI adapter
that calls ``rust_mas_preallocate()``.

The Rust production implementation is in
``rust/kernel/maple_tree_prealloc.rs``. Rust calls narrow C primitives for the
locked write walk, pivot-end selection, NULL extension, new-end calculation,
node sizing, and allocation.

Retained C boundaries
---------------------

Maple node encoding and layout remain C. Rust calls helpers for root
classification, minimum slot counts, slot counts, node type extraction, and
RCU mode.

The locked write traversal remains C. The traversal selects the leaf slot and
records the current range, content, and ancestor capacity. Rust consumes that
metadata to select the write strategy.

The node pool remains C. ``mas_alloc_nodes()`` selects a single node or a
sheaf, refills an existing sheaf, or records ``-ENOMEM``. Rust owns when the
primitive runs and the resulting lifecycle.

Cleanup remains C. ``mas_destroy()`` returns allocated sheaves and nodes to
the Maple Tree node pool.

RCU publication and lockdep checks remain C. This unit prepares a later store
but does not publish a replacement node.

Important invariants
--------------------

Preallocation must preserve the selected range and the content of the planned
write. A NULL write can extend across adjacent NULL slots, while a non-NULL
write preserves a partial walk.

An exact-fit, append, or slot write needs no replacement nodes. A node store
needs a replacement node only in RCU mode. A root expansion, split, rebalance,
or spanning store needs enough nodes to replace every affected level.

The request uses the lowest node with free space and the lowest node that
cannot absorb a replacement. The larger requirement applies when a spanning
or rebalance operation crosses those levels.

A successful operation sets ``MA_STATE_PREALLOC`` even when no allocation was
required. A later ``mas_store_prealloc()`` consumes the prepared state.

Allocation failure clears any partial request, releases allocated nodes,
resets the walk state, and returns the encoded errno.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for root expansion, range stores, spanning writes,
  splits, slot stores, RCU node replacement, preallocation chaining, and
  concurrent allocation and RCU reads.

* The same runs with the Rust preallocation implementation disabled.

* The same focused runs with KASAN enabled.

* The complete upstream Maple Tree regression suite in every run.

* Focused lockdep runs with one and four virtual CPUs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and all native Maple Tree options disabled.

* Symbol and disassembly checks for the Rust adapter and C fallback.

Validation results
------------------

The final revision passed the validation matrix on 2026-11-17.

The Rust images passed one-CPU and four-CPU runs. Each run passed four
focused KUnit cases. The Maple Tree self-test passed 5,357,658 checks on one
CPU and 5,357,709 checks on four CPUs.

The fallback images, with ``CONFIG_RUST_MAPLE_TREE_PREALLOC`` disabled,
passed the same focused cases and the same self-test counts.

The KASAN images passed one-CPU and four-CPU runs with the same focused cases
and self-test counts. No KASAN report occurred.

The lockdep images enabled RCU lockdep checking and passed one-CPU and
four-CPU runs. Both runs passed four focused cases and 3,809,321 Maple Tree
self-test checks. No lockdep warning or call trace occurred.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation reported two existing
cross-reference warnings outside this document.

The C-only build used ``CONFIG_RUST=n``. It compiled
``lib/maple_tree.o`` and ``lib/tests/maple_tree_prealloc_kunit.o``.

The Rust image contains ``rust_mas_preallocate`` and the narrow C helper
symbols. The ``mas_preallocate`` adapter tail-jumps to Rust.

The fallback object contains ``mas_preallocate`` and no Rust preallocation
symbols. It retains the complete C preallocation operation.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple node encoding, node capacity, root representation, or node layout
  changes.

* The meaning of vacant height, sufficient height, or write traversal metadata
  changes.

* The node pool, sheaf allocation rules, or allocation failure encoding
  changes.

* RCU replacement rules change which write strategies require fresh nodes.

* A C helper starts to select a write strategy or calculate the node request.

* The C adapter stops forwarding directly to Rust.
