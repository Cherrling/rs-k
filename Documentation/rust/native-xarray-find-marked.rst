.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray marked forward search
========================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_FIND_MARKED`` option selects an experimental native
Rust implementation of ``xas_find_marked()``. The option depends on the Rust
forward-search, mark lifecycle, and advanced-load paths.

Rust owns the production implementation of these operation groups:

* Restart, bounds, direct-root, and error dispatch.

* Mark-driven chunk scans and chunk jumps.

* Sibling normalization and index updates.

* Empty-subtree retreat and parent traversal.

* Maximum-index and unsigned wrap handling.

* Terminal bounds and restart cursor states.

The C fallback retains its existing marked-search policy.

Entry points and ownership
--------------------------

The public ``xas_find_marked()`` interface and its ``EXPORT_SYMBOL_GPL``
export class do not change. With this option enabled, the definition in
``lib/xarray.c`` forwards to ``rust_xarray_find_marked()``.

The production implementation is in ``rust/kernel/xarray_find_marked.rs``.
It uses Rust for traversal state while calling narrow C primitives for slot,
parent, root-mark, and bitmap access.

Retained C boundaries
---------------------

The XArray node, state, cursor, mark bitmap, and tagged-entry layouts remain
C. Rust reads and updates cursor fields through generated bindings.

RCU and slot access remain C primitives. Parent lookup uses ``xa_parent()``.
Slot access uses ``xa_entry()`` so the implementation works under RCU
protection or the XArray lock.

Mark storage remains C. The root mark helper uses ``xa_marked()``. The chunk
scan helper uses the C mark bitmap and ``find_next_bit()``. Rust owns when to
start, stop, jump, and descend from each chunk scan.

Top, restart, bounds, and error sentinels remain C primitives because they use
the C-owned tagged pointer encoding.

Node and entry classification, node decoding, and sibling detection remain C
primitives. Rust owns the search decisions that consume their results.

The complete C marked-search algorithm remains compiled when this option is
disabled.

Important invariants
--------------------

The caller must hold the RCU read lock or the XArray lock. The implementation
does not publish changes, but it follows parent links and reads slots and
marks.

An error cursor returns immediately without changing the cursor. An initial
index above the maximum moves the cursor to restart.

A direct root entry returns only when the root mark is set. A marked node root
causes descent with the root offset selected from the cursor index.

A chunk scan returns the first set mark bit at or after the selected offset.
Rust updates the index before checking the maximum boundary. The wrap check
preserves C unsigned-long behavior at the maximum index.

A sibling entry redirects the cursor to its canonical slot and updates the
index. A child node causes descent with the offset derived from the index.

If traversal runs past the tree, the result is bounds unless the index has
advanced past the maximum. Reaching the maximum boundary moves the cursor to
restart.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for sparse marks, direct roots, maximum cutoff,
  empty roots, sibling ranges, the maximum index, and an error cursor.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that ``xas_find_marked()`` enters Rust
  in production and the C fallback keeps the complete marked-search algorithm.

Validation record
-----------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. The fallback disabled only
this unit and retained the existing Rust forward-search and mark paths. The
KASAN build used 3 GiB of guest memory. Every run reported:

.. code-block:: text

   xarray_find_marked: pass:7 fail:0 skip:0 total:7
   XArray: 158825682 of 158825682 tests passed

The focused cases cover sparse traversal, direct roots, maximum cutoff,
empty roots, sibling ranges, the maximum index, and an existing error cursor.

No accepted validation log contained a BUG, Oops, KASAN report, warning, call
trace, kernel panic, or RCU stall.

Symbol inspection showed that ``rust/kernel.o`` defines
``rust_xarray_find_marked()``. Object disassembly showed that the production
``xas_find_marked()`` jumps directly to that symbol. The C fallback retained
the complete C marked-search algorithm and contained no Rust marked-search
symbol.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Rust Sphinx generation completed with only existing unresolved-link
warnings in unchanged Rust documentation references.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, mark bitmap, or tagged entry layout changes.

* RCU slot, parent, mark, or lock rules change.

* Error, bounds, restart, sibling, or node encoding changes.

* Chunk-scan ordering or mark propagation changes.

* ``xas_find_marked()`` stops entering the selected implementation.

* The C fallback starts using a different marked-search policy.

* A supported target needs different cursor or mark primitives.
