.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray adjacent cursor movement
===========================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_NEXT_PREV`` option selects an experimental native
Rust implementation for ``xas_prev()`` and ``xas_next()``. The option depends
on the Rust advanced-load path.

Rust owns the production implementation of these operation groups:

* Previous and next index updates with unsigned wrapping.

* Offset adjustment from the new index.

* Parent retreat across chunk boundaries.

* Child descent and offset updates.

* Restart, bounds, and error cursor dispatch.

The C fallback retains its existing adjacent-index policy, including the leaf
fast path in the inline API.

Entry points and ownership
--------------------------

The public ``xas_prev()`` and ``xas_next()`` interfaces and the
``EXPORT_SYMBOL_GPL`` export classes of ``__xas_prev()`` and ``__xas_next()``
do not change.

With this option enabled, the inline public functions call the exported
out-of-line entry points for every movement. Those definitions in
``lib/xarray.c`` forward directly to ``rust_xarray_prev()`` and
``rust_xarray_next()``.

The production implementation is in
``rust/kernel/xarray_next_prev.rs``. Rust calls the native Rust advanced-load
implementation for restart, bounds, and error cursors.

Retained C boundaries
---------------------

The XArray node, state, cursor, and tagged-entry layouts remain C. Rust reads
and updates the cursor fields through generated bindings.

RCU and slot access remain C primitives. The entry helper uses ``xa_entry()``
so the function works under either RCU protection or the XArray lock. Parent
lookup uses ``xa_parent()``.

Frozen and non-node cursor classification remain C primitives because they use
the C-owned tagged pointer encoding. Bounds and error terminal states also
remain C primitives.

Node and node-entry classification and decoding remain C primitives. Rust
owns the cursor movement decisions that consume their results.

The complete C adjacent-index algorithms and inline leaf fast paths remain
compiled when this option is disabled.

Important invariants
--------------------

The caller must hold the RCU read lock or the XArray lock. The implementation
does not publish changes, but it follows parent links and reads slots.

A frozen cursor does not change its index before load dispatch. A bounds or
non-frozen invalid cursor updates the index and then loads through the advanced
load path. A null cursor updates the index and then moves to bounds.

For a live node, the offset is adjusted only when it does not already match the
offset selected by the updated index. Previous movement uses an unsigned-byte
decrement so offset zero retreats through the parent. Next movement advances a
full chunk through the parent.

Parent retreat replaces the offset with the current node position minus or
plus one before following the parent link. A child entry causes descent with
the offset derived from the current index.

Index operations use wrapping unsigned-long arithmetic. Previous movement at
zero reaches the maximum index. Next movement at the maximum index reaches
zero.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for leaf movement, deep parent crossings, restart
  cursors, null cursors, errors, index wrapping, sibling entries, and bounds
  cursors.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that both production entry points enter
  Rust and the C fallback keeps both adjacent-index algorithms.

Validation record
-----------------

Validation used base commit ``6a0e6cc222fefb69e4251c6701c9b28b5d4493dc``.
Every boot completed the focused suite and the complete upstream XArray suite:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Runs
     - Focused result
     - XArray result
   * - Rust
     - SMP1, SMP4 rounds 1-3
     - 8 passed, 0 failed
     - 158825682 of 158825682 passed
   * - C fallback
     - SMP1, SMP4 rounds 1-3
     - 8 passed, 0 failed
     - 158825682 of 158825682 passed
   * - Rust with KASAN
     - SMP1, SMP4 rounds 1-3
     - 8 passed, 0 failed
     - 158825682 of 158825682 passed

The boot logs contained no KUnit failures, BUG reports, warnings, Oops reports,
panics, KASAN reports, call traces, or RCU stalls.

The Rust object defined ``rust_xarray_prev`` and ``rust_xarray_next``. The
Rust-enabled ``lib/xarray.o`` defined ``__xas_prev`` and ``__xas_next`` as
direct tail calls to those symbols. The C fallback object defined both C
algorithms and had no references to either Rust adjacent-cursor symbol.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with no errors. It reported two existing
unresolved-reference warnings outside this unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, cursor, or tagged entry layout changes.

* RCU slot, parent, or lock rules change.

* Frozen, error, bounds, sibling, or node encoding changes.

* Advanced load ordering changes.

* ``xas_prev()`` or ``xas_next()`` stops entering the selected implementation.

* The C fallback starts using a different adjacent-index policy.

* A supported target needs different cursor or parent primitives.
