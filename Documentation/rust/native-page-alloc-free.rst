.. SPDX-License-Identifier: GPL-2.0-only

Native page allocation and free policy
=======================================

Introduction
------------

``CONFIG_RUST_PAGE_ALLOC_FREE`` selects native Rust for the public page
allocation and free policy. The option builds on the native Rust page and
folio release boundary.

Rust defines seven production symbols:

* ``__alloc_pages_noprof()``
* ``alloc_pages_node_noprof()``
* ``__folio_alloc_noprof()``
* ``__free_pages()``
* ``free_pages_nolock()``
* ``free_pages()``
* ``alloc_pages_nolock_noprof()``

The four ordinary exports and the GPL-only
``alloc_pages_nolock_noprof()`` export retain their export classes.
``__alloc_pages_noprof()`` and ``free_pages_nolock()`` remain
non-exported.

Ownership boundary
------------------

Rust owns these decisions:

* ``NUMA_NO_NODE`` fallback selection for the two public convenience paths.
* Offline-node warning placement before an explicit-node allocation.
* Attachment of the first page reference after a frozen allocation.
* Rollback to the frozen allocator when attachment cannot complete.
* Compound flag addition and rmappable-folio conversion.
* The final-reference decision on page free.
* The compound-head and non-compound-tail free branches.
* Allocation-tag adjustment and descending tail-subtree release order.
* The normal and no-lock frozen-free policy.
* Zero-address filtering and virtual-address validation order.
* The no-lock node fallback and constrained allocation policy.

C retains these primitives:

* The buddy allocator, per-CPU page lists, watermarks, reclaim, and OOM paths.
* Page, folio, node-mask, zone, and allocation-context layouts.
* Frozen allocation and direct free operations.
* Page reference, page-head, allocation-tag, and rmappable primitives.
* NUMA topology lookup and offline-node diagnostics.
* Architecture virtual-address validation and page conversion.
* Tracepoints, memory-cgroup charging, KMSAN, page owner, and diagnostics.

The helper ABI is in ``include/linux/page-alloc-free-rust.h``. The helpers
perform only allocator, layout, and primitive operations. They do not define a
production allocation or free entry.

Allocation
----------

``__alloc_pages_noprof()`` passes the original allocation flags to Rust. Rust
requests a frozen page tree from the buddy allocator. A failed request returns
null. A successful request receives its first page reference, then returns.

If focused test failure injection rejects the reference attachment, Rust
returns the still-frozen page tree to the allocator and returns null. This
rollback is the normal failure policy for a post-allocation setup failure.

``alloc_pages_node_noprof()`` replaces ``NUMA_NO_NODE`` with the current
memory node. It applies the original offline-node warning, then calls the
default allocation policy.

``__folio_alloc_noprof()`` adds ``__GFP_COMP`` before allocation. Rust then
marks a large result as rmappable. Null results pass through unchanged.

``alloc_pages_nolock_noprof()`` replaces ``NUMA_NO_NODE`` with the current
CPU node and selects ``ALLOC_NOLOCK``. The frozen allocator retains its
existing no-blocking, zeroed, best-effort contract.

Free
----

``__free_pages()`` and ``free_pages_nolock()`` first save the page-head state
and allocation tag. They then subtract one reference.

A final reference releases the complete frozen tree. A non-final reference on
a compound head leaves the allocation allocated. A non-final reference on a
non-compound high-order allocation subtracts the remaining tail-page tags.
Rust then frees each descending tail subtree and leaves the head allocated.

``free_pages()`` ignores address zero. A nonzero address passes through the
original architecture validation before conversion to a page. The normal free
policy then runs.

Focused KUnit
-------------

``CONFIG_PAGE_ALLOC_FREE_KUNIT_TEST`` enables
``lib/tests/page_alloc_free_kunit.c``. Native Rust runs 14 cases. Fallback and
C-only configurations run 12 cases. They cover:

* Injected allocation rejection and allocation-tag recovery.
* Injected post-allocation reference-attachment rejection and rollback.
* Order-0 allocation and final free.
* Non-compound order-1 allocation and final free.
* Compound order-1 folio allocation, rmappable state, and final release.
* ``NUMA_NO_NODE`` fallback and an explicit current node.
* Opportunistic no-lock allocation and no-lock free.
* A non-final page reference.
* Non-compound high-order tail release with a retained head reference.
* Virtual-address free and the zero-address no-op.
* Page-cache insertion and removal across the allocation and release layers.
* Four concurrent workers that allocate and free order-0 and order-1 pages.

Validation
----------

These configurations passed SMP1 and SMP4:

* Native Rust with 14 of 14 cases.
* Rust fallback with 12 of 12 cases.
* C-only with 12 of 12 cases.
* KASAN with lockdep and 14 of 14 cases.
* PREEMPT_RT with lockdep and ``PROVE_LOCKING`` with 14 of 14 cases.
* DEBUG_VFS with PREEMPT_RT and 14 of 14 cases.

Native ``rust/kernel.o`` defines all seven production entries. Fallback and
C-only ``mm/page_alloc.o`` retain all seven definitions. Native
``Module.symvers`` contains four ordinary exports and one GPL-only export.
The two remaining entries are absent.

External GPL and non-GPL modules linked the five exports that match their
license. Modpost rejected a non-GPL module that used the GPL-only export.
Both license classes rejected the two non-exported entries. Reverse
disassembly shows Rust implementations for all seven entries.

Rustfmt, Clippy, Rustdoc, focused Sphinx, and ``git diff --check`` pass.
Strict checkpatch reports the new-file path warning and the retained
``VM_BUG_ON()`` warning in the virtual-address helper.

Risk and reopening conditions
-----------------------------

A wrong reference-attachment or final-free decision can return a live page to
the allocator. A wrong non-compound tail decision can leak or double-free a
partial high-order allocation. A wrong NUMA fallback can allocate from an
unexpected node.

Reopen this boundary if page references, allocation tags, frozen-allocation
ownership, tail-page state, virtual-address validation, rmappable folio state,
or no-lock allocator policy changes.
