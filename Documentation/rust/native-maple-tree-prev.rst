.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree reverse iteration
========================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_PREV`` option selects a native Rust production
implementation of ``mas_prev()``, ``mas_prev_range()``,
``mas_find_rev()``, and ``mas_find_range_rev()``. The option supports x86_64
and depends on the native Rust Maple Tree forward iteration unit.

Rust owns the production reverse iterator:

* State normalization for start, paused, empty, underflow, and overflow
  states.

* Parent ascent and the pivot metadata needed for the covered range.

* Previous peer-node selection when a leaf is exhausted.

* Slot selection and null-slot skipping for the entry and range variants.

* Dead-node restart and the underflow transition.

* First-entry inclusion and subsequent-entry exclusion for reverse finds.

The existing C reverse iterator remains available when the option is
disabled.

Entry point and ownership
-------------------------

The public interfaces and their ``EXPORT_SYMBOL_GPL`` classes do not change.
With this option enabled, the four public reverse functions are ABI adapters
that call Rust.

The shared C consumers of ``mas_prev_slot()`` and ``mas_prev_node()`` call
Rust helpers when the option is enabled. Rust therefore also covers the
reverse policy used by internal Maple Tree consumers.

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
Rust owns when reverse traversal requires them.

Important invariants
--------------------

A reverse setup rejects a start index at or below the requested minimum
before it changes the Maple state.

An active iterator first moves to the preceding peer node. That operation
ascends until an ancestor has a preceding slot, then descends through the
last populated slot of each child.

The selected node range starts after its preceding pivot. Its maximum comes
from the source ancestor range.

``mas_prev()`` skips null slots. ``mas_prev_range()`` returns each adjacent
range, including a null range, with updated ``index`` and ``last`` metadata.

``mas_find_rev()`` includes the range at the initial index and excludes that
range on subsequent calls. ``mas_find_range_rev()`` applies the same find
policy to every returned range.

Paused and overflow states restart from the saved index. Underflow stops the
iterator without extending below the requested minimum.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for empty and single-entry trees, sparse and multi-node
  reverse traversal, range metadata, pause and overflow recovery, underflow,
  minimum and maximum indexes, and concurrent RCU readers.

* The same runs with the native Rust reverse iterator disabled.

* The same runs with the native Rust reverse iterator enabled and KASAN
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

The Rust images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests. The upstream Maple Tree self-test passed 3,809,321 checks in each run.

The fallback images passed the same one-CPU and four-CPU runs. Each run passed
6 KUnit tests and the same 3,809,321 upstream checks.

The KASAN images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests and the complete 3,809,321-check upstream suite. No KASAN report
occurred.

The lockdep images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests. No lockdep warning or call trace occurred.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict Sphinx generation reported two existing
cross-reference warnings outside this document. The checkpatch
``FILE_PATH_CHANGES`` reminder was suppressed because this change updates
``MAINTAINERS`` for every new file.

The C-only build used ``CONFIG_RUST=n``. It compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_prev_kunit.o``.

The Rust image contains ``rust_mas_prev``, ``rust_mas_prev_range``,
``rust_mas_prev_node``, ``rust_mas_prev_slot``, ``rust_mas_find_rev``, and
``rust_mas_find_range_rev``. The four public adapters tail-jump to Rust.

The enabled C object has Rust relocations for the public reverse adapters.
The fallback object has no Rust reverse references. It retains
``mas_prev_setup``, ``mas_prev_node``, ``mas_prev_slot``,
``mas_find_rev_setup``, and all four public C implementations.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple parent-pointer encoding, root tagging, pivot counts, or node layout
  changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* A C helper starts to select reverse iterator state transitions or
  traversal policy.

* A shared C consumer stops using the native Rust reverse helpers.
