.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray node-chain creation
======================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_CREATE`` option selects an experimental native Rust
implementation of XArray node-chain creation and cursor positioning. The
option depends on the Rust root expansion and store paths and currently
supports x86_64.

Rust owns the production implementation of these operation groups:

* Root selection for a top, valid, or error cursor.
* Zero-entry reservation selection for an empty root.
* Root expansion control.
* Creation of every missing node on the path to the target slot.
* Free-mark initialization for a new node.
* Descent through existing nodes.
* Sibling resolution during descent.
* Cursor updates for ``xa_node`` and ``xa_offset``.
* The cursor position used by the subsequent store mutation.

The C fallback retains its existing node-chain creation and cursor
positioning policy.

Entry points and ownership
--------------------------

With ``CONFIG_RUST_XARRAY_CREATE`` enabled, ``xas_create()`` in
``lib/xarray.c`` forwards to ``rust_xarray_create()``. The Rust store path
calls the native Rust creation function directly. The C ``xas_create()`` ABI
adapter remains available to external C callers. When Rust range creation is
enabled, its implementation calls the native Rust creation function directly.

The production implementation is in ``rust/kernel/xarray_create.rs``. It
calls the retained node allocation primitive for each missing level. Rust
selects the node shift, controls the loop, publishes each new child into the
selected parent slot, descends into the child, and leaves the cursor at the
target slot.

Retained C boundaries
---------------------

The XArray node, state, and tagged-entry layouts remain C. Rust reads cursor
and node fields through the generated bindings.

Slab allocation and common node initialization remain C. The retained
``xas_alloc()`` primitive allocates a node, assigns its parent, updates the
parent count, and initializes common layout fields.

RCU publication remains C. Narrow helpers read a locked root or node slot and
assign a root or node slot with the required publication operation. Rust
controls the order of these operations.

Node and root mark storage remain C. Rust calls C primitives to fill a new
node with free marks and to inspect the free-tracking policy.

Locked root access, tagged-entry classification, sibling decoding, retry
markers, and node encoding remain C primitives. Rust owns the traversal and
cursor policy that uses these primitives.

The complete C creation algorithm remains compiled when this option is
disabled.

Important invariants
--------------------

The caller must hold the XArray lock. All slot reads and publications occur
under this lock.

A top cursor or a valid root cursor requires root selection before the
creation loop. An error cursor returns without changing the tree.

An empty root with the zero-busy flag starts expansion from the advanced zero
entry. A later store must preserve this reservation.

An empty root with sufficient capacity can remain a direct root entry. A
request that disallows a root entry forces at least one node.

Each new child is fully initialized and linked to its parent before
publication. The creation loop publishes the parent slot before descent.

Descent sets ``xa_node`` before resolving sibling entries. The resolved
sibling offset becomes ``xa_offset``. A sibling that points to a node from a
non-leaf level is replaced by the retry marker.

The loop stops at the requested order. On success, ``xa_node`` and
``xa_offset`` identify the slot used by the subsequent store mutation. On an
allocation failure, the retained allocation primitive records the error in
the cursor.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Focused KUnit coverage for direct roots, deep chains, forced node creation,
  existing chains, direct ancestors, zero reservations, free marks, error
  cursors, and target-slot positioning.
* The same four runs with this Rust unit disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* Symbol and disassembly checks proving that ``xas_create()`` enters Rust in
  production and the C fallback retains the complete creation algorithm.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. The KASAN build used 3 GiB
of guest memory. Every run reported:

.. code-block:: text

   xarray_create: pass:8 fail:0 skip:0 total:8
   XArray: 158825682 of 158825682 tests passed

The focused cases cover a direct root, a deep node chain, forced node
creation when root storage is disallowed, an existing chain, a direct value
in an internal ancestor slot, zero-entry reservation, free-mark
initialization, an error cursor, and target-slot positioning for a reused
store cursor.

No accepted validation log contained a BUG, Oops, KASAN report, warning, call
trace, kernel panic, or RCU stall.

Symbol inspection showed that ``rust/kernel.o`` defines
``rust_xarray_create()``. Object disassembly showed that ``xas_create()`` and
``rust_xarray_create_for_store()`` enter that Rust symbol. The C fallback kept
the complete C ``xas_create()`` and contained no Rust creation symbol.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Rust Sphinx generation completed. Its only warning was an existing
cross-reference warning in the unchanged Rust index page.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, or tagged entry layout changes.
* Root, child, or parent publication rules change.
* Node allocation or update callback ordering changes.
* Free-entry tracking or mark storage changes.
* ``xas_create()`` stops entering the selected creation implementation.
* The C fallback starts using a different creation or cursor policy.
* A supported target needs different root, shift, sibling, or mark
  primitives.
