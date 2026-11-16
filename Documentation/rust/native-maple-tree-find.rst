.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree forward finds
====================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_FIND`` option selects a native Rust production
implementation of ``mas_find()`` and ``mas_find_range()``. The option supports
x86_64 and depends on the native Rust Maple Tree forward iteration unit.

Rust owns the production forward find policy:

* State normalization for start, active, paused, empty, root, underflow,
  overflow, and out-of-range states.

* Initial inclusion of the range that contains the requested index.

* Subsequent exclusion of the previously returned range.

* Null-slot skipping for entry finds and adjacent range reporting for range
  finds.

* Forward traversal through the native Rust slot selector.

The existing C forward find setup and both public C functions remain available
when the option is disabled.

Entry point and ownership
-------------------------

The public interfaces and their ``EXPORT_SYMBOL_GPL`` classes do not change.
With this option enabled, ``mas_find()`` and ``mas_find_range()`` are ABI
adapters that call Rust.

Rust calls the existing native Rust ``mas_next_slot`` policy directly. Find
setup does not change the public C entry point or expose a C-owned traversal
decision.

Retained C boundaries
---------------------

Parent-pointer encoding remains C. Rust calls narrow helpers for parent
classification, parent type, parent slot, and encoded-node construction
through the forward iterator.

Node layout remains C. Rust calls helpers for pivot and slot arrays, pivot
counts, data ends, and raw-node conversion.

Protected access remains C. Slot reads retain the Maple tree RCU and lockdep
checks. Dead-node detection retains the required read barrier.

Lockdep initialization, lock checks, and the impossible-transition diagnostic
remain C primitives. Rust owns when find setup and forward traversal require
them.

Important invariants
--------------------

The first find includes the range that contains ``mas->index``. A repeated
find starts after the previously returned range.

A paused find resumes after ``mas->last``. A none state resumes from
``mas->last`` when its saved maximum allows another search.

An active find returns no entry when the previous range reaches the requested
maximum. Entry finds discard a resulting overflow status, while range finds
retain it.

Underflow first checks the requested maximum. If recovery is possible, Rust
restarts the walk at the saved index through the protected state.

A single-entry root outside its covered index moves to the none state with
``index == 1`` and ``last == ULONG_MAX``. A failed find cannot extend through
that range.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for empty and root states, repeated finds, range
  metadata, pause and resume, underflow and overflow recovery, maximum
  indexes, none states, and concurrent RCU readers.

* The same runs with the native Rust find implementation disabled.

* The same runs with the native Rust find implementation enabled and KASAN
  enabled.

* The complete upstream Maple Tree regression suite in every run.

* Focused lockdep runs with one and four virtual CPUs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* A C-only compile with Rust and all native Maple Tree options disabled.

* Symbol and disassembly checks for the Rust adapters and C fallback.

Validation results
------------------

The final revision passed the validation matrix on 2026-11-17.

The Rust images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests. The upstream Maple Tree self-test passed 3,809,321 checks in each run.

The fallback images passed the same one-CPU and four-CPU runs. Each run passed
6 KUnit tests and the same 3,809,321 upstream checks.

The KASAN images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests and the complete upstream suite. No KASAN report occurred.

The lockdep images passed one-CPU and four-CPU runs. Each run passed 6 KUnit
tests and the complete upstream suite. No lockdep warning or call trace
occurred.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict Sphinx generation reported two existing
cross-reference warnings outside this document.

The C-only build used ``CONFIG_RUST=n``. It compiled ``lib/maple_tree.o`` and
``lib/tests/maple_tree_find_kunit.o``.

The Rust image contains ``rust_mas_find`` and ``rust_mas_find_range``. The
``mas_find`` and ``mas_find_range`` adapters tail-jump to Rust.

The enabled C object has Rust relocations for both public adapters. The
fallback object has no Rust forward find references. It retains the public C
``mas_find`` and ``mas_find_range`` implementations and their complete find
setup.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Maple parent-pointer encoding, root tagging, pivot counts, or node layout
  changes.

* RCU semantics, protection checks, or dead-node publication changes.

* Lockdep initialization or Maple lock classes change.

* A C helper starts to select forward find state transitions or inclusion
  policy.

* The native Rust forward slot selector stops serving find traversal.
