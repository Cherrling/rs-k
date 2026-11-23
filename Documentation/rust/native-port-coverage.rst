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

Lockref operations
  Status: ``migrated``.

  Evidence: :doc:`native-lockref`.

  Rust defines the six public ``lockref_*`` symbols. The native C object
  retains only their non-GPL export metadata. C also retains the packed
  layout, initialization, architecture compare-exchange, and spinlock
  helpers.

Kobject lifecycle
  Status: ``migrated``.

  Evidence: :doc:`native-kobject`.

  Rust defines the four public ``kobject_*`` symbols for typed
  initialization, reference acquisition, conditional acquisition, and final
  reference dispatch. The native C object retains only their non-GPL export
  metadata. C also retains bitfield accessors, diagnostics, the release
  callback bridge, and final cleanup.

Interval tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-interval-tree`.

  Rust defines the eight public GPL symbols for insertion, removal, subtree
  search, overlap iteration, and span iteration. The native C object retains
  only the intrusive link and cached augmented erase helpers. C fallback
  configurations retain the complete interval tree algorithms.

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

  Owner: The public symbols defined in ``include/linux/llist.h``,
  ``include/linux/klist.h``, ``include/linux/lockref.h``,
  ``include/linux/kobject.h``, and ``include/linux/interval_tree.h``.

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

Lockref layout and initialization
  Status: ``boundary-retained``.

  Owner: ``include/linux/lockref.h``.

  Reason: The packed spinlock and signed count share an architecture-specific
  word. ``lockref_init()`` initializes architecture lock debugging state.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  the layout and initialization.

  Risk: A layout or initialization mismatch can make a packed update race with
  the embedded lock.

  Exit condition: A Rust abstraction owns the packed layout and equivalent
  lock-debugging initialization.

Lockref architecture helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/lockref.c``.

  Reason: Relaxed compare-exchange, packed-word decoding, and spinlock
  operations depend on architecture and lock implementation details.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs execute the
  fast and serialized paths.

  Risk: Incorrect ordering or packed-word handling can lose a reference or
  corrupt the embedded lock state.

  Exit condition: Approved Rust atomics and spinlock abstractions preserve the
  complete packed operation contract.

Lockref export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/lockref.c``.

  Reason: The existing public API is exported as non-GPL ABI, while Rust GPL
  exports are generated automatically.

  Test: ``Module.symvers`` records all six public symbols as
  ``EXPORT_SYMBOL`` and the native object defines no lockref function body.

  Risk: Duplicate exports or a class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Kobject layout and state
  Status: ``boundary-retained``.

  Owner: ``include/linux/kobject.h``.

  Reason: C callers and retained sysfs and kset paths share the intrusive
  layout, registration bitfields, delayed-work member, and type pointer.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  initialization, reinitialization, and final cleanup.

  Risk: A layout or state mismatch can corrupt registration state or release
  a live object.

  Exit condition: A Rust-owned kobject type and safe state interface replace
  direct C field access.

Kobject diagnostics
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c``.

  Reason: Invalid-pointer, missing-type, reinitialization, and uninitialized
  reference diagnostics use kernel log and stack tracing facilities shared
  with C callers.

  Test: The invalid-initialization KUnit case runs in native, fallback, and
  C-only configurations.

  Risk: A diagnostic mismatch can hide a recoverable initialization error or
  change historical behavior.

  Exit condition: Rust diagnostics preserve the formatting, stack trace, and
  recoverability contracts.

Kobject release bridge
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c`` and ``include/linux/kobject-rust.h``.

  Reason: Rust dispatches the final reference through
  ``rust_kobject_release()``. C then owns delayed work, ktype release,
  parent-reference removal, name cleanup, and sysfs cleanup.

  Test: Final-reference KUnit cases pass under native SMP1/SMP4, KASAN,
  lockdep, and PREEMPT_RT.

  Risk: A callback mismatch can skip cleanup, double release an object, or
  break the delayed-release path.

  Exit condition: Rust owns the complete cleanup and delayed-release
  lifecycle with equivalent workqueue and callback primitives.

Kobject export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c``.

  Reason: The four public lifecycle operations retain a non-GPL export class
  while generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records all four public symbols as
  ``EXPORT_SYMBOL``, and the generated Rust GPL export header excludes them.

  Risk: Duplicate exports or a class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Interval tree layout and iterator state
  Status: ``boundary-retained``.

  Owner: ``include/linux/interval_tree.h``.

  Reason: C callers share the intrusive interval node, cached red-black root,
  augmented subtree endpoint, and used or hole span iterator layout.

  Test: Native, fallback, and C-only KUnit runs exercise ordering, subtree
  maxima, overlap traversal, and the complete span state machine.

  Risk: A layout or endpoint-type change can invalidate direct Rust access or
  alter inclusive range behavior.

  Exit condition: A Rust-owned interval tree type and iterator API replace the
  shared C layouts.

Augmented red-black tree helpers
  Status: ``boundary-retained``.

  Owner: ``lib/interval_tree.c`` and ``include/linux/interval-tree-rust.h``.

  Reason: Intrusive node linking, cached-leftmost insertion, and cached
  augmented erase depend on shared red-black tree layouts and callbacks.

  Test: Interval tree KUnit runs cover every public entry, and the adjacent
  Rust red-black tree suite passes four of four cases.

  Risk: A callback or rotation mismatch can corrupt subtree maxima or cached
  tree state during insertion or removal.

  Exit condition: Rust owns complete intrusive interval tree integration
  without C link or erase callbacks.

The lock-less list boundary is valid only while the inline producer paths,
intrusive layouts, and atomic semantics remain shared ABI. A boundary entry
does not transfer policy, retry decisions, state transitions, or exported
production entry points back to C.

The klist boundary is valid only while initialization, blocking removal,
task wakeup, and shared primitive behavior remain outside the migrated entry
scope. The retained C functions do not select insertion, reference, deletion,
attachment, or traversal policy.

The lockref boundary is valid only while initialization, the packed layout,
architecture operations, and non-GPL export metadata remain shared C
boundaries. Rust owns every reference transition and fallback decision.

The kobject boundary is valid only while the shared layout, bitfields,
diagnostics, final cleanup callback, and non-GPL export metadata remain named
C boundaries. Rust defines every public lifecycle entry and owns every
initialization and reference decision.

The interval tree boundary is valid only while the intrusive node, cached
red-black tree layout, augmented callback interface, and two narrow C helpers
remain shared infrastructure. Rust defines every public entry and owns start
ordering, overlap policy, subtree maximum updates, and span transitions.

Pending inventory
-----------------

The following major areas remain pending. The order follows dependency and
risk. Device drivers and peripheral subsystems remain behind the core work.

Direct-entry audit
  Status: ``pending``.

  Replace C ABI adapters with Rust-defined public entry points unit by unit.

Remaining core object lifecycles
  Status: ``pending``.

  Complete the direct-entry and ownership review for object types beyond
  kobject.

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
