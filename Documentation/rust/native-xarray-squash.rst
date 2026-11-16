.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray mark squash migration
========================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_SQUASH_MARKS`` option selects an experimental native
Rust implementation of the XArray multi-index mark squash policy. The option
depends on the native Rust XArray mark path and currently supports x86_64.

Rust owns the production implementation of these operation groups:

* Iteration over every supported mark.
* Detection of a marked sibling after the first slot.
* Union of sibling marks onto the first slot.
* Clearing of the remaining sibling slots after the union.
* The control flow used by the Rust multi-index store path.

The existing C algorithm remains available for the C store fallback when the
option is disabled.

Entry points and ownership
--------------------------

With ``CONFIG_RUST_XARRAY_SQUASH_MARKS`` enabled, ``xarray_store.rs`` calls
the Rust implementation directly. The shared declaration remains for the
exported ABI check. The C definition of ``rust_xarray_squash_marks()`` is
compiled only for the C store fallback.

The production implementation is in ``rust/kernel/xarray_squash.rs``. It calls
narrow C helpers for bitmap search, one-bit updates, and bitmap clearing.

Retained C boundaries
---------------------

Mark bitmap search remains C. ``find_next_bit()`` locates the first marked
sibling in the bounded range.

Mark bitmap mutation remains C. ``__set_bit()`` sets the first slot and
``bitmap_clear()`` clears the remaining siblings.

The XArray node layout, mark bitmap representation, mark count, XArray lock,
and cursor layout remain C. Rust uses the existing fields and does not
duplicate their layout.

Important invariants
--------------------

The cursor must identify a real node with a non-empty sibling range. The
caller must hold the XArray lock.

For each mark, Rust searches only the sibling slots after the first slot. If
any are marked, the first slot is set and all remaining sibling marks are
cleared. If none are marked, the range is unchanged.

The limit is exclusive. Clearing cannot extend beyond the current sibling
range.

The operation preserves the mark union required by a multi-index entry. Every
index covered by the entry sees the same union after the store completes.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Focused XArray squash KUnit tests on SMP1 and in three repeated SMP4 runs.
* A Rust-disabled squash build with the same four runs.
* The complete upstream XArray regression suite in every run.
* A Rust-enabled KASAN build with SMP1 and three repeated SMP4 runs.
* Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict
  checkpatch, and ``git diff --check``.
* Symbol checks proving that the C squash adapter is absent from the Rust
  build and all Rust squash symbols are absent from the fallback.
* Disassembly checks showing the Rust store path enters the Rust squash
  implementation.

Validation evidence
-------------------

The unit was validated on 2026-11-16 with x86_64 LLVM builds.

The Rust-enabled, C-fallback, and Rust-enabled KASAN configurations each
passed one SMP1 run and three repeated SMP4 runs. Every run reported:

.. code-block:: text

   xarray_squash: pass:2 fail:0 skip:0 total:2
   XArray: 158825682 of 158825682 tests passed

The logs contained no BUG, Oops, KASAN report, warning, call trace, RCU stall,
or kernel panic.

Rustfmt, Clippy, Rustdoc, the focused Rust documentation build, strict
checkpatch, and ``git diff --check`` passed.

Symbol inspection showed that the Rust build defines
``rust_xarray_squash_marks()`` in the Rust object and does not define it in
``lib/xarray.o``. The fallback build defines it in ``lib/xarray.o`` and does
not contain the Rust squash helper symbols.

Disassembly showed that ``rust_xarray_store()`` calls the Rust squash
implementation when a sibling range is present.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, cursor, or mark bitmap layout changes.
* The multi-index sibling representation changes.
* The XArray lock or mark synchronization protocol changes.
* A C helper starts to select union or clearing policy.
* The Rust store path stops entering the Rust squash implementation.
* A supported target needs a different bitmap primitive.
