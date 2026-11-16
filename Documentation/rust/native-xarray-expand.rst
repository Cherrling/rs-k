.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray root expansion
=================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_EXPAND`` option selects an experimental native Rust
implementation of XArray root expansion for the native Rust store path. The
option depends on the Rust store path and currently supports x86_64.

Rust owns the production implementation of these operation groups:

* The shift needed for an empty root.
* The next shift after wrapping an existing root node.
* The maximum index required by the current cursor.
* The repeated root-wrapping loop.
* Propagation of root marks into each new node.
* Parent-link creation and root publication order.
* Update notification before the next wrapping step.

The C fallback retains its existing expansion policy.

Entry points and ownership
--------------------------

With ``CONFIG_RUST_XARRAY_EXPAND`` enabled, ``rust_xarray_expand()`` is the
expansion entry point used by the selected node-chain creation path. A native
Rust creation path calls the Rust expansion function directly. A C creation
fallback calls the exported Rust expansion function. The C ``xas_expand()``
function is compiled only for builds that disable this unit.

The production implementation is in ``rust/kernel/xarray_expand.rs``. It calls
the retained ``xas_alloc()`` primitive for each new node. Rust selects the
shift, count, value accounting, mark propagation, and publication order.

Retained C boundaries
---------------------

The XArray node and state layouts remain C. Rust reads node shift, count,
array, cursor, and slot fields through the generated bindings.

Node allocation remains C. C obtains a preallocated or newly allocated node,
records the parent pointer, and initializes the common layout fields.

Mark bitmap and root flag storage remain C. C helpers fill node mark bitmaps
and read or set root mark flags. Rust decides which marks propagate.

RCU publication remains C. C helpers initialize slot zero and publish a new
root with the required publication operations. Rust controls when each
operation occurs.

Node update callbacks remain C. Rust calls the retained ``xas_update()``
adapter after each new root is visible.

Important invariants
--------------------

The caller must hold the XArray lock. Expansion runs before ``xas_create()``
descends into the wrapped tree.

An empty root returns the first shift that can contain the cursor without
allocating. A non-empty direct entry starts at shift zero. An existing root
node starts one chunk shift above its current shift.

Rust sets the cursor node to NULL before the wrapping loop. This preserves the
rule that root wrapping does not account the old root as a child of itself.

Each new node contains the previous root in slot zero. Its direct count is one.
Its direct value count is one only when the previous root is a value entry.

Rust propagates each root mark into slot zero of the new node. When free-entry
tracking is active, every new free-mark slot starts marked. If the root free
mark is clear, Rust clears slot zero and sets the root free mark.

Rust links the previous root node to its new parent before publishing the new
root. It invokes the update callback before the next wrapping iteration.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Dedicated KUnit coverage for empty-root growth, wrapping a direct value,
  parent chains, mark propagation, and free-mark propagation.
* The same four runs with this Rust unit disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict checkpatch,
  and ``git diff --check``.
* Symbol and disassembly checks proving that ``xas_create()`` calls Rust
  expansion in production and the C fallback retains ``xas_expand()``.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each passed
one SMP1 run and three repeated SMP4 runs. Every run reported:

.. code-block:: text

   xarray_expand: pass:4 fail:0 skip:0 total:4
   XArray: 158825682 of 158825682 tests passed

The focused tests cover empty-root growth, wrapping a direct value root,
parent-slot links across the created chain, user-mark propagation, and
free-mark propagation for reserved slots.

No accepted validation log contained a BUG, Oops, KASAN report, non-test
warning, call trace, kernel panic, or RCU stall.

Symbol inspection showed that ``rust/kernel.o`` defines
``rust_xarray_expand()`` and the production ``lib/xarray.o`` references it.
The fallback Rust object contained no expansion symbol, and the fallback C
object contained no Rust expansion reference.

Disassembly showed that the production ``xas_create()`` calls
``rust_xarray_expand()`` before descending into the wrapped tree. The
fallback keeps the complete C expansion control flow.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, or tagged entry layout changes.
* Root publication or parent-link rules change.
* Root or node mark storage changes.
* The free-entry tracking policy changes.
* The node update callback or allocation lifecycle changes.
* ``xas_create()`` stops entering the selected expansion implementation.
* The C fallback starts using a different expansion policy.
* A supported target needs different root, shift, or mark primitives.
