.. SPDX-License-Identifier: GPL-2.0

Native Rust Program Break
=========================

Status and scope
----------------

The ``CONFIG_RUST_BRK`` option selects native Rust for the production
``brk()`` syscall path. The original C implementation remains available as a
fallback.

Rust owns these production decisions:

1. The killable mmap write-lock failure result.
2. The minimum break when ``COMPAT_BRK`` policy applies.
3. Data-segment limit rejection.
4. Page-aligned transition selection.
5. Same-page, shrink, and expansion dispatch.
6. The stack-gap conflict decision.
7. Success and failure lock-release paths.
8. Userfaultfd completion and locked mapping population.

The C syscall adapter creates one private state object, initializes its
userfaultfd list, and passes the requested break to ``rust_brk()``.

Retained C boundary
-------------------

Each helper performs one operation or exposes one narrow fact. The retained
boundary contains:

* Current-address-space and break-field projections.
* Mmap write-lock acquisition and release.
* ``COMPAT_BRK`` randomization state.
* The existing data-limit, unmapped-area, and locked-memory checks.
* VMA iterator initialization and maple-tree lookup.
* Stack-gap and VMA start projections.
* The existing ``do_brk_flags()`` and aligned-unmap operations.
* Break-field writes.
* Userfaultfd completion and ``mm_populate()``.

The helpers do not choose a transition, select an error, or order a cleanup
path. Rust performs those decisions and calls the helpers in the required
order.

Ordering and invariants
-----------------------

The syscall takes the mmap write lock with a killable acquisition. A lock
failure returns ``-EINTR`` without touching the address space.

Rust rejects a requested break below the applicable minimum and rejects a
request that exceeds the data limit. Both failures restore the original exact
break and release the lock.

When the requested and current breaks round to the same page, Rust updates the
exact break without changing mappings. A lower requested break selects the
shrink path. A higher requested break selects the expansion path.

The shrink path finds the VMA that overlaps the old page-aligned break. It
restores the original exact break if no suitable VMA exists. Otherwise it
records the requested break before the aligned unmap. A successful unmap
releases the write lock itself. Rust then completes userfaultfd events.

The expansion path checks the requested range before selecting a VMA. It
rejects a nearby next VMA according to the stack-gap rule. After expansion,
Rust records the exact requested break, releases the lock, completes
userfaultfd events, and populates the new range when locked mappings are the
default.

Testing
-------

The ``brk`` userspace suite covers:

* Exact updates within one page.
* Page-aligned growth and multi-page shrink.
* ``mincore()`` rejection for pages removed by a shrink.
* Requests below the minimum break.
* ``RLIMIT_DATA`` rejection with an unchanged break.
* Rejection of expansion into an adjacent mapping.
* Population caused by ``mlockall(MCL_FUTURE)``.
* Userfaultfd unmap notification after a shrink.

The adjacent ``vma_merge`` and ``vma_unmap`` suites also exercise break growth
and shrink through VMA merge and unmap paths.

Validation
----------

The native path was validated on 2026-11-16 against baseline
``ea6f598d3ac1``. Six x86_64 configurations built successfully: native
Rust, C fallback, ``CONFIG_COMPAT_BRK``, KASAN, lockdep, and PREEMPT_RT.
The ``brk`` suite passed three times in each configuration for 18/18 runs.
The native configuration also passed with KASLR enabled and disabled.

The adjacent ``vma_merge``, ``vma_unmap``, ``mmap_entry``, ``dup_mmap``,
and ``exit_mmap`` suites passed three times each. A scan of all runtime logs
found no failure, KASAN report, lockdep report, warning, or kernel panic.

The five native configurations each contain one ``rust_brk`` entry and 20
``rust_helper_brk_*`` helpers. The fallback configuration contains neither.
All six configurations have no undefined ``vmlinux`` symbols. The x86_64
syscall wrapper calls ``rust_brk`` in each native configuration.

Clippy, rustdoc, rustfmtcheck, strict Sphinx documentation, and strict
checkpatch validation passed.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The mmap write-lock acquisition or release contract changes.
* ``COMPAT_BRK`` minimum selection changes.
* Data-limit or locked-memory validation changes.
* The stack-gap rule changes.
* Aligned unmap or userfaultfd completion ordering changes.
* A helper starts selecting a transition, error, or cleanup branch.
