.. SPDX-License-Identifier: GPL-2.0

Native Rust User Mapping Entry
==============================

Status and scope
----------------

The ``CONFIG_RUST_MMAP_ENTRY`` option selects native Rust for the production
``do_mmap()`` path. The original C implementation remains available as a
fallback.

Rust owns these production decisions:

1. Population-output initialization and mmap lock verification.
2. Empty-length rejection and read-implies-exec policy.
3. Fixed-no-replace handling and hint rounding.
4. Page alignment, page-offset overflow, and map-count checks.
5. Execute-only protection-key selection and clamping.
6. Unmapped-area selection and address-error propagation.
7. Fixed-address collision rejection.
8. Locked-memory permission and future-limit checks.
9. File mapping access, flags, no-exec, seal, and argument checks.
10. Anonymous private, shared, and droppable mapping policy.
11. Reservation policy for normal and huge-page files.
12. Region creation and populated-range selection.

Retained C boundary
-------------------

The C adapter initializes one opaque mapping state. Rust owns control flow and
policy. Each helper exposes one field projection, one predicate, one fixed
operation, or one existing mmap operation.

The retained boundary contains:

* Current-mm, request, VMA-flag, and file-fact projections.
* Mmap write-lock assertion and personality inspection.
* Fixed-flag insertion, hint rounding, and page alignment.
* Page-offset overflow and map-count predicates.
* Architecture execute-only pkey selection.
* VMA flags derived from protection, file flags, mm defaults, and pkey.
* Unmapped-area selection and address-error inspection.
* VMA intersection, mlock permission, and mlock-limit predicates.
* File size, mode, seal, and mmap-support predicates.
* VMA flag set and clear operations.
* The existing VMA region insertion operation.

The C boundary does not select an error, mapping type, file policy, or
population policy. It exposes facts and performs fixed operations requested by
Rust.

Ordering and invariants
-----------------------

Rust clears the population output and verifies the mmap write lock before it
accesses request state. A zero length returns ``-EINVAL`` before address
processing.

Read permission gains execute permission under ``READ_IMPLIES_EXEC``. A file
on a no-exec mount suppresses that implication. ``MAP_FIXED_NOREPLACE`` gains
``MAP_FIXED`` before hint processing and address selection.

Rust rejects an aligned zero length with ``-ENOMEM``. Page-offset overflow
returns ``-EOVERFLOW``. Excessive VMA counts return ``-ENOMEM`` before VMA
creation.

For file mappings, Rust preserves the original shared, validated-shared, and
private policy order. A legacy shared request first removes non-legacy flags.
A validated-shared request checks those flags. Shared requests then flow
through the common private access checks.

For anonymous mappings, Rust selects private, shared, or droppable policy from
the mapping type. A droppable mapping keeps its private-equivalent access and
offset behavior. It rejects locked or explicit huge-page combinations before
region creation.

``MAP_NORESERVE`` sets the VMA reservation flag when overcommit policy permits
it. A huge-page file always sets the flag.

Rust calls the existing region insertion operation once. A successful locked
or populated request sets the output length. All other paths leave it zero.

Testing
-------

The ``mmap_entry`` userspace suite covers:

* Zero and overflowing lengths.
* Anonymous private and shared mappings.
* Fixed replacement and ``MAP_FIXED_NOREPLACE`` collisions.
* Hint rounding at the mmap minimum address.
* Read-implies-exec and no-exec mount policy.
* Execute-only mappings.
* File read and write mode failures.
* Unsupported validated-shared flags.
* Page-offset overflow and memfd write seals.
* Population and nonblocking population behavior.
* Locked memory, droppable mappings, and invalid droppable combinations.
* Reservation behavior under both normal and never-overcommit policy.
* Locked-memory permission and limit failures.
* Map-count rejection.

Validation record
-----------------

The migration gates passed on 2026-11-15 against baseline commit
``0943769836de``. All configurations use ``CONFIG_WERROR=y`` and LLVM.

The production Rust image, the C fallback image, a KASAN image, a lockdep
image, and a PREEMPT_RT image built successfully. The KASAN image uses
``CONFIG_KASAN_GENERIC=y`` and ``CONFIG_KASAN_INLINE=y``. The lockdep image
uses ``CONFIG_PROVE_LOCKING=y`` and ``CONFIG_LOCKDEP=y``.

The ``mmap_entry`` userspace suite passed three times in each of the five
configurations. The suite exercised the complete request path, including the
new mixed read-execute mapping case.

The production image passed the ``copy_vma``, ``dup_mmap``, ``exit_mmap``,
``vma_unmap``, and ``vma_merge`` suites three times each. Log scans found no
``BUG``, ``WARNING``, KASAN report, lockdep report, RCU stall, kernel panic, or
call trace.

The test program compiled with GCC and Clang using
``-Wall -Wextra -Werror``. Rust format, Clippy, and documentation checks
passed with ``rustfmtcheck``, ``CLIPPY=1``, and ``rustdoc``.

The production ``System.map`` contains ``rust_mmap_entry``. Disassembly shows
that ``do_mmap()`` calls it directly. The fallback ``System.map`` does not
contain ``rust_mmap_entry``.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Mmap lock ownership or the initial population-output rule changes.
* Read-implies-exec or no-exec path policy changes.
* Fixed-address, hint, alignment, overflow, or map-count ordering changes.
* Execute-only pkey selection or VMA flag derivation changes.
* Unmapped-area selection or address-error semantics change.
* File access, validated flags, seal, or huge-page policy changes.
* Anonymous private, shared, or droppable policy changes.
* Reservation or population policy changes.
* Region insertion semantics change.
* A helper starts selecting an error, mapping type, or policy branch.
