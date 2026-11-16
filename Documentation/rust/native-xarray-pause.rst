.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray pause and cursor recovery
============================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_PAUSE`` option selects an experimental native Rust
implementation for ``xas_pause()``. The option depends on the Rust advanced
load path.

Rust owns the production implementation of these operation groups:

* Restart and bounds cursor selection.

* Sibling-run scanning from the current slot.

* Index alignment and advancement for the selected node shift.

* Wrapping advancement of a null cursor.

The C fallback retains its existing pause policy, including slot scans and
cursor recovery.

Entry points and ownership
--------------------------

The public ``xas_pause()`` interface and its ``EXPORT_SYMBOL_GPL`` export class
do not change.

With this option enabled, ``xas_pause()`` in ``lib/xarray.c`` forwards directly
to ``rust_xarray_pause()``. The production implementation is in
``rust/kernel/xarray_pause.rs``.

Retained C boundaries
---------------------

The XArray node, state, cursor, sibling, and tagged-entry layouts remain C.
Rust reads and updates the cursor fields through generated bindings.

The XArray lock and RCU primitives remain in C. The caller holds the XArray
lock or the RCU read lock while the pause scan reads slots. Rust uses
``xa_entry()`` through a helper and ``xa_is_sibling()`` to classify each slot.

Restart, bounds, and invalid cursor encoding remain C primitives. Rust owns the
decisions that consume and set these states.

Important invariants
--------------------

An invalid cursor, including an error, restart, or bounds cursor, remains
unchanged.

A valid null cursor advances its index with unsigned wrapping and becomes a
restart cursor. It does not read a slot.

A live cursor becomes a restart cursor before the implementation scans the
remaining slots in its chunk. The scan stops at the first non-sibling slot or
at the end of the chunk.

The new index clears the bits below the node shift. The implementation then
adds the distance from the old slot to the selected slot, shifted by the node
shift. Index addition uses unsigned wrapping.

If the resulting index is zero, the cursor becomes a bounds cursor. A wrapped
index at the maximum unsigned long follows this rule.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for invalid and null cursors, ordinary advancement,
  sibling runs, index wrapping, bounds selection, and resumed iteration.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that the production entry point enters
  Rust and the C fallback keeps its pause algorithm.

Validation record
-----------------

Validation used base commit ``2df3676732b794e40f3ac7f2a75c01042665a418``.
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

The Rust object defined ``rust_xarray_pause``. The Rust-enabled
``lib/xarray.o`` defined ``xas_pause`` as a direct tail call to that symbol.
The C fallback object defined the complete C pause algorithm and had no
reference to ``rust_xarray_pause``.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with no errors. It reported two existing
unresolved-reference warnings outside this unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, sibling, or tagged entry layout changes.

* XArray lock rules or slot access rules change.

* Restart, bounds, invalid, or sibling encoding changes.

* ``xas_pause()`` stops entering the selected implementation.

* The C fallback starts using a different pause policy.

* A supported target needs different cursor or slot primitives.
