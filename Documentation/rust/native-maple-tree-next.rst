.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree forward iteration
========================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_NEXT`` option selects a native Rust production
implementation of ``mas_next()`` and ``mas_next_range()``. The option supports
x86_64 and depends on the native Rust Maple Tree walk unit.

Rust owns the production forward iterator:

* State normalization for start, paused, empty, overflow, and underflow
  states.

* Parent ascent and the pivot metadata needed for the covered range.

* Peer-node selection when a leaf is exhausted.

* Slot selection and null-slot skipping for the entry and range variants.

* Dead-node restart and the overflow transition.

The existing C forward iterator remains available when the option is disabled.

Entry point and ownership
-------------------------

The public interfaces and their ``EXPORT_SYMBOL_GPL`` classes do not change.
With this option enabled, ``mas_next()`` and ``mas_next_range()`` are ABI
adapters that call Rust.

The shared C consumers of ``mas_next_slot()``, ``mas_next_node()``, and
``mas_ascend()`` call Rust helpers when the option is enabled. The Rust policy
therefore also covers the forward traversal used by ``mas_find()``.

Retained C boundaries
---------------------

Parent-pointer encoding remains C. Rust calls narrow helpers for root and
parent classification, parent type, parent slot, and encoded-node
construction.

Node layout remains C. Rust calls helpers for pivot and slot arrays, pivot
counts, data ends, and raw-node conversion.

Protected access remains C. Slot reads retain the Maple tree RCU and lockdep
checks. Dead-node detection retains the required read barrier.

Lockdep checks and the impossible-transition diagnostic remain C primitives.
Rust owns when ascent and forward traversal require them.

Important invariants
--------------------

Parent ascent validates that the parent pointer is stable before the state
changes to the ancestor.

The covered minimum and maximum are derived from the first ancestor slots
that identify both bounds. A dead ancestor restarts the protected operation.

An exhausted leaf selects the next peer node. The iterator descends through
the first slot and recalculates the range metadata before exposing an entry.

``mas_next()`` skips null slots. ``mas_next_range()`` returns each adjacent
range, including a null range, with updated ``index`` and ``last`` metadata.

Paused and underflow states restart from the saved index. Overflow stops the
iterator without extending beyond the requested maximum.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for empty and single-entry trees, sparse and multi-node
  forward traversal, range metadata, reused overflow, pause and underflow
  restart, maximum indexes, and concurrent RCU readers.

* The same runs with the native Rust forward iterator disabled.

* The same runs with the native Rust forward iterator enabled and KASAN
  enabled.

* The complete upstream Maple Tree regression suite in every run.

* Focused lockdep runs with one and four virtual CPUs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and all native Maple Tree options disabled.

* Symbol and disassembly checks for the Rust adapters and C fallback.

Validation results
------------------

The final revision passed the validation matrix on 2026-11-16.

The Rust images passed one-CPU and four-CPU runs. Each run passed 16 KUnit
tests. The upstream Maple Tree self-test passed 3,809,321 checks in each run.

The fallback images passed one-CPU and four-CPU runs. Each run passed 16 KUnit
tests. The upstream self-test passed 5,357,658 checks on one CPU and 5,357,709
checks on four CPUs.

The KASAN images passed the same matrix. The KASAN self-test counts matched the
fallback counts. No KASAN report occurred.

The lockdep images passed one-CPU and four-CPU runs. Each run passed 16 KUnit
tests. No lockdep warning or call trace occurred.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx reported existing cross-reference warnings
outside this document. Clippy used a focused exemption for an existing finding
in ``rust/kernel/vma_unmap.rs``. This unit does not modify that file.

The C-only build used ``CONFIG_RUST=n``. It compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_next_kunit.o``.

The Rust image contains ``rust_mas_next``, ``rust_mas_next_range``,
``rust_mas_ascend``, ``rust_mas_next_node``, and ``rust_mas_next_slot``. The
``mas_next`` and ``mas_next_range`` adapters tail-jump to Rust.

The enabled C object has Rust relocations for ascent, forward slot selection,
and both public adapters. The fallback image contains no ``rust_mas_next``
symbol. It retains ``mas_next_setup``, ``mas_next_node``, ``mas_next_slot``,
``mas_ascend``, and both public C implementations.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple parent-pointer encoding, root tagging, pivot counts, or node layout
  changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* A C helper starts to select iterator state transitions or traversal policy.

* A shared C consumer stops using the native Rust forward helpers.
