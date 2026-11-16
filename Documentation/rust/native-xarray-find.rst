.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray forward search
=================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_FIND`` option selects an experimental native Rust
implementation of ``xas_find()``. The option depends on the Rust load path
and currently supports x86_64.

Rust owns the production implementation of these operation groups:

* Restart, valid-cursor, bounds, error, and empty-root dispatch.

* Maximum-index boundary handling.

* Cursor advancement from the current offset.

* Index updates derived from node shifts and offsets.

* Empty-subtree traversal and parent retreat.

* Sibling-entry filtering.

* Placement of the cursor at the bounds sentinel after traversal.

The C fallback retains its existing forward-search policy.

Entry points and ownership
--------------------------

The public ``xas_find()`` interface and its ``EXPORT_SYMBOL_GPL`` export class
do not change. With this option enabled, the definition in ``lib/xarray.c``
forwards to ``rust_xarray_find()``.

The production implementation is in ``rust/kernel/xarray_find.rs``. It calls
the native Rust advanced load implementation for restart cursors and uses
narrow C primitives for parent and slot access.

Retained C boundaries
---------------------

The XArray node, state, cursor, and tagged-entry layouts remain C. Rust reads
and updates the cursor fields through the generated bindings.

RCU and slot access remain C primitives. The entry helper uses
``xa_entry()`` so the function works under either RCU protection or the
XArray lock. The parent helper uses ``xa_parent()`` for the same contexts.

Error and bounds sentinels remain C primitives because they use the C-owned
tagged pointer encoding.

Node and entry classification, node decoding, and sibling detection remain C
primitives. Rust owns the search decisions that consume their results.

The complete C forward-search algorithm remains compiled when this option is
disabled.

Important invariants
--------------------

The caller must hold the RCU read lock or the XArray lock. The implementation
does not publish changes, but it follows parent links and reads slots.

An error or bounds cursor returns immediately without changing the tree.
An initial index above the maximum moves the cursor to bounds.

A restart cursor first loads the requested location. A direct or internal
entry returns immediately. A loaded non-node cursor also returns immediately,
including bounds or a retry state.

A real node cursor advances by one offset. When the offset reaches the chunk
size, Rust moves to the node referenced by the parent slot and continues with
the next parent offset.

A node slot causes descent with offset zero. A sibling entry is skipped.
A non-NULL non-sibling entry becomes the result.

Index masking and addition use wrapping arithmetic to preserve C unsigned
long behavior at the maximum index.

If traversal runs past the tree, a null cursor node becomes the bounds
sentinel. Exhaustion caused only by the maximum index leaves the current node
position unchanged.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Focused KUnit coverage for sparse restart, unaligned resumption, an empty
  root, bounds, sibling skipping, the maximum index, and an error cursor.
* The same four runs with this Rust unit disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* Symbol and disassembly checks proving that ``xas_find()`` enters Rust in
  production and the C fallback keeps the complete search algorithm.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. The KASAN build used 3 GiB
of guest memory. Every run reported:

.. code-block:: text

   xarray_find: pass:7 fail:0 skip:0 total:7
   XArray: 158825682 of 158825682 tests passed

The focused cases cover sparse restart traversal, unaligned resumption, an
empty root, initial and traversal bounds, sibling skipping, the maximum
index, and an existing error cursor.

No accepted validation log contained a BUG, Oops, KASAN report, warning, call
trace, kernel panic, or RCU stall.

Symbol inspection showed that ``rust/kernel.o`` defines
``rust_xarray_find()``. Object disassembly showed that the production
``xas_find()`` jumps directly to that symbol. The C fallback retained the
complete C search algorithm and contained no Rust find symbol.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Rust Sphinx generation completed with only existing unresolved-link
warnings in unchanged Rust documentation references.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, or tagged entry layout changes.

* RCU slot, parent, or lock rules change.

* Error, bounds, retry, sibling, or node encoding changes.

* Advanced load ordering changes.

* ``xas_find()`` stops entering the selected implementation.

* The C fallback starts using a different search policy.

* A supported target needs different cursor or parent primitives.
