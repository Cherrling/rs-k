.. SPDX-License-Identifier: GPL-2.0-only

Native page and folio release
==============================

Introduction
------------

``CONFIG_RUST_PAGE_FOLIO_RELEASE`` selects native Rust for final page and
folio reference release. The option builds on the native Rust VFS and generic
skb waves.

Rust defines four production symbols:

* ``__folio_put()``
* ``folios_put_refs()``
* ``release_pages()``
* ``__folio_batch_release()``

These symbols retain their ordinary export class. Their production ownership
does not depend on a C adapter that calls Rust.

Ownership boundary
------------------

Rust owns these decisions:

* Zone-device, hugetlb, huge-zero, and ordinary-folio branch selection.
* Final-reference selection for single and batched release.
* Reference subtraction for each folio in a batch.
* Batch null-entry filtering and entry compaction.
* Page-cache release lock accumulation across a batch.
* Memory-cgroup uncharge batching and final allocator submission.
* Transparent-union input treatment as an encoded-page array.
* Encoded ``NR_PAGES_NEXT`` parsing and reference counts.
* ``FOLIO_BATCH_SIZE`` refill and tail-batch flush.
* The one-time deferred LRU drain in ``__folio_batch_release()``.

The helper ABI is in ``include/linux/page-folio-release-rust.h``.

C retains these primitives:

* Page, folio, encoded-page, batch, and LRU vector layouts.
* Reference and map counts.
* LRU membership tests, lock acquisition, lock release, and list removal.
* Deferred split queue removal.
* Memory cgroup uncharge operations.
* Zone-device, hugetlb, and huge-zero tests and final release paths.
* Frozen-page and unref-batch allocator entry.
* Page-to-folio conversion and encoded pointer decoding.
* Page allocation, buddy state, per-CPU page lists, and diagnostics.

The helper functions perform only these primitives. They do not choose a
release branch or implement a production entry.

Single-folio release
--------------------

``__folio_put()`` receives a folio that already reached zero references.
Rust performs these steps:

1. If the folio is zone-device memory, call the zone-device release primitive
   and return.
2. If the folio is hugetlb memory, call the hugetlb release primitive and
   return.
3. Remove page-cache and LRU state.
4. Remove deferred-split state.
5. Uncharge the folio from its memory cgroup.
6. Return the frozen pages to the page allocator.

The branch order is part of the contract. A zone-device or hugetlb folio must
not enter ordinary LRU and memory-cgroup release.

Batched release
---------------

``folios_put_refs()`` processes each initialized batch entry in order. Rust
skips a null entry and a huge-zero folio.

For zone-device memory, Rust first releases any accumulated LRU lock, subtracts
the requested references, and calls the zone-device final release only when the
subtraction reaches zero.

For all other folios, Rust subtracts the requested references. A non-final
subtraction ends that entry. A final hugetlb subtraction releases the LRU lock
and calls the hugetlb final release. A final ordinary folio is compacted into
the output part of the same batch.

Rust releases the last accumulated LRU lock. An empty output reinits the batch.
A non-empty output receives one batched memory-cgroup uncharge and one
unref-batch allocator submission. The allocator submission leaves the batch
ready for reuse.

Encoded release
---------------

``release_pages_arg`` is a transparent union. Its C ABI is one array pointer.
Rust receives the same pointer value as an encoded-page array.

Each entry is converted back to its page, then to its head folio. Rust sets the
reference count to one unless ``ENCODED_PAGE_BIT_NR_PAGES_NEXT`` is set. In that
case, the next entry supplies the reference count and is consumed with the
current folio.

Rust fills a local 31-entry batch. A full batch calls ``folios_put_refs()``.
The final partial batch calls the same function. This covers page arrays,
folio arrays, encoded arrays, the exact boundary, and the tail.

Deferred LRU drain
------------------

``__folio_batch_release()`` checks ``percpu_pvec_drained``. If the state is
false, Rust drains deferred LRU additions once and sets the state to true. Rust
then releases the batch through ``folios_put_refs()``.

Batch reuse preserves the drained state. A caller that reinitializes the batch
resets this state through the existing C layout helper.

Failure behavior
----------------

The selected release path allocates no memory and performs no recoverable
operation with an error result. Allocation failure is covered by producing
pages through the page allocator in focused tests. A null batch entry, the
empty output, and a failed allocation are explicit branch cases.

Focused KUnit
-------------

``CONFIG_PAGE_FOLIO_RELEASE_KUNIT_TEST`` enables
``lib/tests/page_folio_release_kunit.c``. Native Rust runs 14 cases. Fallback
and C-only configurations skip the test-only failure injection and run 13
cases. They cover:

* An injected pre-transfer failure that preserves page references.
* Final release of an order-0 page.
* Final release of an order-1 compound folio.
* A non-final reference subtraction.
* Multiple references across a batch.
* A null batch entry and the huge-zero folio.
* ``release_pages()`` with page pointers.
* ``release_pages()`` with folio pointers.
* ``release_pages()`` with encoded page pointers.
* ``NR_PAGES_NEXT`` encoding.
* More than one 31-entry batch and a tail batch.
* Retained and repeated batch drain state.
* Page-cache insertion, LRU drain, removal, and final folio release.
* Four concurrent release workers.

The behavior cases run under native Rust, Rust fallback, and C-only
configurations.

Validation
----------

These configurations passed SMP1 and SMP4:

* Native Rust.
* ``CONFIG_RUST_PAGE_FOLIO_RELEASE=n``.
* ``CONFIG_RUST=n``.
* KASAN with lockdep.
* PREEMPT_RT with lockdep and PROVE_LOCKING.
* DEBUG_VFS with PREEMPT_RT.

These checks also passed:

* Symbol placement in native, fallback, and C-only objects.
* Ordinary export class in ``Module.symvers``.
* External GPL and non-GPL module link tests.
* Reverse disassembly of all four native Rust entries.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Evidence is recorded in ``native-port-coverage.rst``.

Risk and reopening conditions
-----------------------------

A wrong final-reference decision can return a live page to the allocator.
A wrong batch compaction or encoded reference count can release unrelated
memory.

A wrong zone-device, hugetlb, or huge-zero decision can enter an ordinary
release path with incompatible state. A wrong LRU lock lifetime can deadlock
or expose memory without its required lock.

Reopen this boundary decision if any of these conditions changes:

* The page, folio, batch, encoded-page, or LRU vector layout changes.
* Reference subtraction or final-reference semantics change.
* Zone-device, hugetlb, huge-zero, deferred-split, or memory-cgroup release
  ordering changes.
* LRU lock acquisition or batched removal changes.
* Encoded-page flags or ``NR_PAGES_NEXT`` interpretation changes.
* Page allocator free-batch ownership or reuse semantics changes.
