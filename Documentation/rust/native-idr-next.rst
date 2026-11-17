.. SPDX-License-Identifier: GPL-2.0

Native Rust IDR iteration
=========================

Status and scope
----------------

The ``CONFIG_RUST_IDR_NEXT`` option selects native Rust implementations of
``idr_get_next_ul()`` and ``idr_get_next()``.

Rust owns the production iteration policy:

* Base-relative cursor normalization.

* Selection of the first visible entry at or after the requested ID.

* Sparse-tree and empty-subtree traversal control.

* Retry recovery for concurrently replaced XArray entries.

* Acceptance of value entries from live nodes.

* Placement of the public ID into the caller's cursor.

* The signed-ID overflow check used by ``idr_get_next()``.

The original C iteration algorithms remain available when the option is
disabled.

Entry points and ownership
--------------------------

The public IDR interfaces and their export classes do not change. With this
option enabled, both C entry points forward directly to the matching Rust
production function.

Rust initializes an XArray cursor at the normalized start index and uses the
native Rust forward-search unit. It owns every loop decision that controls
where iteration resumes.

Retained C boundaries
---------------------

The IDR and XArray root layouts remain in C. The signed-ID overflow diagnostic
also remains in C.

RCU slot access and parent lookup remain C primitives. Rust decides when to
perform a lookup, when to descend, and when to restart after a retry entry.
IDR iteration uses the raw RCU load boundary because the public API permits
either an RCU read lock or a caller-owned lock that excludes writers. This
matches the retained radix-tree iterator contract and avoids asserting
ownership of the XArray-internal ``xa_lock``.

Entry encoding remains C. Rust uses the retained classifiers for node,
sibling, retry, and internal entries. A non-retry internal entry is visible
only when the cursor identifies a live node, matching the radix-tree iterator
contract.

Caller synchronization remains unchanged. Readers use the RCU read lock or
exclude concurrent writers as required by the public IDR API.

Important invariants
--------------------

If the requested ID is below the IDR base, iteration starts at the base. If it
is at or above the base, Rust searches at the corresponding base-relative
index.

The requested ID itself is included in the search. A successful iteration
returns the first visible entry with an index greater than or equal to that
ID.

Allocated NULL entries are not returned by the radix-tree iterator and do not
hide later visible entries.

Value entries from a live leaf node are valid iteration results. Retry entries
restart the search at the current index under the caller's synchronization.

On success, Rust updates the caller's cursor with the public ID. On failure,
the cursor is unchanged.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit cases for sparse traversal, base-relative starts, holes,
  allocated NULL entries, live value entries, signed wrappers, and 256
  sequential IDs.

* The same tests with native Rust IDR iteration disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* The same Rust-enabled runs with KASAN enabled.

* The same Rust-enabled runs with lockdep enabled.

* The complete Maple Tree self-test in every focused boot.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks showing that the enabled C object calls both Rust entry points
  and that the fallback contains no Rust IDR iteration calls.

Validation results
------------------

Validation on 2026-11-17 used parent commit
``bd6eae17399e``.

The native Rust, Rust C-fallback, and pure C runs passed all seven focused
IDR allocation cases and all six IDR iteration cases on one and four CPUs.
Each run also passed the complete Maple Tree self-test with 3,809,321 of
3,809,321 checks.

KASAN SMP1 and SMP4 runs passed both IDR suites without a KASAN report.

The first lockdep boot reported suspicious RCU reads in the native XArray
search path from callers such as generic-netlink registration and workqueue
CPU online. Those callers use a caller-owned lock instead of ``xa_lock``.
The public IDR API permits this synchronization, and the retained
radix-tree iterator uses raw RCU loads for the same contract. Final SMP1 and
SMP4 lockdep boots passed without an RCU warning after the Rust IDR path
selected the corresponding external-lock load variants.

The iteration tests use aligned ``u32`` storage for IDR values. Scalar
variables with unaligned addresses can resemble encoded internal XArray
entries and are not valid IDR values.

In the native configuration, ``lib/idr.o`` references
``rust_idr_get_next`` and ``rust_idr_get_next_ul``.
``rust/kernel.o`` defines both symbols. The Rust C-fallback and pure C
objects contain neither Rust iteration reference.

The final source passed ``rustfmtcheck``, a full kernel build with
``CLIPPY=1``, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check``. Strict checkpatch reported no errors. Two expected
messages were reviewed and ignored: the added-file warning is covered by the
updated ``MAINTAINERS`` entries, and ``idr_init_base()`` is required to test
the public base behavior. Focused Sphinx reported only the existing missing
``kernel-docs`` document warning.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* IDR base or cursor semantics change.

* IDR NULL-entry visibility changes.

* XArray retry, value-entry, or internal-entry rules change.

* XArray forward-search cursor contracts change.

* The signed-ID overflow diagnostic changes.

* A C helper starts to select iteration policy instead of providing a
  primitive operation.

* The fallback starts to use a new Rust IDR iteration symbol.
