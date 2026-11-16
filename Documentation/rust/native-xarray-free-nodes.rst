.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray node reclamation
===================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_FREE_NODES`` option selects an experimental native
Rust implementation of subtree reclamation for the Rust XArray store path.
The option depends on the native Rust store path and currently supports
x86_64.

Rust owns the production implementation of these operation groups:

* Traversal of every node and slot in a detached subtree.
* Descent into child nodes before their parent is reclaimed.
* Retry-marker publication before a readable entry is retired.
* Accounting reset and update notification for each reclaimed node.
* The order in which child and parent nodes enter the RCU reclamation path.

The C store fallback retains its existing reclamation algorithm.

Entry points and ownership
--------------------------

With ``CONFIG_RUST_XARRAY_FREE_NODES`` enabled, ``xarray_store.rs`` calls the
Rust implementation directly. The C definition of
``rust_xarray_free_nodes()`` is compiled only for the C store fallback.

The production implementation is in
``rust/kernel/xarray_free_nodes.rs``. It calls narrow C adapters for slot
access, retry-marker publication, parent lookup, update notification, and RCU
reclamation.

Retained C boundaries
---------------------

The XArray node and slot layout remain C. C adapters provide the locked slot
read and the parent pointer lookup.

RCU slot initialization remains C. The helper uses ``RCU_INIT_POINTER()`` to
publish ``XA_RETRY_ENTRY`` so existing RCU readers restart instead of walking
reclaimed memory.

Node update callbacks remain C. The helper invokes ``xas_update()`` after Rust
resets the node accounting fields.

RCU reclamation remains C. The helper invokes ``xa_node_free()`` after Rust
has processed every descendant and initialized a retry marker for every
visible entry.

The complete C ``xas_free_nodes()`` implementation remains for ``xa_destroy()``
and for builds that disable this native Rust unit.

Important invariants
--------------------

The caller must remove ``top`` from its parent slot before entering the Rust
implementation. The caller must hold the XArray lock and keep every reachable
node live until Rust schedules it for reclamation.

Rust descends into a child node when the node shift is non-zero and the current
slot contains a node entry. Other non-NULL slots receive a retry marker before
their containing node can be retired.

Rust resets ``count`` and ``nr_values``, invokes the update callback, and then
schedules the node for RCU reclamation. It processes all descendants before
the parent reaches these steps.

Rust stops after reclaiming ``top``. The parent pointer is only followed after
the current node has completed reclamation.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Focused root-slot and nested-subtree reclamation KUnit tests on SMP1 and in
  three repeated SMP4 runs.
* A C-fallback build with the same four runs.
* The complete upstream XArray regression suite in every run.
* A Rust-enabled KASAN build with SMP1 and three repeated SMP4 runs.
* Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict
  checkpatch, and ``git diff --check``.
* Symbol checks proving that the final Rust build defines
  ``rust_xarray_free_nodes()`` in the Rust object.
* Symbol checks proving that the fallback defines the function in
  ``lib/xarray.o`` and contains no native Rust reclamation fixtures.
* Disassembly checks showing that ``rust_xarray_store()`` enters the Rust
  reclamation implementation.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. Every run reported:

.. code-block:: text

   xarray_free_nodes: pass:2 fail:0 skip:0 total:2
   XArray: 158825682 of 158825682 tests passed

The focused tests replace a range spanning multiple root child slots and a
range containing a deeper nested subtree. They also verify mark union,
loads from inside the reclaimed range, and values outside the range.

No validation log contained a BUG, Oops, KASAN report, warning, call trace,
kernel panic, or RCU stall.

Symbol inspection showed that the Rust object defines
``rust_xarray_free_nodes()`` while the fallback object defines the C adapter.
The fallback object contained no native Rust reclamation fixture.

Disassembly showed two calls from ``rust_xarray_store()`` to the Rust subtree
reclamation implementation. The Rust implementation calls the retained C
helpers for locked slot reads, retry publication, parent lookup, update
notification, and RCU reclamation.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, slot, parent, or tagged-entry layout changes.
* Retry-marker or RCU reader-restart semantics change.
* The XArray lock or update callback protocol changes.
* ``xa_destroy()`` starts using a separate reclamation path.
* A C helper starts to select traversal or reclamation order.
* The Rust store path stops entering the Rust reclamation implementation.
* A supported target needs different parent or slot primitives.
