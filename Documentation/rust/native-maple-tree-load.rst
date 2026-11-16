.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree loads
============================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_LOAD`` option selects an experimental native Rust
implementation of ``mtree_load()``. The option currently supports x86_64.

Rust owns the production load policy:

* Maple state initialization and lockdep sequence setup.

* Root selection for empty, single-entry, and node-backed trees.

* Single-entry handling for index zero.

* Pivot selection and descent from the root to a leaf.

* Dead-node detection, state reset, and restart from the root.

* RCU read-side lifetime and zero-entry conversion to NULL.

The existing C load traversal remains available when the option is disabled.

Entry point and ownership
-------------------------

The public interface and its ``EXPORT_SYMBOL`` class do not change. With this
option enabled, ``mtree_load()`` is an ABI adapter that calls
``rust_mtree_load()`` in ``rust/kernel/maple_tree_load.rs``.

Rust owns the complete public lookup control flow. The enabled C object does
not select pivots, descend nodes, retry dead nodes, or duplicate the lookup
policy.

Retained C boundaries
---------------------

Maple node encoding remains C. Rust calls narrow helpers for encoded-node
classification, type extraction, root-bit removal, and conversion to a raw
node address.

Node layout remains C. Rust calls helpers that return the pivot and slot arrays
for the current concrete Maple node type.

Protected access remains C. Slot reads use ``rcu_dereference_check()`` and the
existing Maple tree lockdep state. The dead-node test performs the required
read barrier before comparing the parent pointer.

Lockdep diagnostics remain C because the checks depend on the current Lockdep
configuration and Maple lock class implementation.

The ``ma_read`` tracepoint remains C. Rust calls one helper with the stable
``mtree_load`` trace name.

RCU primitives remain C. Rust owns the lifetime and restart loop around the
read-side critical section.

Important invariants
--------------------

A start-state lookup always begins at the current root under RCU protection.

A root that contains a node is converted to an active state before descent. A
dead root node sets the state back to start and repeats from the current root.

A non-node root covers index zero only. Any lookup above zero returns NULL
without changing the root entry.

Pivot selection chooses the first pivot greater than or equal to the requested
index. The selected slot is read with the existing Maple protection and lockdep
checks.

Descent stops at any leaf node. A dead intermediate node resets the state to
start and restarts the complete lookup.

The reserved ``XA_ZERO_ENTRY`` value is hidden from callers after the RCU
read-side critical section ends.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for empty and single-entry trees, deep nodes, zero
  entries, the maximum index, and concurrent RCU readers.

* The same runs with native Rust loads disabled.

* The same runs with Rust enabled and KASAN enabled.

* The complete upstream Maple Tree regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that the fallback contains no Rust load symbols.

* Disassembly checks showing that ``mtree_load()`` calls Rust when enabled and
  that the C fallback retains the complete traversal.

Validation results
------------------

The final revision passed the formal twelve-run startup matrix: one single-CPU
run and three four-CPU runs for each of the Rust, C fallback, and Rust plus
KASAN builds. Every run passed the five focused KUnit cases and the complete
upstream Maple Tree regression. The one-CPU runs passed 5,357,658 checks. The
four-CPU runs passed 5,357,709 checks. The matrix passed 64,292,355 checks in
total.

The lockdep build passed the focused five-case KUnit suite in one single-CPU
run and one four-CPU run. The logs contained no BUG, WARNING, Oops, KASAN
report, RCU stall, or panic marker.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported zero errors, warnings,
and checks after excluding its generic new-file reminder, because MAINTAINERS
already lists the new files. The focused Sphinx build reported two existing
cross-reference warnings outside this document.

The Rust build contains both ``mtree_load`` and ``rust_mtree_load``. Its
``mtree_load`` body tail-jumps to ``rust_mtree_load``. The fallback build
contains only ``mtree_load`` and retains the complete C traversal.

Configuration notes
-------------------

KASAN used outline instrumentation and ``CONFIG_FRAME_WARN=4096``. Existing C
stack frames in ``mas_wr_store_entry`` and ``vfs_coredump`` exceed the inline
KASAN limit. This configuration does not relax a limit reached only by the
new Rust code.

The lockdep run used ``CONFIG_TEST_MAPLE_TREE=n`` and ran the focused KUnit
suite. The existing C ``mt_validate`` path reports suspicious RCU usage under
lockdep. A strict RCU grace-period configuration was not validated for this
unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple node encoding, root tagging, pivot counts, or slot layout changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* The ``ma_read`` tracepoint contract changes.

* A C helper starts to select lookup policy instead of providing a primitive.

* The fallback starts to use any new Rust load symbol.
