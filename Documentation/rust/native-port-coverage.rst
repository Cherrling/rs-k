.. SPDX-License-Identifier: GPL-2.0

Native Rust port coverage ledger
================================

Purpose
-------

This page records the authoritative production ownership of the native Rust
port. It distinguishes complete native components from earlier migrations
that still use a C entry-point adapter. It also records justified C and
assembly boundaries and the work that remains.

Status definitions
------------------

``migrated``
  Rust owns the complete production component or closed subsystem boundary.
  Rust defines the public production entry points. The native build excludes
  the corresponding C implementation.

``boundary-retained``
  C or assembly owns a named type layout, ABI contract, architecture
  primitive, boot entry, or other shared boundary. The entry includes the
  reason and review condition.

``pending``
  The scope is not yet native Rust, or an earlier Rust algorithm still uses a
  C ABI adapter. A pending item does not count as complete.

Migrated components
-------------------

Lock-less lists
  Status: ``migrated``.

  Evidence: :doc:`native-llist`.

  Rust defines ``llist_del_first()``, ``llist_del_first_this()``, and
  ``llist_reverse_order()``. The native build contains no C implementation or
  C ABI adapter for those operations.

Klist entry operations
  Status: ``migrated``.

  Evidence: :doc:`native-klist`.

  Rust defines the eleven public GPL symbols for insertion, deletion,
  attachment state, and iteration. The native C file retains list
  initialization, blocking removal, and the shared wakeup callback.

Boundary-retained inventory
---------------------------

Shared type layouts
  Status: ``boundary-retained``.

  Owner: ``include/linux/llist.h``.

  Reason: Both languages access the same intrusive structures.

  Test: Native, fallback, and C-only KUnit runs exercise the same layouts.

  Risk: A layout change can invalidate one language's field access.

  Exit condition: All consumers use a Rust-owned type and the C layout is no
  longer part of the shared interface.

Public ABI
  Status: ``boundary-retained``.

  Owner: The public symbols defined in ``include/linux/llist.h`` and
  ``include/linux/klist.h``.

  Reason: Existing callers and loadable modules use the stable C ABI.

  Test: KUnit calls the public symbols and module post-processing validates
  their exports.

  Risk: A signature or export-class change can break binary compatibility.

  Exit condition: A coordinated kernel-wide API change removes or replaces
  the public interface.

Architecture atomics
  Status: ``boundary-retained``.

  Owner: ``rust/kernel/sync/atomic`` and the architecture atomic layer.

  Reason: LKMM operations depend on architecture primitives.

  Test: SMP1 and SMP4 runs include native, KASAN, lockdep, and PREEMPT_RT.

  Risk: Incorrect ordering can cause lost nodes or stale successor reads.

  Exit condition: A replacement atomic API preserves the complete LKMM
  contract and all architecture implementations.

Lock-less list inline paths
  Status: ``boundary-retained``.

  Owner: ``include/linux/llist.h``.

  Reason: Header-only producer operations are part of the C container
  interface.

  Test: The concurrent producer and consumer KUnit case exercises these
  paths.

  Risk: Mixed atomic and non-atomic field access can introduce data races.

  Exit condition: All producers use a Rust-owned intrusive container API and
  the inline C interface is removed.

Klist initialization
  Status: ``boundary-retained``.

  Owner: ``lib/klist.c``.

  Reason: Spinlock initialization depends on architecture and lock debugging
  support.

  Test: Every klist KUnit case uses a C-initialized list, and lockdep runs the
  suite with expanded lock metadata.

  Risk: Incorrect initialization can leave lockdep state or architecture lock
  state invalid.

  Exit condition: A Rust abstraction provides equivalent lock initialization
  and debug behavior.

Blocking klist removal
  Status: ``boundary-retained``.

  Owner: ``lib/klist.c``.

  Reason: The operation registers a waiter and uses task state, scheduling,
  and wakeup primitives.

  Test: The blocking ``klist_remove()`` KUnit case runs in native, fallback,
  and C-only configurations.

  Risk: Incorrect publication or wake ordering can lose a wakeup or return
  before final release.

  Exit condition: Rust owns the complete waiter and task-wakeup lifecycle.

Klist primitive helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/klist.c``.

  Reason: The helpers preserve list debugging, refcount, spinlock, and
  interrupt-state semantics shared with C.

  Test: Native KUnit, KASAN, lockdep, and PREEMPT_RT runs exercise the helpers.

  Risk: A mismatched lock or reference primitive can break list lifetime or
  interrupt-state restoration.

  Exit condition: Approved Rust list, lock, and refcount abstractions cover the
  complete helper contract.

The lock-less list boundary is valid only while the inline producer paths,
intrusive layouts, and atomic semantics remain shared ABI. A boundary entry
does not transfer policy, retry decisions, state transitions, or exported
production entry points back to C.

The klist boundary is valid only while initialization, blocking removal,
task wakeup, and shared primitive behavior remain outside the migrated entry
scope. The retained C functions do not select insertion, reference, deletion,
attachment, or traversal policy.

Pending inventory
-----------------

The following major areas remain pending. The order follows dependency and
risk. Device drivers and peripheral subsystems remain behind the core work.

Direct-entry audit
  Status: ``pending``.

  Replace C ABI adapters with Rust-defined public entry points unit by unit.

Core object lifecycles
  Status: ``pending``.

  Complete the direct-entry and ownership review.

Lock and wait lifecycles
  Status: ``pending``.

  Complete the direct-entry and concurrency review.

Time and IRQ core
  Status: ``pending``.

  Complete the direct-entry and interrupt-context review.

Scheduling
  Status: ``pending``.

  Complete the direct-entry and lock-order review.

Memory management
  Status: ``pending``.

  Complete the direct-entry and fault-path review.

Signals and process lifecycles
  Status: ``pending``.

  Complete the direct-entry and lifetime review.

Core filesystem
  Status: ``pending``.

  Begin after the core contracts stabilize.

Core networking
  Status: ``pending``.

  Begin after the core contracts stabilize.

Device drivers
  Status: ``pending``.

  Begin after the core subsystems.

An earlier Rust algorithm with a C adapter is not listed as migrated. It
remains available in the ``rs`` baseline, but it must pass the direct-entry
audit before this ledger records it as complete.

Promotion rules
---------------

A component moves to ``migrated`` only after all of these conditions hold:

* Rust defines every production entry point for the selected scope.
* The native build excludes the corresponding C production implementation.
* Rust owns the component state machine, lifecycle, errors, and policy.
* Every retained boundary has an owner, reason, test, risk, and exit
  condition.
* Native, fallback, and C-only builds pass.
* Boot, focused KUnit, concurrency, KASAN, lockdep, PREEMPT_RT, Rustfmt,
  Clippy, Rustdoc, Sphinx, and checkpatch pass as applicable.
* Symbol inspection proves the production ownership claim.
