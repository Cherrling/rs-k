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

Rust directly defines ``mtree_load()`` in
``rust/kernel/maple_tree_load.rs``. The public interface and its ordinary
``EXPORT_SYMBOL`` class do not change. C retains the export metadata in
``lib/maple_tree.c``, and ``rust/Makefile`` excludes ``mtree_load`` from the
automatic Rust GPL exporter.

Rust owns the complete public lookup control flow. The native C object does
not define ``mtree_load()``, select pivots, descend nodes, retry dead nodes,
or duplicate the lookup policy.

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

* Symbol checks showing that Rust defines ``mtree_load()``, the native C
  object defines no load body, and the fallback and C-only objects define the
  complete traversal.

* Export checks showing that ``mtree_load`` remains an ordinary
  ``EXPORT_SYMBOL`` and the generated Rust GPL export header excludes it.

Validation results
------------------

Validation ran on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native            Native Rust, one and three four-CPU runs
Rust fallback     ``CONFIG_RUST_MAPLE_TREE_LOAD=n``, one and three four-CPU
                  runs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust plus ``CONFIG_KASAN=y``, one and three four-CPU
                  runs
Native lockdep    Native Rust plus ``CONFIG_PROVE_LOCKING=y``, four CPUs
Native RT         Native Rust plus ``CONFIG_PREEMPT_RT=y``, four CPUs
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, ``CONFIG_MAPLE_TREE_LOAD_KUNIT_TEST=y``, and
``CONFIG_TEST_MAPLE_TREE=y``.

All six kernel configurations compiled successfully. All 16 QEMU runs passed
all five ``maple_tree_load`` KUnit cases and the complete Maple Tree self-test.
Native, fallback, C-only, lockdep, and RT runs passed 3,809,321 checks each.
The KASAN SMP1 run passed 5,357,658 checks. Each KASAN SMP4 run passed
5,357,709 checks. The matrix passed 67,142,637 checks in total.

The KASAN run initialized KASAN. The lockdep run enabled lock dependency
tracking and prove-locking. The RT kernel reported an ``SMP PREEMPT_RT``
build. The logs contained no BUG, warning, KASAN report, suspicious RCU
usage, lock inversion, Oops, panic, or RCU stall.

Rustfmt, Clippy, Rustdoc, kernel-doc, focused Sphinx generation, strict
checkpatch, and ``git diff --check`` passed. The focused Sphinx build reported
two existing cross-reference warnings outside this document.

The Rust build contains ``mtree_load`` but no ``rust_mtree_load`` adapter.
``rust/kernel.o`` defines ``mtree_load``. The native ``lib/maple_tree.o``
defines no load body and retains one unresolved reference and the export
metadata. The fallback and C-only objects define the complete C traversal.
``Module.symvers`` records ``mtree_load`` as ``EXPORT_SYMBOL``, and the
generated Rust GPL export header excludes it.

Configuration notes
-------------------

KASAN used inline instrumentation and ``CONFIG_FRAME_WARN=2048``. All tested
C and Rust stack frames fit this limit.

The lockdep run also enabled ``CONFIG_TEST_MAPLE_TREE`` and completed the full
Maple Tree self-test without a lockdep or RCU diagnostic.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple node encoding, root tagging, pivot counts, or slot layout changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* The ``ma_read`` tracepoint contract changes.

* A C helper starts to select lookup policy instead of providing a primitive.

* The fallback starts to use any new Rust load symbol.
