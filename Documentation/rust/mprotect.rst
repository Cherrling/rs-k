.. SPDX-License-Identifier: GPL-2.0

Native Rust Memory Protection
=============================

Status and scope
----------------

The ``CONFIG_RUST_MPROTECT`` option selects native Rust for the production
``mprotect()`` and ``pkey_mprotect()`` paths. The original C implementation
remains available as a fallback.

Rust owns these production decisions:

1. Request untagging, growth-flag validation, and address alignment checks.
2. Length rounding, overflow detection, and architecture protection checks.
3. Killable mmap write-lock acquisition and release.
4. Pkey allocation validation.
5. Initial VMA lookup and ``PROT_GROWSDOWN`` or ``PROT_GROWSUP`` selection.
6. Previous-VMA selection and iterator repositioning.
7. The complete VMA loop and contiguous-range validation.
8. Per-VMA access, write-execute, and architecture flag validation ordering.
9. Security hook, filesystem callback, and fixup ordering.
10. Protection conversion, commit accounting, VMA modification, and rollback.
11. Page-protection updates, population decisions, and mm statistics ordering.
12. Success and error paths after the TLB gather starts.

The C syscall adapter creates one private state object. The object contains the
current mm, request arguments, VMA iterator, TLB gather state, previous VMA,
and per-VMA flag projections. Rust holds one pointer to this object for the
complete request.

The exported ``mprotect_fixup()`` entry remains for ``setup_arg_pages()``. Its
C adapter initializes the same private state and calls the shared Rust fixup
implementation. The adapter does not duplicate the fixup decisions.

Retained C boundary
-------------------

Each helper performs one access, predicate, conversion, or existing operation.
The retained boundary contains:

* Scalar field access for the request and private state.
* Mmap write-lock acquisition and release.
* Architecture protection and VMA flag validation.
* Legacy VMA flags and typed VMA flags conversion.
* VMA iterator initialization, lookup, previous lookup, and range-end access.
* VMA start, end, growth, execute, seal, and anonymous-account predicates.
* PFN permission page-table walks.
* Security and filesystem protection callbacks.
* VMA modification and flag-reset primitives.
* Page-protection updates, TLB operations, population, accounting, and perf.

These helpers do not select the request flow, choose an error, or order a
cleanup path.

Ordering and invariants
-----------------------

Only one VMA request is active for a state object. The mmap write lock protects
the VMA tree, VMA flags, and page protections.

Rust validates the request before taking the mmap write lock. An empty request
returns success without locking. A length overflow returns ``-ENOMEM``.
Conflicting growth flags, an unaligned address, or an invalid protection value
returns ``-EINVAL``.

After locking, Rust rejects an unallocated pkey before it looks up a VMA. A
missing VMA or a range hole returns ``-ENOMEM``. A growth request against a VMA
without the matching growth property returns ``-EINVAL``.

The initial lookup positions the VMA iterator. ``vma_prev()`` may move that
iterator to a preceding VMA. Rust must call the range lookup again before the
production loop. This restores the iterator to the VMA selected by the growth
policy.

For each VMA, Rust prepares the new flags before the access, write-execute,
architecture, and security checks. A filesystem ``mprotect`` callback runs
after those checks and before the fixup. Failed fixup accounting is rolled back
before returning.

The fixup records old flags before any change. It applies the sealed-VMA check,
same-flags shortcut, PFN check, and commit accounting in that order. A
successful modification updates the previous-VMA state, VMA flags, page
protection, mm statistics, and perf state.

``TLB`` completion runs after the loop succeeds or fails. The final short-range
check reports ``-ENOMEM`` only when the loop itself did not fail.

Testing
-------

The ``mprotect`` userspace suite covers:

* Read, write, execute, and ``PROT_NONE`` transitions.
* Fault recovery and resident-data preservation.
* Unaligned addresses, empty ranges, length overflow, and growth conflicts.
* A range hole returning ``-ENOMEM``.
* Middle-range split, permissions, fault behavior, and restoration merge.
* ``PROT_GROWSDOWN`` expansion and invalid growth attributes.
* Allocated, freed, and unavailable pkeys.
* Sealed VMA rejection with ``-EPERM``.
* Locked-page population when a VMA becomes writable.
* File-backed writable protection checks.
* Four concurrent workers that repeatedly split, merge, and restore VMAs.

Validation
----------

The development branch passed the following configurations:

* Native Rust: three consecutive suite runs.
* C fallback: three consecutive suite runs.
* KASAN: three consecutive suite runs.
* Lockdep: three consecutive suite runs.
* PREEMPT_RT: three consecutive suite runs.

The native Rust kernel also passed three consecutive ``brk`` suite runs with
``CONFIG_USERFAULTFD`` enabled. The complete ``brk`` suite includes userfaultfd
unmap, initial growth, registration, and shrink paths.

A TCG run with ``-cpu max`` enabled PKU and exercised the allocated pkey path.
The test allocated a pkey, applied it with ``pkey_mprotect()``, freed it, and
checked that the freed key was rejected.

The native Rust kernel passed three consecutive runs of each adjacent mm/VMA
suite:

* ``mmap_entry``
* ``vma_merge``
* ``vma_unmap``
* ``dup_mmap``
* ``exit_mmap``

Run one suite with:

.. code-block:: sh

   tools/testing/selftests/mprotect/run.sh "$srctree" "$builddir"

Enable PKU for the pkey allocation path with:

.. code-block:: sh

   QEMU_ACCEL=tcg QEMU_CPU=max \
       tools/testing/selftests/mprotect/run.sh "$srctree" "$builddir"

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The mmap write-lock contract changes.
* VMA iterator lookup or previous lookup semantics change.
* Growth-policy selection changes.
* Pkey allocation or architecture override rules change.
* Security or filesystem callback ordering changes.
* Commit accounting, locked-page population, or mm statistics change.
* A helper starts selecting a request outcome or a cleanup branch.
