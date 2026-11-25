.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Range Unmapping
=========================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_UNMAP`` option selects direct Rust definitions of the
production ``__vm_munmap()``, ``do_vmi_munmap()``, ``do_munmap()``, and
``vm_munmap()`` entries. The option depends on ``CONFIG_RUST_EXIT_MMAP``.
The complete C implementations in ``mm/vma.c`` and ``mm/mmap.c`` are selected
only when the option is disabled.

This unit covers the public ``munmap()`` entry point and the internal
``do_vmi_munmap()`` entry point. It also covers the non-maple-tree-aware
``do_munmap()`` wrapper and the internal ``vm_munmap()`` wrapper. Rust owns
range validation, page alignment, the first-overlap lookup, empty-range lock
handling, mmap write locking, wrapper dispatch, and the userfaultfd completion
order.

The aligned unmap entry point, completion sequence, and rollback loops are the
separate Rust unit described in :doc:`vma-munmap-complete`. The gather loop and
partial-split decisions are described in :doc:`vma-munmap-gather`.

Entry points and ownership
--------------------------

``rust/kernel/vma_unmap.rs`` directly defines all four production entries.
The original C implementations in ``mm/vma.c`` and ``mm/mmap.c`` compile only
when ``CONFIG_RUST_VMA_UNMAP`` is disabled. The existing
``EXPORT_SYMBOL(vm_munmap)`` metadata remains in C. ``do_munmap()`` remains
unexported. The change does not expose a new public kernel ABI.

Rust ``__vm_munmap()`` obtains ``current->mm`` through one narrow C helper. It
creates the private VMA iterator and userfaultfd list, acquires the write lock,
runs the range operation, completes the required lock-release path, and
reports the userfaultfd events.

Rust ``do_vmi_munmap()`` receives the caller's existing iterator, mm, and
userfaultfd list. It applies the same range validation and first-overlap
decision. This preserves all existing callers that already hold the mmap write
lock.

Rust ``do_munmap()`` provides private iterator storage for callers that pass
only an mm and range. A narrow C helper initializes that storage. Rust then
uses the same range decision and passes the held-lock contract to the aligned
unmap operation.

Rust ``vm_munmap()`` forwards to ``__vm_munmap()`` with the non-unlocking
contract. It preserves the callers that expect a successful call to leave the
mmap write lock held.

Rust owns these decisions:

1. Page-aligned start validation.
2. Address-space limit validation.
3. Length overflow and page alignment.
4. Zero-length rejection.
5. First overlapping VMA lookup.
6. Whether an empty range releases the write lock.
7. The killable write-lock acquisition for the syscall path.
8. The conditional write unlock after errors.
9. Userfaultfd completion after the lock contract finishes.
10. The lock-release contract selected by each wrapper.
11. The lifetime of private iterator storage for ``do_munmap()``.

Retained C boundary
-------------------

The C boundary contains:

* Current-address-space field access.
* VMA iterator and userfaultfd list initialization primitives.
* Mmap write-lock acquisition and release.
* ``TASK_SIZE`` evaluation for the active thread.
* First-overlap maple-tree lookup.
* Userfaultfd unmap completion.
* The private VMA iterator and userfaultfd list layout.

``do_vmi_align_munmap()`` now gathers, splits, detaches, completes, and rolls
back through Rust. Split orchestration is described in :doc:`vma-split`. C
retains the individual VMA and maple-tree primitives.

Ordering and invariants
-----------------------

The syscall path initializes an iterator at ``start`` and an empty
userfaultfd list. It takes the mmap write lock with a killable acquisition.

For an invalid range, Rust returns ``-EINVAL`` before touching the iterator.
For a valid range, it rounds the length up to a page boundary and rejects a
zero resulting range.

Rust uses the iterator to find the first VMA that overlaps the end address. If
no VMA exists, the operation succeeds. With the unlock contract enabled, Rust
releases the write lock and still completes userfaultfd processing.

When a VMA overlaps, Rust calls the existing aligned munmap implementation.
After that call, Rust preserves the original lock rules:

* An error leaves the write lock held for Rust to release.
* A non-unlocking caller leaves the write lock held for Rust to release.
* A successful unlocking caller delegates the final release to the aligned
  munmap implementation.

Userfaultfd completion runs after the final lock decision.

The ``do_munmap()`` wrapper initializes an iterator for the supplied mm and
start address. Its caller must already hold the mmap write lock. The wrapper
never requests a final unlock.

The ``vm_munmap()`` wrapper uses the current address space and always selects
the non-unlocking contract. The function definition remains an ordinary
exported symbol, and its C export metadata remains outside the fallback
implementation guard.

Testing
-------

KUnit checks aligned ranges, unaligned length rounding, the maximum valid
range, unaligned starts, starts beyond ``TASK_SIZE``, oversized lengths,
zero-length requests, and arithmetic wraparound.

The userspace test runs 64 rounds. Each round checks:

* Repeated unmapping of an empty range.
* Partial front, middle, and tail unmapping.
* Removal of multiple remaining VMAs with holes in one call.
* Unaligned start rejection.
* Zero-length and overflow-length rejection.
* Page-aligned rounding of an unaligned length.
* Shared anonymous and shared ``memfd`` mappings.
* ``MREMAP_FIXED`` replacement through ``do_munmap()``.
* AIO context setup and teardown through ``vm_munmap()``.
* A gate-synchronized four-thread workload with 16 unmap cycles per worker.

The test reports through ``/dev/kmsg`` and powers off QEMU.

Validation record
-----------------

The KUnit suite contains one Rust range-operation case. Native KUnit ran three
times at four vCPUs and once at one vCPU. KASAN, lockdep, and PREEMPT_RT each
ran KUnit once at four vCPUs and once at one vCPU. Every configuration ran the
userspace suite once. Each run covered the 64 single-threaded rounds and 4096
concurrent map and unmap cycles.

.. list-table::
   :widths: 34 20 20 26
   :header-rows: 1

   * - Configuration
     - KUnit runs
     - KUnit result
     - Userspace
   * - Native Rust
     - SMP4: 3, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - C fallback
     - Not built
     - Not built
     - 1 pass
   * - C-only fallback
     - Not built
     - Not built
     - 1 pass
   * - KASAN
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - lockdep
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass
   * - PREEMPT_RT
     - SMP4: 1, SMP1: 1
     - 1 of 1 pass
     - 1 pass

Userspace runs used 1536 MiB of RAM. KUnit runs used 1024 MiB of RAM. KVM was
the primary accelerator and TCG was the fallback. The KASAN configuration used
Clang and LLD because Rust KASAN support requires Clang.

The final logs contain no failed case, BUG, warning, KASAN report, lockdep
complaint, RCU stall, oops, or unexpected panic.

An adjacent native SMP4 boot passed ``copy_mm`` 5 of 5 cases,
``mm_lifetime`` 6 of 6 cases, ``exit_mmap`` 2 of 2 cases, and ``dup_mmap``
2 of 2 cases.

Build and production checks:

* Native, fallback, and C-only builds completed. Clippy passed with
  ``CONFIG_WERROR=y``. The native, KASAN, lockdep, and PREEMPT_RT builds also
  completed with ``CONFIG_WERROR=y``.
* ``rustfmtcheck``, Rustdoc, and ``git diff --check`` passed.
* Strict checkpatch reported zero errors, zero warnings, and zero checks.
* ``scripts/get_maintainer.pl`` matched every added file.
* The Rust Sphinx build passed with only existing cross-reference warnings.
* The native Rust object directly defines all four production entries. Native
  ``mm/mmap.o`` has no ``do_munmap()`` or ``vm_munmap()`` definition. Native
  ``mm/vma.o`` has no ``__vm_munmap()`` or ``do_vmi_munmap()`` definition.
* Fallback and C-only objects define ``do_munmap()`` and ``vm_munmap()`` in
  ``mm/mmap.o`` and ``__vm_munmap()`` and ``do_vmi_munmap()`` in
  ``mm/vma.o``. None of the entries is defined in Rust.
* Each linked image has one definition of each production entry. The
  production image omits the old adapter, test-only, and VMA-unmap KUnit
  symbols.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* ``Module.symvers`` contains ``vm_munmap`` as an ordinary ``EXPORT_SYMBOL``.
  It omits ``do_munmap``, ``__vm_munmap``, and ``do_vmi_munmap``.
* Rust disassembly calls the lock, iterator, range-lookup, aligned-munmap,
  unlock, and userfaultfd helpers in the documented order.
* Disassembly shows that all four entries are implemented in the Rust
  object.
* The final production image boots to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier BUG, warning, KASAN report,
  RCU stall, lockdep complaint, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* The detached maple-tree representation becomes safe for Rust to own.
* The mmap lock release contract changes.
* A wrapper changes its lock-release contract.
* The first-overlap lookup changes to another tree or index.
* Userfaultfd unmap completion changes its ordering contract.
* Private iterator initialization starts selecting unmap policy.
* A helper starts selecting munmap policy instead of performing one primitive
  operation.
