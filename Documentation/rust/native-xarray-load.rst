.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray load migration
=================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_LOAD`` option selects an experimental native Rust
implementation of XArray load traversal in ``lib/xarray.c``. The option
currently supports x86_64.

Rust owns the production implementation of these operation groups:

* Root entry selection and validity checks.
* Cursor reload and node descent.
* Multi-index sibling expansion.
* Advanced-load traversal.
* Zero-entry conversion and RCU retry loops for ``xa_load()``.

The existing C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public ``xa_load()`` and ``xas_load()`` interfaces and export classes do
not change. With ``CONFIG_RUST_XARRAY_LOAD`` enabled, the definitions in
``lib/xarray.c`` forward directly to Rust and do not execute a load traversal
algorithm.

The two Rust entry points are:

* ``rust_xarray_load()`` for the basic ``xa_load()`` interface.
* ``rust_xarray_load_advanced()`` for the advanced ``xas_load()`` interface.

The production implementation is in ``rust/kernel/xarray_load.rs``.

Retained C boundaries
---------------------

RCU lock acquisition remains in C. The Rust basic-load entry point uses the
kernel RCU abstraction to protect node lifetime during traversal.

The XArray node layout remains in C. Rust does not duplicate the slot array,
node shift, node parent, or cursor field layouts.

Slot access remains C. ``xa_entry()`` performs the existing READ_ONCE() slot
load, and ``xa_head()`` reads the array root.

Tagged entry classification remains C. The XArray helpers identify nodes,
sibling entries, retry entries, and zero entries.

Secondary cursor operations remain C. The helpers initialize, reset, and mark
the advanced cursor, and retrieve its encoded error state.

Important invariants
--------------------

A valid ``xa_state`` cursor has zero low address bits. An invalid state uses
the low bits to encode bounds, restart, or an error.

If the root is a direct entry, only index zero can return it. If the root is a
node, the index must fit within the root node shift and fanout.

Node descent records the selected node before sibling expansion. A sibling
entry normally redirects the slot load. For an interior node, a sibling that
redirects to another node becomes a retry entry because walking the tree must
restart from the root.

The caller must hold either the XArray lock or an RCU read-side critical
section. The lock or RCU section protects the lifetime of every visited node.

The basic ``xa_load()`` interface converts reserved zero entries to NULL. The
advanced ``xas_load()`` interface preserves internal entries for its caller.

The basic interface restarts a traversal after a retry entry. It resets the
cursor before it loads the root again.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* All six focused XArray load KUnit cases with repeated SMP4 execution.
* A Rust-disabled C fallback with the same KUnit cases.
* The upstream runtime XArray regression suite for Rust and C fallback.
* A Rust-enabled x86_64 KASAN build with SMP1 and repeated SMP4 execution.
* Rustfmt, Clippy, Rust documentation, strict checkpatch, and
  ``git diff --check``.
* Export-class checks for ``xa_load()`` and ``xas_load()``.
* Disassembly checks showing C adapters enter Rust and contain no load walk.

Measured validation results
---------------------------

The Rust-enabled image passed all six focused KUnit cases on one virtual CPU
and in three repeated four-CPU runs. The Rust-disabled C fallback passed the
same four runs.

The upstream runtime suite passed in both images. Each image reported
``158825682 of 158825682`` XArray checks passed.

The Rust-enabled KASAN image passed all six focused KUnit cases and the
upstream runtime suite on one virtual CPU and in three repeated four-CPU runs.
The KASAN build disabled errors for the existing frame-size warning only.
KASAN instrumentation remained enabled.

The concurrent test alternates store and erase operations. This forces deep
node removal, reallocation, and RCU reclamation while four reader threads load
the same index.

None of the Rust, C fallback, or KASAN boot logs contained a BUG, Oops, kernel
panic, KASAN report, warning, call trace, or RCU stall.

The C fallback image contained only the ``xa_load()`` and ``xas_load()``
symbols from this unit. It contained no ``rust_xarray_*`` or new
``rust_helper_xarray_*`` symbols.

Object-level disassembly showed two direct jumps from the C adapters to Rust.
The Rust kernel object contained the root selection, node descent, sibling
expansion, cursor reload, and retry logic.

Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation with warnings as
errors, and ``git diff --check`` passed. Strict checkpatch passed with the
expected file-addition warning suppressed because this change also updates
``MAINTAINERS``.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The root or node representation changes.
* The tagged entry encoding changes.
* RCU node-lifetime rules change.
* A supported target needs a different cursor state representation.
* A C helper starts to select traversal policy.
* A C adapter stops forwarding directly to Rust.
* A public interface starts to execute a C load traversal again.
* Performance data shows that an inline Rust path needs separate review.
