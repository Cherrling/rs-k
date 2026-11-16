.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray range creation
=================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_CREATE_RANGE`` option selects an experimental native
Rust implementation of ``xas_create_range()``. The option depends on the Rust
node-chain creation path and currently supports x86_64.

Rust owns the production implementation of these operation groups:

* The last index covered by the requested multi-index range.
* Cursor offset adjustment when it already identifies the order node.
* Conversion of the cursor to single-index mode.
* The reverse traversal from the last chunk to the first chunk.
* Repositioning across zero-offset ancestors after each chunk.
* Final cursor positioning at the first index in the range.
* Restoration of the original order and index after an allocation error.

The C fallback retains its existing range creation and cursor recovery
policy.

Entry points and ownership
--------------------------

The public ``xas_create_range()`` interface and its ``EXPORT_SYMBOL_GPL``
export class do not change. With this option enabled, the definition in
``lib/xarray.c`` forwards to ``rust_xarray_create_range()``.

The production implementation is in
``rust/kernel/xarray_create_range.rs``. It calls the native Rust node-chain
creation function for each required chunk. Rust controls the range boundary,
backward traversal, ancestor repositioning, and recovery order.

Retained C boundaries
---------------------

The XArray node, state, cursor, and tagged-entry layouts remain C. Rust reads
and updates the cursor fields through the generated bindings.

Parent lookup remains a C primitive because it depends on the C-owned node
layout and lock protocol.

Error classification remains a C primitive because cursor errors use the
C-owned tagged pointer encoding.

Node allocation, common node initialization, slab ownership, RCU publication,
mark storage, and update callbacks remain inside the retained node-chain
creation primitives.

The complete C range creation algorithm remains compiled when this option is
disabled.

Important invariants
--------------------

The caller must hold the XArray lock when the cursor references an XArray.
The error-only path can run without an XArray because it returns before any
tree access.

The range end is derived from the original index, shift, and sibling count.
The original values are saved before the cursor switches to single-index
mode.

An existing node cursor at the requested order adjusts its offset by the
sibling mask before range traversal begins.

After creating the last chunk, Rust moves backward one chunk at a time. If
the current node is below the requested order, Rust follows parent links
until it reaches an ancestor with a nonzero offset or the requested shift.

On success, the original first index returns to the cursor. The cursor shift
and sibling count remain zero, and the offset identifies the first requested
slot.

On an error, the original shift, sibling count, and index return to the
cursor. The error pointer remains visible so the caller can allocate memory
and retry with ``xas_nomem()``.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Focused KUnit coverage for a single index, a multi-chunk range, a deep
  range, an existing multi-index entry, and error recovery.
* The same four runs with this Rust unit disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* Symbol and disassembly checks proving that ``xas_create_range()`` enters
  Rust in production and the C fallback keeps the complete C algorithm.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. The KASAN build used 3 GiB
of guest memory. Every run reported:

.. code-block:: text

   xarray_create_range: pass:5 fail:0 skip:0 total:5
   XArray: 158825682 of 158825682 tests passed

The focused cases cover a single-index request, a multi-chunk range, a deep
range crossing two node levels per chunk, conversion of an existing
multi-index entry, and restoration of a pre-existing error cursor.

No accepted validation log contained a BUG, Oops, KASAN report, warning, call
trace, kernel panic, or RCU stall.

Symbol inspection showed that ``rust/kernel.o`` defines
``rust_xarray_create_range()``. Object disassembly showed that the production
``xas_create_range()`` jumps directly to that symbol. The C fallback retained
the complete C range traversal and contained no Rust range symbol.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Rust Sphinx generation completed with only pre-existing
cross-reference warnings in unchanged Rust pages.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, or tagged entry layout changes.
* Parent lookup or lock rules change.
* Error pointer encoding or recovery rules change.
* Node-chain creation ordering changes.
* ``xas_create_range()`` stops entering the selected implementation.
* The C fallback starts using a different range or recovery policy.
* A supported target needs different cursor or range primitives.
