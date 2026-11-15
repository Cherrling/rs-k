.. SPDX-License-Identifier: GPL-2.0

Native Rust Entry Orchestration for mremap
==========================================

Status and scope
----------------

The ``CONFIG_RUST_MREMAP_ENTRY`` option selects native Rust for the production
``do_mremap()`` path. The original C implementation remains available as a
fallback.

Rust owns these decisions:

1. Length alignment before validation.
2. Unexpected flag rejection.
3. Source address alignment.
4. New length rejection for zero or excessive values.
5. The early success path when no new address is implied.
6. New address range and alignment.
7. The ``MREMAP_MAYMOVE`` requirement for an implied new address.
8. The no-resize requirement for ``MREMAP_DONTUNMAP``.
9. Source and target overlap rejection.
10. Mmap write-lock acquisition and interrupt propagation.
11. Early map-count rejection and immediate unlock.
12. Move-only operation dispatch.
13. Source lookup, preparation, and error propagation.
14. Fixed-target versus non-fixed resize dispatch.
15. Final lock release, populated expansion, and userfaultfd notification.

Retained C boundary
-------------------

The C adapter initializes one opaque entry state. The Rust entry receives the
opaque state pointer and does not access the remap layout.

The C helpers expose one field projection, predicate, or existing operation.
The boundary contains:

* Length alignment and flag-mask inspection.
* Address and length limit predicates.
* New-address implication and range predicates.
* Source and target overlap inspection.
* The mmap write-lock operation.
* The early map-count predicate.
* Move-only and new-address predicates.
* Source VMA lookup and preparation.
* The existing ``remap_move()``, ``mremap_to()``, and ``mremap_at()`` entries.
* Kernel error-value inspection.
* Mmap unlock, populated expansion, and userfaultfd notification.

Two predicates combine checks that produce the same error and have no side
effects. One rejects a zero or excessive new length. The other rejects a new
address outside the task limit or without page alignment. No helper selects a
different success or failure path.

Ordering and invariants
-----------------------

Rust aligns both lengths before it validates parameters. Validation returns
``-EINVAL`` for the documented invalid inputs. Requests without an implied new
address return success from validation after the common checks.

Rust acquires the mmap write lock in killable mode and returns ``-EINTR`` when
the acquisition is interrupted. An early map-count failure unlocks immediately
and returns ``-ENOMEM``.

A move-only request goes directly to the batch move path. Other requests look
up and prepare the source VMA. A preparation error goes directly to the common
completion path. Successful preparation selects the fixed-target or resize
path.

After execution, Rust classifies the return value as success or error. Rust
then releases the mmap write lock only when the operation still holds it.
Successful mlocked expansion populates the new range. Every request that
reaches execution sends the required userfaultfd notifications after unlock.

Testing
-------

The ``copy_vma`` userspace suite covers unexpected flags, misaligned source and
target addresses, zero and excessive new lengths, fixed moves without
``MREMAP_MAYMOVE``, invalid ``MREMAP_DONTUNMAP`` resizing, and overlapping
targets. Existing tests cover move-only, fixed-target resize, non-fixed resize,
mlocked ``MREMAP_DONTUNMAP``, and injected move failures.

Validation record
-----------------

Validation completed on 2026-11-15. Five x86_64 kernel configurations built:
production, C fallback, KASAN, lockdep, and PREEMPT_RT. The first, third,
fourth, and fifth configurations enabled ``CONFIG_RUST_MREMAP_ENTRY``.

The ``copy_vma`` userspace suite passed three out of three QEMU runs on all
five configurations. The parameter cases verified unexpected flags,
misaligned source and target addresses, zero and excessive new lengths,
invalid fixed targets, and overlapping targets. The suite also verified that
every rejected request left the source VMA intact.

The KASAN configuration used generic instrumentation with ``DEBUG_VM`` and
page-allocation fault injection. It passed the existing partial-move
fault-injection case and the complete entry suite in all three runs. Stack
instrumentation was disabled because Clang stack instrumentation exceeds the
kernel frame limit.

The production configuration passed three out of three QEMU runs for each
adjacent userspace suite: ``dup_mmap``, ``exit_mmap``, ``vma_unmap``, and
``vma_merge``.

The test source passed GCC and Clang static builds with ``-Wall -Wextra
-Werror``. The Rust checks passed ``rustfmtcheck``, Clippy, and Rustdoc.
Strict reStructuredText validation and ``git diff --check`` passed.

The production image contains one ``rust_mremap_entry`` entry and all 23 C
helper functions. The C fallback image contains no entry Rust symbols.
Disassembly shows a direct ``rust_mremap_entry`` call from
``__do_sys_mremap()``.

The final scan covered 27 QEMU logs. It found no ``BUG``, ``WARNING``, KASAN
report, kernel oops, lockdep report, or RCU stall.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Supported ``MREMAP_*`` flags or their validation order change.
* ``TASK_SIZE``, page alignment, or overlap rules change.
* Mmap lock acquisition or unlock ownership changes.
* Early map-count validation changes.
* Move-only classification changes.
* VMA lookup or ``check_prep_vma()`` ordering changes.
* Fixed-target or resize dispatch changes.
* Populated expansion or userfaultfd notification ordering changes.
* A helper starts selecting a success, failure, or dispatch branch.
