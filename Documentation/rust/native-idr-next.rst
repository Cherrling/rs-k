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
option enabled, Rust directly defines ``idr_get_next_ul()`` and
``idr_get_next()``. The native C object retains only their export metadata
and the narrow helper boundaries listed below. In the fallback, C defines
both functions and retains their original export records.

Rust initializes an XArray cursor at the normalized start index and uses the
native Rust forward-search unit. It owns every loop decision that controls
where iteration resumes.

Retained C boundaries
---------------------

The IDR and XArray root layouts remain in C. The signed-ID overflow diagnostic
also remains in C.

The original GPL and non-GPL export classes remain in C metadata. The
automatic Rust GPL exporter skips these direct entries to avoid duplicate
exports.

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

* Symbol checks showing that Rust defines both public entry points, the native
  C object defines neither, and the fallback defines each one.

Validation results
------------------

Validation on 2026-11-25 used parent commit
``9dbe9972e10f2098631eef434960f4395fc7ac21``. All builds used LLVM.

Native Rust, Rust C-fallback, and pure C runs passed all six focused iteration
cases on one and four CPUs. The same six runs passed all seven allocation
cases. KASAN, lockdep, and PREEMPT_RT each passed both suites on one and four
CPUs. No final log contained a KASAN report, lockdep or RCU report, BUG,
warning, Oops, kernel panic, or RCU stall.

A lockdep four-CPU adjacent run passed all eight Maple Tree suites, with 56 of
56 cases passing.

The iteration tests use aligned ``u32`` storage for IDR values. Scalar
variables with unaligned addresses can resemble encoded internal XArray
entries and are not valid IDR values.

In the native configuration, ``rust/kernel.o`` directly defines
``idr_get_next()`` and ``idr_get_next_ul()``. ``lib/idr.o`` defines neither
symbol and retains only the references needed by their export records. The
C-fallback and pure C objects define both symbols, and the Rust fallback
object defines neither.

The final source passed ``rustfmtcheck``, a full kernel build with
``CLIPPY=1``, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check``. Focused Sphinx rendered both IDR pages and reported only
the existing missing ``kernel-docs`` document warning outside this scope.

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

* The fallback starts to depend on a Rust IDR iteration symbol.
