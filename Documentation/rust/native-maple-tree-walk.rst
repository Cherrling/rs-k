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

Rust directly defines ``mas_walk()`` in
``rust/kernel/maple_tree_walk.rs``. The public interface and its
``EXPORT_SYMBOL_GPL`` class do not change. The automatic Rust exporter
provides the GPL export.

Rust owns the complete public state machine and traversal policy. The native
C object does not define ``mas_walk()``, select pivots, descend nodes, update
Maple range metadata, retry dead nodes, or duplicate the state conversion
policy.

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

* A lockdep build with four virtual CPUs and a PREEMPT_RT build with four
  virtual CPUs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and both native Maple Tree options disabled.

* Symbol checks showing that Rust defines ``mas_walk()``, the native C object
  defines no walk body, and the fallback and C-only objects define the
  complete state machine.

* Export checks showing that ``mas_walk`` remains a GPL export.

Validation results
------------------

Validation ran on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native            Native Rust, one and three four-CPU runs
Rust fallback     Both native Maple Rust options disabled, one and three
                  four-CPU runs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust plus ``CONFIG_KASAN=y``, one and three four-CPU
                  runs
Native lockdep    Native Rust plus ``CONFIG_PROVE_LOCKING=y``, four CPUs
Native RT         Native Rust plus ``CONFIG_PREEMPT_RT=y``, four CPUs
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, ``CONFIG_MAPLE_TREE_WALK_KUNIT_TEST=y``, and
``CONFIG_TEST_MAPLE_TREE=y``.

All six kernel configurations compiled successfully. All 16 QEMU runs passed
all five ``maple_tree_walk`` KUnit cases and the complete Maple Tree
self-test. Native, fallback, C-only, lockdep, and RT runs passed 3,809,321
checks each. The KASAN SMP1 run passed 5,357,658 checks. Each KASAN SMP4 run
passed 5,357,709 checks. The matrix passed 67,142,637 checks in total.

The KASAN run initialized KASAN. The lockdep run enabled lock dependency
tracking and prove-locking. The RT kernel reported an ``SMP PREEMPT_RT``
build. The logs contained no BUG, warning, KASAN report, suspicious RCU
usage, lock inversion, Oops, panic, or RCU stall.

The Rust build contains ``mas_walk`` but no ``rust_mas_walk`` adapter.
``rust/kernel.o`` defines ``mas_walk``. The native ``lib/maple_tree.o``
defines no walk body or export record. The fallback and C-only objects define
the complete C traversal. ``Module.symvers`` records ``mas_walk`` as
``EXPORT_SYMBOL_GPL``, and the generated Rust export header contains
``EXPORT_SYMBOL_RUST_GPL(mas_walk)``.

Rustfmt, Clippy, Rustdoc, kernel-doc, focused Sphinx generation, strict
checkpatch, and ``git diff --check`` passed. The focused Sphinx build reported
two existing cross-reference warnings outside this document.

Configuration notes
-------------------

KASAN used inline instrumentation and ``CONFIG_FRAME_WARN=2048``. All tested
C and Rust stack frames fit this limit.

The lockdep run also enabled ``CONFIG_TEST_MAPLE_TREE`` and completed the full
Maple Tree self-test without a lockdep or RCU diagnostic.

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
