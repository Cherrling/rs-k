.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree node staging
===================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_NODE_ALLOC`` option selects the native Rust
production implementation of staged-node allocation for Maple Tree writes.

Rust owns these operations:

* Select the single-node allocation fast path.
* Reuse a staged single node without another allocation.
* Replace a staged single node with sheaf storage when a write needs more
  nodes.
* Refill an existing sheaf when its capacity is too small.
* Allocate a new sheaf for the requested node count.
* Install ``-ENOMEM`` when the selected allocation operation fails.
* Select and clear one completed node for a write.
* Return unused sheaf and single-node storage to the node pool.

The ordinary, preallocated, split, rebalance, spanning, and root-replacement
write paths use this lifecycle when the option is enabled. Native Rust stores
also request replacement nodes through this implementation.

Retained C boundary
-------------------

The C boundary contains these operations:

* Allocate one object from the Maple node slab cache.
* Free one unused staged single node.
* Allocate, refill, size, pop, and return slab sheaves.
* Clear a completed Maple node.
* Install the encoded allocation error in a Maple state.
* Enforce the tree-lock and RCU rules for staged and published nodes.

These operations remain in C because they call kernel slab internals, depend
on the concrete ``struct maple_node`` layout, or encode Maple node metadata.
Rust owns the state-dependent allocation policy and error transitions.

Allocation policy
-----------------

A zero request performs no allocation. A one-node request first uses a sheaf
when one is already staged, then reuses a staged single node, and finally
allocates one cache object.

A multi-node request releases a staged single node. Rust reuses an existing
sheaf when it has enough capacity. Otherwise, Rust refills the sheaf. With no
staged sheaf, Rust obtains a new sheaf for the complete request.

The selected node is removed from the staged state before C clears its storage.
Cleanup resets the request count, returns the remaining sheaf, and frees an
unused single node. Rust preserves the original allocation order and the
original error transition.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* A successful allocation clears the request count.
* A failed allocation retains the encoded ``-ENOMEM`` state.
* A multi-node request does not retain both a single node and a sheaf.
* A reused sheaf must have at least the requested capacity.
* Cleanup clears every staged pointer that it releases.
* Rust clears a node before a write initializes it.
* Staged nodes do not escape the tree-write lock or RCU lifetime rules.

Testing
-------

The Maple Tree store KUnit suite covers exact-fit stores, replacement-node
stores in locked and RCU modes, root expansion, spanning, split, rebalance,
allocation-range writes, and concurrent RCU writes. These cases exercise the
single-node and sheaf paths and verify that staged storage is released.

The complete Maple Tree self-test exercises allocation, deletion, large trees,
node replacement, and tree teardown through the production paths.

The unit validation gate includes:

* Native Rust and C-fallback focused KUnit runs on one and four CPUs.
* The complete Maple Tree self-test in every focused run.
* Native Rust KASAN runs on one and four CPUs.
* Native Rust lockdep runs on one and four CPUs.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* A C-only build with ``CONFIG_RUST=n``.
* Object inspection for the production Rust and C fallback paths.

Validation results
------------------

Validation on 2026-11-17 used parent commit
``9448831865dfc6519982e8d846d4577861481a39``. The native Rust and C-fallback
runs passed all 56 focused KUnit cases on one and four CPUs. Each run passed
3,809,321 complete Maple Tree checks.

The native Rust KASAN runs passed all 56 focused KUnit cases. They passed
5,357,658 complete checks on one CPU and 5,357,709 checks on four CPUs. The
KASAN logs contained no KASAN report.

The native Rust lockdep runs passed all 56 focused KUnit cases and 3,809,321
complete checks on one and four CPUs. The lockdep logs contained no lock-order
warning or call trace. Lockdep used ``CONFIG_DEBUG_MAPLE_TREE=n``.

The KASAN image used ``KCFLAGS=-Wno-error=frame-larger-than``. This option
downgraded two existing frame-size warnings in ``fs/coredump.c`` and
``lib/tests/maple_tree_store_kunit.c``. Other warnings remained fatal.

Rustfmtcheck, Clippy, Rustdoc, strict checkpatch, and ``git diff --check``
passed. A focused Rust Sphinx build emitted only existing cross-reference
warnings in ``Documentation/rust/index.rst`` and
``Documentation/rust/quick-start.rst``.

A full ``CONFIG_RUST=n`` image built successfully. Its Maple Tree KUnit run
passed all 51 applicable cases on four CPUs and all 3,809,321 complete checks.
The C object contains no staged-node Rust reference.

In the native configuration, ``lib/maple_tree.o`` references
``rust_maple_tree_stage_alloc_nodes``,
``rust_maple_tree_stage_empty_nodes``, and
``rust_maple_tree_stage_pop_node``. ``rust/kernel.o`` defines all three
symbols. The C-fallback Maple Tree object references none of these symbols.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Maple node slab or sheaf interfaces change.
* ``struct ma_state`` staging fields change ownership rules.
* ``struct maple_node`` layout or zeroing requirements change.
* The allocation error encoding changes.
* Tree-lock or RCU protection rules for staged nodes change.
* A C helper starts to implement a staged-allocation policy decision.
