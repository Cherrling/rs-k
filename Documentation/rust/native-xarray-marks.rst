.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray mark migration
=================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_MARKS`` option selects an experimental native Rust
implementation of the XArray mark lifecycle in ``lib/xarray.c``. The option
currently supports x86_64 and depends on the native Rust XArray store path.

Rust owns the production implementation of these operation groups:

* Cursor validity checks for mark lookup and mutation.
* Lookup of a mark on the root or current node slot.
* Propagation of a set mark from the current node to its ancestors.
* Propagation stop conditions when a node or ancestor already has the mark.
* Propagation of a clear mark only through ancestors without other marked
  descendants.
* Root mark flag updates after propagation reaches the root.
* Initialization of all marks, including the free-tracking policy.

The existing C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public ``xas_get_mark()``, ``xas_set_mark()``, ``xas_clear_mark()``, and
``xas_init_marks()`` interfaces do not change. All four remain
``EXPORT_SYMBOL_GPL`` interfaces.

With ``CONFIG_RUST_XARRAY_MARKS`` enabled, these definitions in
``lib/xarray.c`` forward directly to Rust and do not execute a C mark
algorithm.

The Rust production implementation is in ``rust/kernel/xarray_marks.rs``.
It calls narrow C adapters for operations that depend on mark bitmaps, root
flags, parent traversal, or the XArray lock protocol.

Retained C boundaries
---------------------

Mark bitmap access remains C. The helpers use ``test_bit()``,
``__test_and_set_bit()``, ``__test_and_clear_bit()``, and ``bitmap_empty()``
on the existing node mark arrays.

Root mark storage remains C. The helpers read and update the encoded mark
bits in ``xa_flags``.

Parent traversal remains C. ``xa_parent_locked()`` obtains the next ancestor
with the existing lockdep assertion.

Free-tracking configuration remains C. The helper reads the
``XA_FLAGS_TRACK_FREE`` policy bit from the existing root flags.

The XArray node layout, cursor layout, lock, and root flag representation
remain C. Rust does not duplicate their storage layout.

Important invariants
--------------------

A cursor with non-zero low address bits is invalid. Mark lookup returns
false, and mark mutation and initialization return without changing the
XArray.

For a valid root cursor, lookup uses the root mark flags. For a valid node
cursor, lookup tests the mark bitmap for the current slot.

Setting a mark updates the current slot first. Propagation stops when an
ancestor slot already has the mark. If the walk reaches the root, the root
mark flag is set if it is not already present.

Clearing a mark updates the current slot first. Propagation stops if the
slot was already clear. It also stops when the current node still has any
other slot with the mark, because that descendant must keep every ancestor
marked.

Mark initialization visits every mark from zero through ``XA_MARK_MAX``. A
free-tracking XArray sets ``XA_FREE_MARK`` and clears all other marks. Other
XArrays clear every mark.

The caller must hold either the XArray lock or an RCU read-side critical
section. Root and node mark mutation requires the XArray lock.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* All focused XArray mark KUnit cases with repeated SMP4 execution.
* A Rust-disabled C fallback with the same KUnit cases.
* The complete upstream runtime XArray regression suite for Rust and the C
  fallback.
* A Rust-enabled x86_64 KASAN build with SMP1 and repeated SMP4 execution.
* Rustfmt, Clippy, Rust documentation, strict checkpatch, and
  ``git diff --check``.
* Export-class checks for the four public advanced mark interfaces.
* Disassembly checks showing the C adapters enter Rust directly.
* Symbol checks showing that the C fallback contains no Rust mark path.

Validation record
-----------------

The Rust-enabled image passed all eight focused XArray mark KUnit cases on
one virtual CPU and in three repeated four-CPU runs. The Rust-disabled C
fallback and the Rust-enabled KASAN image passed the same four runs.

Every Rust, C fallback, and KASAN run also passed the complete upstream
runtime suite. Each run reported ``158825682 of 158825682`` XArray checks
passed.

The focused tests cover parent propagation, independent marks, invalid
cursors, mark initialization, free-mark allocation and reuse, erase policy,
multi-index marks, and concurrent writer and reader stress. The concurrent
writer alternates mark updates and store or erase operations across shallow
and deep indexes.

No validation log contained a BUG, Oops, KASAN report, warning, call trace,
kernel panic, or RCU stall. The KASAN build disabled errors for existing
frame-size warnings only. Generic, inline, and stack KASAN instrumentation
remained enabled.

Object-level disassembly showed a direct jump from each C adapter to its Rust
production function. ``Module.symvers`` classified all four public interfaces
as ``EXPORT_SYMBOL_GPL``.

The C fallback image contained the four ``xas_*_mark`` symbols and no
``rust_xarray_mark*`` or ``rust_xarray_node_mark*`` symbols.

Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict checkpatch,
and ``git diff --check`` passed.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node or cursor layout changes.
* The root mark encoding or free-tracking flags change.
* The XArray lock or ancestor traversal protocol changes.
* A C helper starts to select mark propagation or initialization policy.
* A C adapter stops forwarding directly to Rust.
* A supported target needs a different mark bitmap representation.
* Performance data shows that a retained primitive needs a different
  implementation.
