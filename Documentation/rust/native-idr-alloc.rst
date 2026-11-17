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
option enabled, each C entry point forwards directly to the matching Rust
production function.

Rust owns the allocation loops in ``rust/kernel/idr_alloc.rs``. It performs
the retained free-mark search and calls the native Rust XArray node-creation
unit. The selected free slot is then published through the retained IDR slot
replacement boundary.

Retained C boundaries
---------------------

The IDR root marker and its existing malformed-root diagnostic remain in C.
Rust calls the marker helper before each allocation. The negative-start
diagnostic also remains in C.

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

* Symbol checks showing that the enabled C object calls the three Rust entry
  points and that the fallback contains no Rust IDR calls.

Validation results
------------------

Validation on 2026-11-17 used parent commit
``4d1d0c00c9cea38e5eea1a633776f25d7f5f39fb``.

The native Rust, Rust C-fallback, and pure C runs passed all seven focused
KUnit cases on one and four CPUs. Each run also passed the complete Maple Tree
self-test with 3,809,321 of 3,809,321 checks.

KASAN SMP1 and SMP4 runs passed all seven cases without a KASAN report.

The first lockdep boot exposed suspicious RCU reads in the Rust XArray helpers.
IDR used a caller-owned writer lock while the helpers asserted ownership of
``xa_lock``. The dedicated external-writer-lock operations fixed this contract.
Final lockdep SMP1 and SMP4 runs passed all seven cases without a warning.

The concurrent case uses four workers to allocate every ID in a shared
128-entry range. An atomic compare-exchange detects duplicate claims. A hard
timeout bounds startup and allocation, and the test verifies that every slot
was claimed exactly once. No final log contained a BUG, Oops, warning, or call
trace.

In the native configuration, ``lib/idr.o`` references
``rust_idr_alloc_u32``, ``rust_idr_alloc``, and
``rust_idr_alloc_cyclic``. ``rust/kernel.o`` defines all three symbols. The
C-fallback object contains no Rust IDR reference.
The pure C object also contains no Rust IDR reference.

The final source passed ``rustfmtcheck``, a full kernel build with
``CLIPPY=1``, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check``. Focused Sphinx reported only the existing missing
``kernel-docs`` document and undefined ``getting_llvm`` label warnings.

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

* The fallback starts to use any new Rust IDR symbol.
