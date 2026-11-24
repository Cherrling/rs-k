.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray ID allocation
================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_ALLOC`` option selects experimental native Rust
implementations of ``__xa_alloc()`` and ``__xa_alloc_cyclic()``. The option
currently supports x86_64 and depends on native Rust node creation, marked
search, marks, preallocation, and store mutation.

Rust owns the production allocation policy:

* First-fit selection from the free mark.

* Allocation retries after the operation reports ``-ENOMEM``.

* Range-exhaustion handling and ``-EBUSY`` reporting.

* Cyclic search start selection and retry from the range minimum.

* Cyclic wrap notification and next-ID advancement.

The existing C allocation algorithms remain available when the option is
disabled.

Entry points and ownership
--------------------------

The public ``__xa_alloc()`` and ``__xa_alloc_cyclic()`` interfaces and their
``EXPORT_SYMBOL`` export class do not change. With this option enabled, Rust
directly defines both public entry points. Their native C implementations are
omitted, while C retains their export records and the narrow helper
boundaries listed below. The C fallback defines both functions and retains
their original export records.

Rust owns the allocation loops in ``rust/kernel/xarray_alloc.rs``. It calls
the native Rust marked-search, store, mark, and preallocation units instead of
the C allocation algorithms.

Retained C boundaries
---------------------

XArray locking remains C. The public wrappers and callers select plain,
bottom-half, or interrupt-disabled locking. The native allocation functions
require the lock to be held.

Lock dropping for blocking preallocation remains C. The
``rust_xarray_alloc_nomem()`` adapter saves the lock class, releases the lock
only when the allocation flags permit blocking, invokes the native Rust
preallocation unit, and reacquires the same lock class.

Free-mark storage remains C. Rust calls the native mark lifecycle to clear the
selected slot mark and uses the retained root flag helpers for cyclic-wrap
notification.

The restart-state helper is shared with IDR allocation. It is built when
either ``CONFIG_RUST_XARRAY_ALLOC`` or ``CONFIG_RUST_IDR_ALLOC`` is enabled.

The ``EXPORT_SYMBOL`` records remain in C metadata. The automatic Rust GPL
exporter skips both direct entries to avoid duplicate exports.

The node and tagged-entry layouts remain C. Rust calls the existing value,
node, sibling, zero, restart, and advanced-entry classifiers and encoders.

Node allocation, root publication, accounting propagation, and RCU
reclamation remain C primitives used by the native creation and store paths.

Important invariants
--------------------

The caller holds the XArray lock during slot selection and publication. A
blocking preallocation may release the lock only through the retained
adapter. The adapter reacquires the lock before Rust examines the result.

A normal allocation must use an XArray with ``XA_FLAGS_TRACK_FREE``. Advanced
entries and XArrays without free tracking keep their existing diagnostics and
return ``-EINVAL``.

A NULL allocation entry uses the advanced zero-entry encoding. It reserves
the selected index and remains visible as NULL through the normal load API.

When marked search reaches the end of the requested range, the cursor enters
the restart state and the allocation returns ``-EBUSY``. The caller-provided
ID remains unchanged in this case.

Cyclic allocation first searches at the maximum of the range minimum and the
provided next ID. If that search exhausts the range, Rust retries from the
range minimum. A successful retry reports wrap.

If the next ID wraps from ``U32_MAX`` to zero, the XArray records a pending
wrap notification. The next successful cyclic allocation reports that wrap
once and clears the notification.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled, C fallback, and Rust-disabled C-only x86_64 LLVM builds with
  one and four virtual CPUs.

* Focused KUnit cases for first-fit holes, reserved entries, range
  exhaustion, cyclic retry from the minimum, the maximum ID, and concurrent
  allocation.

* The same runs with native Rust allocation enabled under KASAN, lockdep, and
  PREEMPT_RT.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that Rust defines both public entry points, the native
  C object defines neither, and the fallback defines each one.

Validation results
------------------

This unit was validated on 2026-11-25 against ``adc270b6fa65`` with the unit
patch applied.

The native, C fallback, C-only, KASAN, lockdep, and PREEMPT_RT builds each
passed one single-CPU run and one four-CPU run. Every run passed all six
focused KUnit cases and the complete XArray suite with 158825682 of 158825682
tests.

The concurrent case uses four workers to allocate every ID in a shared
128-entry range. The test detects duplicate claims and allocation failures.
No validation log contained a BUG, Oops, KASAN report, warning, call trace,
kernel panic, or RCU stall.

Symbol checks showed that ``rust/kernel.o`` defines both entry points in each
native configuration. The corresponding ``lib/xarray.o`` retains only
unresolved references for the export records. The fallback and C-only
``lib/xarray.o`` define both functions. Every configuration retains the
original ``EXPORT_SYMBOL`` class.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Strict checkpatch reported no errors, warnings, or checks. Focused Sphinx
generation completed with two unrelated cross-reference warnings in
``rust/index.rst`` and ``rust/quick-start.rst``.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* XArray lock acquisition, lock classes, or blocking preallocation changes.

* Free-mark storage, range bounds, or tagged-entry encoding changes.

* The native marked-search, store, mark, or preallocation contracts change.

* A C helper starts to select allocation policy instead of providing a
  primitive operation.

* The cyclic wrap notification or next-ID rules change.

* The fallback starts to depend on a Rust XArray allocation symbol.
