.. SPDX-License-Identifier: GPL-2.0

Native Rust PID Allocation
==========================

Status and scope
----------------

The ``CONFIG_RUST_PID_ALLOCATION`` option selects native Rust for the
production PID allocation state machine.  The original C implementation
remains available as a fallback.

This unit covers set_tid validation, namespace traversal, IDR allocation,
IDR preload retry selection, partial-failure rollback, namespace publication,
counter updates, active-reference acquisition, and pidfs insertion failure
handling.  It does not change the public C ABI.

The Rust code owns these decisions:

1. Reject a set_tid array larger than the namespace depth.
2. Validate every requested identifier against its namespace maximum.
3. Select the namespace-specific cyclic PID lower bound.
4. Retry an IDR allocation at most once after ``-ENOMEM``.
5. Map IDR exhaustion to ``-EEXIST`` or ``-EAGAIN``.
6. Preserve the requirement that PID 1 is created before other PIDs.
7. Roll back every identifier allocated before publication.
8. Refuse publication into a namespace that stopped accepting PID creation.
9. Publish identifiers and counters under the existing PID map lock.
10. Release a fully published PID if pidfs insertion fails.

Entry points and ownership
--------------------------

Rust directly defines the production ``alloc_pid()`` entry.  The native build
compiles the original C implementation out of ``kernel/pid.c``.  The C
fallback and C-only configurations retain the complete implementation.

Rust returns either a live PID pointer or an ``ERR_PTR()`` value.  Error
encoding, state transitions, allocation retry, rollback, publication, and
pidfs failure handling all remain in Rust.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel or
subsystem primitives:

* PID cache allocation and raw cache release.
* PID lock, task-list, wait queue, inode-list, and pidfs initialization.
* ``READ_ONCE()`` namespace field reads.
* Capability checks for checkpoint-restore PID requests.
* PID namespace reference acquisition and release.
* IDR preload, allocation, cursor, replacement, and removal operations.
* ``pidmap_lock`` acquisition and release.
* Namespace active-reference acquisition.
* pidfs insertion.

The helpers do not choose retry, error mapping, validation, publication, or
rollback policy.

Ordering and invariants
-----------------------

The PID cache object and namespace reference are acquired before set_tid
validation.  Every early failure releases both resources in the original
order.

The namespace walk records the effective PID maximum for every level before
allocating under ``pidmap_lock``.  The same recorded values are used for IDR
allocation so a concurrent sysctl change cannot alter the selected bounds.

An IDR allocation uses NULL placeholders until every namespace level succeeds.
This prevents ``find_pid_ns()`` from observing a partially initialized PID.
Publication replaces each placeholder and increments the matching namespace
counter while the lock remains held.

If an IDR allocation reports ``-ENOMEM``, Rust drops and reacquires the lock
and preload section once.  A second ``-ENOMEM`` is returned to the caller.

The rollback range starts at the first identifier assigned in the current
attempt.  Earlier identifiers remain allocated until the same cleanup path
removes them.

The namespace must still have the ``PIDNS_ADDING`` bit set at every level
before publication.  This check runs after all identifiers are allocated so
more specific errors retain their established error codes.

pidfs insertion is the last fallible operation.  If it fails, Rust calls the
normal ``free_pid()`` release path and returns ``-ENOMEM``.

Validation record
-----------------

The x86_64 validation used the final source for every KUnit configuration.
KASAN used Clang, ``LLVM=1``, and generic inline KASAN.

.. list-table::
   :widths: 38 24 16 22
   :header-rows: 1

   * - Configuration
     - Boots
     - Cases
     - Result
   * - Native
     - 3 SMP4, 1 SMP1
     - 8 of 8
     - Pass
   * - Rust fallback
     - SMP4, SMP1
     - 6 of 6
     - Pass
   * - C-only
     - SMP4, SMP1
     - 6 of 6
     - Pass
   * - KASAN with native
     - SMP4, SMP1
     - 8 of 8
     - Pass
   * - Lockdep with native
     - SMP4, SMP1
     - 8 of 8
     - Pass
   * - PREEMPT_RT with native
     - SMP4, SMP1
     - 8 of 8
     - Pass

The two Rust-only selector tests are absent from the fallback and C-only runs.
The six implementation-independent integration tests ran in every
configuration.

The tests cover the cyclic lower bound, IDR error mapping, default allocation,
invalid set_tid sizes and values, explicit-ID collision, nested namespace
rollback, 32 sequential allocations, and 16 concurrent allocator workers.
The nested rollback case first verifies child-reaper rejection rollback.  It
then allocates an inner ID, fails an outer explicit ID lookup, verifies
inner-ID rollback, and reuses the inner namespace.

Every KUnit log reported zero failures and no BUG, warning, KASAN report,
Oops, refcount report, lockdep complaint, or kernel panic.

The native SMP4 regression image enabled the PID lifecycle, PID association,
PID number, zombie task release, signal-aware exit, and task exit notification
suites.  The adjacent suites passed 22 of 22 cases.

Native ``rust/kernel.o`` defines ``alloc_pid()`` and native ``kernel/pid.o``
contains no implementation.  The linked image contains one definition, no
``rust_pid_alloc()`` wrapper, no export-table entry, and no unresolved symbol.

The production build used Clippy and produced both ``bzImage`` and modules.
The production image starts with four vCPUs under KVM.  PID allocation,
process creation, early scheduling, and namespace setup complete before the
image reaches the expected missing-root-filesystem panic.  The boot log
contains no unexpected BUG, warning, KASAN report, Oops, refcount report, or
lockdep complaint.

A second production configuration disabled ``CONFIG_PID_NS`` while it kept the
Rust PID allocator enabled.  It passed the full build, linked with no
unresolved symbols, kept ``alloc_pid()`` defined by Rust, and reached the
same expected missing-root-filesystem panic without an unexpected diagnostic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for IDR, pidfs, namespace references, or
  PID cache allocation.
* A helper starts to choose allocation policy or rollback order.
* PID namespace level limits, ``RESERVED_PIDS``, or ``PIDNS_ADDING`` semantics
  change.
* The IDR preload protocol changes.
* set_tid validation or capability rules change.
