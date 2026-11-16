.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray multi-index split
====================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_SPLIT`` option selects experimental native Rust
implementations for the two-phase multi-index split path:

* ``xas_split_alloc()`` prepares detached nodes and their sibling layout.

* ``xas_split()`` commits the prepared nodes under the XArray lock.

Rust owns order validation, pending-chain growth, child initialization,
publication order, mark propagation, and value accounting. The C fallback
retains the complete original algorithms.

Entry points and ownership
--------------------------

The public ``xas_split_alloc()`` and ``xas_split()`` interfaces and their
``EXPORT_SYMBOL_GPL`` export classes do not change.

With this option enabled, both entry points in ``lib/xarray.c`` forward
directly to exported Rust functions. The production implementation is in
``rust/kernel/xarray_split.rs``.

The separate ``xas_try_split()`` one-shot policy remains in C and is outside
this unit.

Retained C boundaries
---------------------

The XArray node, state, sibling, tagged-entry, and mark layouts remain C.
Slab allocation uses the native allocation-lifecycle boundary from the
``RUST_XARRAY_NOMEM`` unit.

Relaxed parent and slot initialization remain C RCU operations. RCU slot
publication and XArray node update callbacks also remain C boundaries.

Mark bitmap access remains C. Rust owns which parent and child slots receive
each propagated mark.

Error encoding remains C. Rust decides when an unsupported or failed
preparation becomes ``-ENOMEM``.

Important invariants
--------------------

``xas_split_alloc()`` does not allocate when the requested output shift
already covers the source order. It rejects requests that would require more
than two levels below the current cursor.

A successful preparation builds one detached node per sibling covered by the
requested split. Every prepared node contains either the source entry or a
sibling pointer to that entry.

Prepared nodes are linked newest-first through their parent field. A partial
allocation failure releases the complete pending chain and records ENOMEM.

``xas_split()`` starts from the native Rust load result. A top cursor is a
no-op.

For a vertical split, the current pending node becomes a child of the cursor
node. Rust sets child depth, offset, count, and value count before mark and
slot publication.

For a horizontal split, the canonical slot receives the entry and the
remaining sibling slots receive canonical sibling pointers.

The parent value count is adjusted once after all covered slots are updated.
The node update callback runs for every new child and for the final parent.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for no-op allocation, multi-node preparation, top
  cursors, horizontal splits, vertical splits, marks, and entry visibility.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite, including its exhaustive
  multi-index split and try-split coverage, in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that both production entry points
  enter Rust and that the C fallback retains both complete algorithms.

Validation record
-----------------

Validation used base commit ``66aac65c4323f5b9cee6676320f340c590a73af3``.
Every boot completed the focused suite and the complete upstream XArray suite:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Runs
     - Focused result
     - XArray result
   * - Rust
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed
   * - C fallback
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed
   * - Rust with KASAN
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed

The boot logs contained no KUnit failures, BUG reports, warnings, Oops reports,
panics, KASAN reports, call traces, or RCU stalls.

The Rust object defined ``rust_xarray_split_alloc`` and ``rust_xarray_split``.
The Rust-enabled ``lib/xarray.o`` defined both public entry points as direct
tail calls to those symbols. The C fallback object defined both complete C
algorithms and had no reference to either Rust symbol.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with no errors. It reported two existing
unresolved-reference warnings outside this unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, sibling, tagged-entry, or mark layout changes.

* Split preparation stops using the parent field as its pending-chain link.

* RCU initialization, slot publication, or node update callback rules change.

* Mark propagation or parent value accounting changes.

* ``xas_try_split()`` starts sharing a different preparation or commit
  policy.

* A supported target needs different allocation, RCU, or mark primitives.
