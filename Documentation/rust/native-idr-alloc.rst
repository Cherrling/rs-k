.. SPDX-License-Identifier: GPL-2.0

Native Rust IDR allocation
==========================

Status and scope
----------------

The ``CONFIG_RUST_IDR_ALLOC`` option selects native Rust implementations of
``idr_alloc_u32()``, ``idr_alloc()``, and ``idr_alloc_cyclic()``.

Rust owns the production allocation policy:

* Base-relative range normalization.

* First-fit free-slot selection and reuse of holes.

* Inclusive and exclusive range conversion.

* Range-exhaustion handling and ``-ENOSPC`` reporting.

* Cyclic search start selection and retry from the range minimum.

* Next-ID publication ordering and placement-cursor advancement.

* Allocation retries when node creation reports ``-ENOMEM``.

The original C allocation algorithms remain available when the option is
disabled.

Entry points and ownership
--------------------------

The public IDR interfaces and their export classes do not change. With this
option enabled, Rust directly defines ``idr_alloc_u32()``, ``idr_alloc()``,
and ``idr_alloc_cyclic()``. The native C object retains only their export
metadata and the narrow helper boundaries listed below. In the fallback, C
defines all three functions and retains their original export records.

Rust owns the allocation loops in ``rust/kernel/idr_alloc.rs``. It performs
the retained free-mark search and calls the native Rust XArray node-creation
unit. The selected free slot is then published through the retained IDR slot
replacement boundary.

Retained C boundaries
---------------------

The IDR root marker and its existing malformed-root diagnostic remain in C.
Rust calls the marker helper before each allocation. The negative-start
diagnostic also remains in C.

The original GPL and non-GPL export classes remain in C metadata. The
automatic Rust GPL exporter skips these direct entries to avoid duplicate
exports.

IDR slot replacement and accounting remain C. An IDR can store a NULL value
as an allocated ID. The replacement boundary therefore uses the existing
IDR-aware count calculation and clears the free tag. Calling the general
XArray store accounting would not preserve this IDR rule.

The XArray node and tagged-entry layouts remain C. Rust uses the existing
classifiers for tree nodes, value entries, sibling entries, restart state,
and cursor errors.

Node allocation, root publication, mark propagation, empty-node deletion, and
RCU reclamation remain XArray primitives. Rust owns the policy that decides
when these primitives are called.

Caller locking remains unchanged. IDR callers provide synchronization for
writers and RCU protection for readers where required.

IDR writers do not necessarily hold the XArray-internal ``xa_lock``. The Rust
marked-search and node-creation operations therefore have explicit
external-writer-lock variants. These variants use raw RCU loads after the
caller asserts that its own writer lock excludes concurrent XArray changes.
This rule matches the existing C ``idr_get_free()`` boundary. Ordinary XArray
operations continue to use the lockdep-checked slot access helpers.

Important invariants
--------------------

A zero upper bound in ``idr_alloc()`` means ``INT_MAX``. A negative start
returns ``-EINVAL`` and does not change the IDR.

The base-relative search clamps the requested ID to zero when it is below the
IDR base. The maximum comparison remains inclusive for ``idr_alloc_u32()``
and uses an exclusive public end for the other two entry points.

Before slot publication, Rust writes the selected public ID to the caller's
ID. If publication fails, Rust restores the caller's original value. This
ordering guarantees that an RCU lookup cannot observe an entry with an
uninitialized embedded ID.

An allocated NULL entry is different from a free slot. It is not returned as
an allocated object by a successful search, and a later allocation must treat
its index as occupied.

Hole reuse depends on the free mark becoming visible again when an entry is
removed. The allocation path clears the mark only after successful insertion.

A cyclic allocation first searches from the maximum of the range minimum and
the stored next ID. If the range is exhausted, Rust retries from the range
minimum. A successful allocation stores the next cursor value with unsigned
wraparound.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for base-relative ranges, first-fit holes, allocated
  NULL entries, zero-ended ranges, cyclic wrap, 256 sequential IDs, and
  concurrent allocation.

* The same test runs with native Rust IDR allocation disabled.

* The same test runs with ``CONFIG_RUST`` disabled.

* The same Rust-enabled runs with KASAN enabled.

* The same Rust-enabled runs with lockdep enabled.

* The complete Maple Tree self-test in every focused boot.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that Rust defines all three public entry points, the
  native C object defines none of them, and the fallback defines each one.

Validation results
------------------

Validation on 2026-11-25 used parent commit
``9dbe9972e10f2098631eef434960f4395fc7ac21``. All builds used LLVM.

Native Rust, Rust C-fallback, and pure C runs passed all seven focused
allocation cases on one and four CPUs. The same six runs passed all six
iteration cases. KASAN, lockdep, and PREEMPT_RT each passed both suites on one
and four CPUs. No final log contained a KASAN report, lockdep or RCU report,
BUG, warning, Oops, kernel panic, or RCU stall.

A lockdep four-CPU adjacent run passed all eight Maple Tree suites, with 56 of
56 cases passing.

The concurrent case uses four workers to allocate every ID in a shared
128-entry range. An atomic compare-exchange detects duplicate claims. A hard
timeout bounds startup and allocation, and the test verifies that every slot
was claimed exactly once. No final log contained a BUG, Oops, warning, or call
trace.

In the native configuration, ``rust/kernel.o`` defines all five public IDR
allocation and iteration symbols. ``lib/idr.o`` defines none of them and has
only the references needed by their export records. The C-fallback and pure C
objects define all five symbols, and the Rust fallback object defines none.

The native, fallback, and pure C ``Module.symvers`` files preserve the original
classes. ``idr_alloc`` and ``idr_alloc_u32`` use ``EXPORT_SYMBOL_GPL``.
``idr_alloc_cyclic``, ``idr_get_next``, and ``idr_get_next_ul`` use
``EXPORT_SYMBOL``. The old ``rust_idr_alloc*`` and ``rust_idr_get_next*``
adapter names are absent. The final ``vmlinux`` has no unresolved IDR symbol.

The final source passed ``rustfmtcheck``, a full kernel build with
``CLIPPY=1``, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check``. Focused Sphinx rendered both IDR pages and reported only
the existing missing ``kernel-docs`` document warning outside this scope.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* IDR base, range, NULL-entry, or cyclic-cursor rules change.

* IDR slot accounting or free-mark behavior changes.

* XArray node creation, marked search, or cursor error contracts change.

* The external writer-lock contract for IDR access changes.

* The malformed-IDR or negative-start diagnostics change.

* A C helper starts to select allocation policy instead of providing a
  primitive operation.

* The fallback starts to depend on a Rust IDR symbol.
