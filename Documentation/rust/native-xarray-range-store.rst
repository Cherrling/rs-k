.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray range stores and order queries
==================================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_RANGE_STORE`` option selects experimental native Rust
implementations of ``xa_store_range()``, ``xas_get_order()``, and
``xa_get_order()``. The option currently supports x86_64 and requires native
Rust node creation, operation preallocation, and store mutation.

Rust owns the production range policy:

* Rejection of internal entries and reverse ranges.

* Starting order selection from the least-significant bit of ``last + 1``.

* Natural range selection, alignment, and block-tail truncation.

* Repeated multi-index stores and forward range advancement.

* Lock release after ``-ENOMEM`` and retry after preallocation.

* Sibling-run scanning for both public and advanced order queries.

The existing C range algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public interfaces and their ``EXPORT_SYMBOL`` classes do not change. With
this option enabled, each C entry point forwards directly to its matching Rust
production function.

Rust owns the complete control flow in ``rust/kernel/xarray_range_store.rs``.
It calls the native Rust creation, load, store, and preallocation units. The
retained configuration selection does not leave a second range algorithm in
the enabled C object.

Retained C boundaries
---------------------

XArray locking remains C. Rust calls the plain XArray lock and unlock helpers
because ``xa_store_range()`` does not inherit a caller-held lock.

RCU protection remains C. The public order query enters an RCU read-side
critical section before loading the entry. The advanced order query requires
the caller to hold the XArray lock or the RCU read lock.

Tagged-entry encoding remains C. Rust classifies internal entries by their low
bits and calls a narrow C helper for the existing warning and error encoding.

Protected slot access remains C. Rust calls a narrow helper that performs the
same RCU and lockdep checked dereference as ``xa_entry()``.

Node layout, node creation, slot publication, mark propagation, and RCU
reclamation remain C primitives used by the native store path.

Important invariants
--------------------

The inclusive range requires ``first <= last``. A NULL entry erases the range
and does not allocate nodes solely to represent empty space.

A non-NULL query entry uses the order implied by the least-significant zero in
``last + 1`` to prepare the operation. If ``last + 1`` wraps to zero, the
starting order is the width of ``unsigned long``.

Each store pass refreshes ``xa_shift`` and ``xa_sibs`` from the remaining
inclusive range. Rust advances by the represented range size and never calls a
store for an index after ``last``.

An operation that reports ``-ENOMEM`` exits with the XArray lock released.
The native preallocation unit either prepares one retry or returns the encoded
error. A partial successful store is not repeated.

An order query starts after ``xas_load`` resolves sibling entries. It examines
slots spaced by powers of two within the current node and adds the node shift.
An empty query returns order zero.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for validation, direct and deep single-index stores,
  aligned and unaligned ranges, chunk crossings, deep natural alignment,
  overwrite, erasure, cursor and public order queries, high indexes, and
  concurrent RCU readers.

* The same runs with native Rust range stores disabled.

* The same runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks showing that all three C entry points call
  Rust in the enabled configuration and that the fallback retains all three C
  algorithms.

Validation results
------------------

This unit was validated on 2026-11-16 against
``3afdfccba736e56e481652f226c839a6fecbb231`` with the unit patch applied.

The Rust-enabled, C fallback, and Rust-enabled KASAN images each passed one
single-CPU run and three four-CPU runs. Every Rust-enabled run passed all ten
focused KUnit cases, including the single-index ``ULONG_MAX`` case. Every C
fallback run passed the nine cases used by both implementations. Every run
passed the complete XArray suite with 158825682 of 158825682 tests.

The concurrent case uses three RCU readers and one range writer over a shared
64-index range. It checks loaded values and order-query bounds during updates
and erasure. No validation log contained a BUG, Oops, KASAN report, warning,
call trace, kernel panic, or RCU stall.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with pre-existing cross-reference warnings
in unrelated documents.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* XArray lock acquisition, lock classes, or operation retry rules change.

* Order encoding, sibling encoding, or protected slot access changes.

* The native creation, load, store, or preallocation contracts change.

* A C helper starts to select range policy instead of providing a primitive.

* The fallback starts to use any new Rust range or order symbol.
