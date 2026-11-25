.. SPDX-License-Identifier: GPL-2.0

Native Rust PID Lifecycle Release
=================================

Status and scope
----------------

The ``CONFIG_RUST_PID_LIFECYCLE`` option selects native Rust for the
production PID release orchestration.  The original C implementation remains
available as a fallback.

This unit covers final-reference release, namespace count transitions, and PID
type array release.  It excludes PID allocation, the inline ``get_pid()`` fast
path, namespace lifetime implementation, IDR storage, pidfs storage, and RCU
implementation.  It does not change the public C ABI.

The Rust code owns these operations:

1. Decide whether a reference decrement released the final reference.
2. Release the pidfs object and PID cache storage in the original order.
3. Release the active namespace reference for a detached PID.
4. Traverse every namespace level while releasing a detached PID.
5. Select reaper wake, invariant warning, or no action after a count change.
6. Traverse a PID type array from ``PIDTYPE_MAX`` to ``PIDTYPE_MIN``.
7. Enter the deferred final-reference callback after the RCU grace period.

Entry points and ownership
--------------------------

Rust directly defines ``put_pid()``, ``free_pid()``, and ``free_pids()``. The
native build compiles the original C implementations out of ``kernel/pid.c``.
The C fallback and C-only configurations retain the complete implementations.

Rust preserves the original export classes. ``put_pid()`` remains a GPL-only
export, while ``free_pid()`` and ``free_pids()`` remain unexported.

Control flow, state selection, traversal, and operation order are in Rust. The
helpers perform one retained kernel primitive each. The Rust task-release
state machine calls the Rust PID reference and array release functions
directly.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel,
architecture, or subsystem primitives:

* The inline ``get_pid()`` fast path in ``include/linux/pid.h``.
* Atomic ``refcount_t`` increment, decrement, and final-reference tests.
* ``pidmap_lock`` and ``tasklist_lock`` lockdep assertions.
* Active namespace reference operations.
* Namespace PID count reads and writes.
* ``idr_remove()`` and child-reaper wake operations.
* pidfs removal and final PID cache release.
* Namespace reference release.
* RCU callback queueing.

These helpers do not select lifecycle transitions or release order.  The Rust
module reads helper results and owns the state machine.

Ordering and invariants
-----------------------

The caller of ``put_pid()`` owns one live PID reference.  A NULL pointer is
valid and does nothing.

The Rust final-reference path reads the active namespace before the reference
decrement.  After the final decrement, it frees pidfs state, frees the PID
cache object, and releases the namespace reference.  It does not access the
PID object after the cache release.

The detached PID path first asserts that ``tasklist_lock`` is not held.  It
releases the active namespace reference, then acquires ``pidmap_lock``.  While
the lock is held, Rust visits namespace levels from zero through the recorded
level.  Each visit decrements the namespace count and removes the matching IDR
entry.

The count transition preserves the original behavior.  Counts one and two wake
the child reaper.  ``PIDNS_ADDING`` reports the existing child-reaper
invariant.  Other counts require no action.

After the IDR removals, Rust removes the PID from pidfs and queues the final
deferred reference.  The RCU callback recovers the PID from its embedded RCU
head and enters the same Rust final-reference path.

``free_pids()`` releases non-NULL entries in descending PID type order.  The
PID type enumeration includes a non-reference-counting type, so every
non-NULL entry must be checked independently.

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
     - 6 of 6
     - Pass
   * - C lifecycle fallback
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - C-only
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - KASAN with native
     - SMP4, SMP1
     - 6 of 6
     - Pass
   * - Lockdep with native
     - SMP4, SMP1
     - 6 of 6
     - Pass
   * - PREEMPT_RT with native
     - SMP4, SMP1
     - 6 of 6
     - Pass

The two Rust-only selector tests are absent from the fallback and C-only runs.
The four implementation-independent release tests ran in every configuration.

The release tests cover a NULL pointer, a non-final reference, a synthetic
final reference, and 24 real kernel threads.  Each kernel thread starts,
allocates a PID, exits, is reaped by ``kthread_stop_put()``, and is followed by
``rcu_barrier()``.  The selector tests cover all count actions and every
non-NULL ordering in a PID type array.

The native SMP4 regression image enabled the PID allocation, PID association,
PID number, zombie task release, signal-aware exit, and task exit notification
suites.  The adjacent suites passed 24 of 24 cases.  These cover allocation
and rollback, task association, namespace number lookup, real task release,
signal exit, and parent notification.

Every KUnit log reported zero failures and no BUG, warning, KASAN report, Oops,
lockdep complaint, or kernel panic.

Native ``rust/kernel.o`` defines ``put_pid()``, ``free_pid()``, and
``free_pids()``.  Native ``kernel/pid.o`` contains no definition of those
entries.  The linked image contains one definition of each entry, no
``rust_pid_*`` wrapper, and no unresolved symbol.  ``vmlinux.symvers`` contains
``put_pid`` as ``EXPORT_SYMBOL_GPL`` and no entry for the other two functions.

The production configuration disables KUnit, KASAN, lockdep, and PREEMPT_RT.
It retains ``CONFIG_WERROR`` and ``CONFIG_RUST_PID_LIFECYCLE``.

The production build used Clippy and produced both ``bzImage`` and modules.
The four-vCPU KVM image completed PID setup, creation, and early fork activity
before it reached the expected missing-root-filesystem panic.  The log
contains no unexpected BUG, warning, KASAN report, Oops, refcount report, or
lockdep complaint.

A second production configuration disabled ``CONFIG_PID_NS`` while it kept the
Rust PID lifecycle enabled.  It passed Clippy, linked with no unresolved
symbols, retained the same three Rust entry points, and reached the same
expected panic without an unexpected diagnostic.  This verifies that the Rust
path does not depend on PID namespace inline functions that are absent from a
non-namespace build.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for the same reference-count, locking,
  RCU, IDR, or pidfs primitives.
* A helper starts to select lifecycle policy or release order.
* PID, pidfs, or PID namespace ownership rules change.
* The namespace count state machine gains another transition.
* The inline ``get_pid()`` path becomes out of line and can move to Rust
  without a measured hot-path regression.
