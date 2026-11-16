.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray teardown
===========================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_DESTROY`` option selects an experimental native Rust
implementation of ``xa_destroy()``. The option currently supports x86_64 and
depends on native Rust XArray mark and subtree-reclamation units.

Rust owns the production teardown policy:

* Root detachment ordering.
* Reinitialization of every root mark.
* The zero-busy allocation-array special case.
* Selection and invocation of subtree reclamation.
* Reuse of the XArray after teardown.

The existing C teardown algorithm remains available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``xa_destroy()`` interface and its ``EXPORT_SYMBOL`` export class do
not change.

With this option enabled, the C entry point acquires the XArray lock with
interrupts disabled, calls ``rust_xarray_destroy_tree()``, and restores the
saved interrupt state. The Rust production implementation is in
``rust/kernel/xarray_destroy.rs``.

Rust reuses the native mark lifecycle and subtree-reclamation modules. It does
not call the C teardown algorithm.

Retained C boundaries
---------------------

Interrupt-safe XArray locking remains in C. The C entry point owns lock
acquisition and interrupt restoration.

Root publication remains C. The detach helper reads the locked root with
``xa_head_locked()`` and clears it with ``RCU_INIT_POINTER()``.

The node, cursor, and tagged-entry layouts remain C. Rust calls the existing
entry classifiers and node decoder.

Root flag storage remains C. Rust mark policy chooses which flags to set or
clear, but the helpers perform the flag update.

Node reclamation remains C. The native free-nodes module publishes retry
entries, resets accounting, invokes node update callbacks, and schedules RCU
reclamation.

Important invariants
--------------------

The root is detached before marks or nodes are changed. Readers that observe
the old root hold RCU protection while its nodes remain live.

Every root mark is reinitialized after detachment. An XArray with free-entry
tracking receives the free mark because all indices are available after
teardown.

An XArray initialized with zero-busy semantics clears the free mark after mark
reinitialization. Index zero remains reserved in that configuration.

Referenced objects are not owned by the XArray. ``xa_destroy()`` releases only
internal nodes and does not release entries stored by the caller.

A detached node subtree is not visible from the new root. The reclamation
walk replaces reachable slots with retry entries before scheduling each node
for RCU reclamation.

The lock, root layout, and configuration flags remain valid after teardown.
A normal XArray can accept new stores after ``xa_destroy()`` returns.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* Six focused KUnit cases for empty, direct-root, deep-tree, zero-busy,
  reuse, and concurrent RCU-reader behavior.
* The same four runs with native Rust teardown disabled.
* The same four runs with Rust enabled and KASAN enabled.
* The complete upstream XArray regression suite in every run.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* Symbol and disassembly checks for the enabled adapter, Rust production
  symbols, and the C fallback.

Validation results
------------------

This unit was validated against ``c1e3cb9cddab`` with the unit patch applied.

The Rust-enabled, C fallback, and Rust-enabled KASAN images each passed one
single-CPU run and three four-CPU runs. Every run passed all six focused KUnit
cases and the complete XArray suite with 158825682 of 158825682 tests.

The concurrent case uses four readers while the main test thread tears down a
prepopulated deep tree. This overlaps reader traversal with retry publication
and RCU reclamation. No validation log contained a KASAN report, BUG, Oops,
warning, call trace, kernel panic, or RCU stall.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with pre-existing cross-reference
warnings in unrelated documents.

The enabled Rust object exports ``rust_xarray_destroy_tree()``. The C object
imports it, and disassembly shows ``xa_destroy()`` acquiring the lock, calling
Rust, and restoring interrupts. The enabled C object contains no
``xas_free_nodes()`` subtree walk.

The fallback object defines the complete C teardown algorithm and contains no
reference to ``rust_xarray_destroy_tree()``. The fallback Rust object does not
define the teardown symbol.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The root representation, root marks, or zero-busy flag rules change.
* XArray locking or interrupt-state handling changes.
* RCU node-lifetime or retry-entry rules change.
* The native mark or free-nodes modules change their contracts.
* A C helper starts to select teardown policy instead of providing a primitive.
* The C adapter stops restoring the saved interrupt state.
* Destructible XArrays require a different reuse protocol.
