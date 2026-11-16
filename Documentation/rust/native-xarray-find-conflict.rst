.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray range conflict search
========================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_FIND_CONFLICT`` option selects an experimental
native Rust implementation for ``xas_find_conflict()``. The option depends on
the Rust advanced-load path.

Rust owns the production implementation of these operation groups:

* Root start and cursor validation.

* Child descent and sibling resolution.

* Range-boundary checks.

* Sibling skipping and locked slot scans.

* Parent retreat and terminal offset restoration.

The C fallback retains its existing conflict-search policy.

Entry points and ownership
--------------------------

The public ``xas_find_conflict()`` interface and its ``EXPORT_SYMBOL_GPL``
export class do not change.

With this option enabled, ``xas_find_conflict()`` in ``lib/xarray.c`` forwards
directly to ``rust_xarray_find_conflict()``. The production implementation is
in ``rust/kernel/xarray_find_conflict.rs``.

Rust reuses the native Rust advanced-load start and descent helpers. These
helpers remain internal to the Rust implementation.

Retained C boundaries
---------------------

The XArray node, state, cursor, range, sibling, and tagged-entry layouts remain
C. Rust reads and updates the cursor fields through generated bindings.

The XArray lock remains in C. The caller holds this lock for the complete
conflict search.

Tagged pointer primitive classification and decoding remain C operations.
Locked slot and parent access also remain C helpers.

Important invariants
--------------------

An error or null cursor returns NULL without changing the cursor.

A top cursor starts a new walk through the native Rust load path. A direct root
entry can become an immediate conflict. A root node is descended before the
range scan.

After descent, a node above the requested range shift ends the search. A node
at the requested range shift stops after the last sibling covered by the
range. Lower nodes retreat through their parent after the last chunk slot.

The search skips sibling entries. It descends node entries and inspects the
first slot of each child. A node shift remains unchanged while the search
walks the requested range.

When the search ends without a conflict, it restores the offset to the first
slot covered by the range.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for errors, empty cursors, direct roots, sibling
  resolution, absent ranges, and repeated conflict traversal.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that the production entry point enters
  Rust and the C fallback keeps its conflict algorithm.

Validation record
-----------------

Validation used base commit ``999a994eaa919a5c49edd27a05915e8a4b7afc91``.
Every boot completed the focused suite and the complete upstream XArray suite:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Runs
     - Focused result
     - XArray result
   * - Rust
     - SMP1, SMP4 rounds 1-3
     - 6 passed, 0 failed
     - 158825682 of 158825682 passed
   * - C fallback
     - SMP1, SMP4 rounds 1-3
     - 6 passed, 0 failed
     - 158825682 of 158825682 passed
   * - Rust with KASAN
     - SMP1, SMP4 rounds 1-3
     - 6 passed, 0 failed
     - 158825682 of 158825682 passed

The boot logs contained no KUnit failures, BUG reports, warnings, Oops reports,
panics, KASAN reports, call traces, or RCU stalls.

The Rust object defined ``rust_xarray_find_conflict``. The Rust-enabled
``lib/xarray.o`` defined ``xas_find_conflict`` as a direct tail call to that
symbol. The C fallback object defined the complete C conflict-search algorithm
and had no reference to ``rust_xarray_find_conflict``.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with no errors. It reported two existing
unresolved-reference warnings outside this unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, range, sibling, or tagged entry layout
  changes.

* XArray lock rules or locked slot access rules change.

* Top, error, bounds, sibling, or node encoding changes.

* Advanced-load start or descent ordering changes.

* ``xas_find_conflict()`` stops entering the selected implementation.

* The C fallback starts using a different conflict-search policy.

* A supported target needs different cursor, parent, or slot primitives.
