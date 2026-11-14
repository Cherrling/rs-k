.. SPDX-License-Identifier: GPL-2.0

Native Rust Final Address-Space Teardown
========================================

Status and scope
----------------

The ``CONFIG_RUST_MM_TEARDOWN`` option selects native Rust for the production
orchestration of final address-space teardown in ``__mmput()``. The original C
sequence remains available as a fallback.

This unit covers the code reached after ``mm_users`` reaches zero. It excludes
device drivers, individual subsystem teardown implementations, and final
``struct mm_struct`` release through ``mmdrop()``. It does not change the
public C ABI. ``CONFIG_RUST_EXIT_MMAP`` selects the separate mapping-teardown
unit described in :doc:`exit-mmap`. With that option disabled, the mapping
teardown remains the original C implementation.

The Rust code owns these production decisions:

* The complete ordering of subsystem teardown.
* Conditional removal from the global ``mmlist``.
* Conditional release of the binary-format module.
* The point at which the final ``mm_count`` reference is dropped.

The Rust code runs through this sequence:

1. Validate the zero-user invariant.
2. Clear uprobes and asynchronous I/O state.
3. Leave KSM and khugepaged.
4. Release user mappings and the huge-zero reference.
5. Clear the executable file.
6. Remove the address space from ``mmlist`` when required.
7. Release the binary-format module when required.
8. Leave the LRU-generation tracking.
9. Release the private futex hash.
10. Drop the final ``mm_count`` reference.

Entry points and ownership
--------------------------

The C ``__mmput()`` adapter calls ``rust_mm_final()`` when the option is
enabled. The original C sequence is compiled out. Both ``mm_users`` release
paths reach Rust teardown, while the adapter preserves the C entry point used
by fallback callers and the asynchronous work callback.

When both ``CONFIG_RUST_MM_LIFETIME`` and ``CONFIG_RUST_MM_TEARDOWN`` are
enabled, the Rust synchronous release path calls the shared Rust teardown
implementation directly. The asynchronous work callback still enters through
the C ``__mmput()`` adapter.

When only ``CONFIG_RUST_MM_TEARDOWN`` is enabled, the original C ``mmput()``
and ``mmput_async()`` paths call the Rust teardown through ``__mmput()``. This
configuration is a supported and tested combination.

The Rust implementation uses narrow C helpers for individual operations:

* The fatal ``mm_users`` diagnostic.
* Uprobe, asynchronous I/O, KSM, and khugepaged exits.
* Mapping teardown through ``exit_mmap()`` when Rust mapping teardown is
  disabled.
* The huge-zero reference release.
* Executable-file cleanup.
* ``mmlist`` emptiness, locking, and removal.
* Binary-format presence and module release.
* LRU-generation cleanup and private futex hash release.
* The final ``mmdrop()`` operation.

These helpers do not select ordering. Each helper performs one existing C
operation or one indivisible C critical section, while Rust evaluates the
invariant, chooses the conditional actions, and calls the operations in the
required order.

Retained C boundary
-------------------

The following operations remain in C because they directly use subsystem or
kernel internals:

* The fatal invariant diagnostic for a non-zero ``mm_users`` value.
* The subsystem-specific teardown functions.
* The global ``mmlist`` lock and list operation.
* Binary-format pointer access and module reference release.
* The inline ``mmdrop()`` operation and its memory-ordering contract.

The helpers expose no public API. Modules do not reference Rust teardown
symbols.

Ordering and invariants
-----------------------

The Rust sequence preserves the original C order. Mapping teardown runs after
the KSM, khugepaged, and asynchronous I/O teardown steps. It runs entirely in
Rust when ``CONFIG_RUST_EXIT_MMAP`` is enabled. The executable file is cleared
after mapping teardown.

The Rust plan queries ``mmlist`` only after mapping and executable-file
cleanup. It removes the entry under ``mmlist_lock`` only when the list is
non-empty. It releases the binary-format module only when ``mm->binfmt`` is
non-null.

The zero-user check remains fatal. Changing this invariant to a recoverable
warning would permit address-space teardown while users still hold references,
so the unit preserves the existing behavior.

The final C helper still calls the inline ``mmdrop()``. This retains the full
barrier required by the membarrier contract and preserves the established
``mm_count`` lifetime path.

Validation record
-----------------

The x86_64 validation used four vCPUs, 1536 MiB of RAM, PREEMPT_RT, and
lockdep. KVM was used when available, with TCG as the fallback accelerator.

.. list-table::
   :widths: 42 16 16 26
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust lifetime and Rust teardown
     - 3
     - 6 of 6
     - Pass
   * - C lifetime and C teardown
     - 3
     - 3 of 3
     - Pass
   * - C lifetime and Rust teardown
     - 3
     - 4 of 4
     - Pass
   * - Rust lifetime and C teardown
     - 3
     - 5 of 5
     - Pass
   * - KASAN with Rust lifetime and Rust teardown
     - 3
     - 6 of 6
     - Pass

The KASAN configuration used generic inline KASAN in addition to PREEMPT_RT
and lockdep.

The tests cover task-mm acquisition planning, synchronous and asynchronous
release planning, final conditional cleanup planning, real task-mm
acquisition, synchronous final teardown, and asynchronous final teardown. The
real teardown cases execute the Rust sequence and reach final ``mmdrop()``.

Every log reported zero failures and no BUG, warning, KASAN report, Oops,
lockdep complaint, or kernel panic.

The production build used Clippy with KUnit, PREEMPT_RT, and KASAN disabled.
The image contains ``rust_mm_final``. KUnit selector symbols are absent, and
``llvm-nm -u vmlinux`` reports no unresolved symbols. The production image
boots to the expected missing-root-filesystem panic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``mm_users`` invariant becomes recoverable without unsafe lifetime use.
* A subsystem teardown function gains a Rust-native safe abstraction.
* The subsystem teardown order or dependency rules change.
* The separate address-space mapping teardown gains a new lock or failure
  contract.
* ``mmlist`` locking or membership semantics change.
* Binary-format module lifetime rules change.
* ``mmdrop()`` gains a Rust implementation with the same ordering contract.
* A helper starts to select teardown policy instead of performing one
  primitive operation.
