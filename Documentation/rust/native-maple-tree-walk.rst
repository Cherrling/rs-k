.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree stateful walks
=====================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_WALK`` option selects an experimental native Rust
implementation of ``mas_walk()``. The option currently supports x86_64 and
depends on the native Rust Maple Tree load unit.

Rust owns the production walk policy:

* Initial state normalization and the ``ma_error``, ``ma_none``, and
  ``ma_pause`` restart decisions.

* Root selection for empty, single-entry, and node-backed trees.

* Pivot selection and descent from the root to a leaf.

* Range metadata updates for ``index``, ``last``, ``offset``, ``end``,
  ``min``, and ``max``.

* Dead-node detection, state reset, and restart from the current root.

* The single-entry result transitions for a hit at zero and a miss above zero.

The existing C stateful walk remains available when the option is disabled.

Entry point and ownership
-------------------------

The public interface and its ``EXPORT_SYMBOL_GPL`` class do not change. With
this option enabled, ``mas_walk()`` is an ABI adapter that calls
``rust_mas_walk()`` in ``rust/kernel/maple_tree_walk.rs``.

Rust owns the complete public state machine and traversal policy. The enabled
C object does not select pivots, descend nodes, update Maple range metadata,
retry dead nodes, or duplicate the state conversion policy.

Shared traversal primitives live in ``rust/kernel/maple_tree_core.rs``. The
native Rust load and walk units use the same Rust state initialization and
entry traversal primitives.

Retained C boundaries
---------------------

Maple node encoding remains C. Rust calls narrow helpers for encoded-node
classification, type extraction, root-bit removal, and conversion to a raw
node address.

Node layout remains C. Rust calls helpers that return the pivot and slot arrays
for the current concrete Maple node type.

Traversal metadata remains C. The node end offset uses the existing
``ma_data_end()`` implementation because it interprets the node metadata
layout.

Protected access remains C. Slot and root reads use the existing RCU checks
and Maple tree lockdep state. Dead-node checks retain the required read
barrier.

Lockdep and strict RCU bookkeeping remain C because they depend on the current
Lockdep and RCU configuration. Rust calls one helper before state
normalization.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for empty and single-entry trees, range metadata,
  zero entries, the maximum index, and concurrent RCU readers.

* The same runs with the native Rust walk disabled.

* The same runs with the native Rust walk enabled and KASAN enabled.

* The complete upstream Maple Tree regression suite in every run.

* Focused lockdep runs with one and four virtual CPUs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and both native Maple Tree options disabled.

* Symbol checks showing that the fallback contains no Rust walk symbol.

* Disassembly checks showing that ``mas_walk()`` jumps to Rust when enabled.

Validation results
------------------

The final revision passed the formal twelve-run startup matrix on
2026-11-16. It used one single-CPU run and three four-CPU runs for each of the
Rust, C fallback, and Rust plus KASAN builds.

Every run passed the five focused KUnit cases and the complete upstream Maple
Tree regression. The single-CPU runs passed 5,357,658 checks. The four-CPU
runs passed 5,357,709 checks. The matrix passed 64,292,355 checks in total.

The lockdep build passed the focused five-case KUnit suite in one single-CPU
run and one four-CPU run. No accepted validation log contained a BUG,
WARNING, Oops, KASAN report, RCU stall, call trace, or panic marker.

Rustfmt, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. The focused Sphinx build reported existing
cross-reference warnings outside this document. Strict checkpatch excluded
the generic ``FILE_PATH_CHANGES`` reminder because MAINTAINERS lists all new
files.

Clippy passed after removing two unit-specific unnecessary safety comments.
The run used ``KRUSTFLAGS=-Aclippy::missing_safety_doc`` for the existing,
unmodified ``rust_vma_unmap_test_range()`` finding in
``rust/kernel/vma_unmap.rs``.

The C-only check used ``CONFIG_RUST=n`` with both native Maple Tree options
disabled. It compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_walk_kunit.o``.

The Rust build contains both ``mas_walk`` and ``rust_mas_walk``. The 9-byte
``mas_walk`` body tail-jumps to ``rust_mas_walk``. The fallback build contains
only ``mas_walk``. Its 319-byte body calls ``mtree_range_walk()`` and retains
the complete C state machine.

Configuration notes
-------------------

KASAN used outline instrumentation, stack instrumentation, and no inline
instrumentation. ``CONFIG_FRAME_WARN=8192`` accommodates existing C KUnit
frames above the default limit. This setting does not hide a frame-size
report from the new Rust code.

The lockdep run used ``CONFIG_TEST_MAPLE_TREE=n`` and ran the focused KUnit
suite. The complete Maple Tree regression remains in the Rust, fallback, and
KASAN runs.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple node encoding, root tagging, pivot counts, slot layout, or metadata
  layout changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* A C helper starts to select state transitions or traversal policy instead of
  providing a primitive.

* The native Rust load no longer uses the shared traversal primitives.
