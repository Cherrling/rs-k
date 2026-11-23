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

Red-black tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-rbtree`.

  Rust defines the eleven public GPL and non-GPL symbols for insertion,
  removal, augmented callbacks, traversal, replacement, and linked-tree
  maintenance. The native C object retains only non-GPL export metadata and
  uses one internal Rust augmented erase bridge. C fallback configurations
  retain the complete algorithms.

Priority lists
  Status: ``migrated``.

  Evidence: :doc:`native-plist`.

  Rust directly defines ``plist_add()``, ``plist_del()``, and
  ``plist_requeue()``. It owns priority placement, equal-priority FIFO
  ordering, bucket promotion, requeue decisions, and the validation sequence.
  C retains intrusive layouts, raw list primitives, and diagnostic helpers.
  The functions remain unexported.

List sort
  Status: ``migrated``.

  Evidence: :doc:`native-list-sort`.

  Rust directly defines ``list_sort()``. It owns stable bottom-up merging,
  pending-list state, comparator invocation, and restoration of the circular
  doubly-linked list. The native C object retains only the non-GPL export
  metadata. C fallback configurations retain the complete algorithm.

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
  ``include/linux/kobject.h``, ``include/linux/interval_tree.h``,
  ``include/linux/rbtree.h``, ``include/linux/rbtree_augmented.h``,
  ``include/linux/plist.h``, and ``include/linux/list_sort.h``.

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

Red-black tree layout and color encoding
  Status: ``boundary-retained``.

  Owner: ``include/linux/rbtree_types.h`` and ``include/linux/rbtree.h``.

  Reason: C and Rust access the same intrusive node, cached root, linked-node,
  parent pointer, and color fields.

  Test: Native, fallback, and C-only KUnit runs check color invariants,
  black-height, traversal order, linked order, augmentation, and replacement.

  Risk: A layout or parent-color encoding change can corrupt both the tree and
  lockless lookup assumptions.

  Exit condition: A Rust-owned tree and node representation replaces the
  shared C layouts.

Red-black tree export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/rbtree.c`` and ``rust/Makefile``.

  Reason: Ten historical symbols use non-GPL exports while
  ``rb_erase_linked()`` and the internal augmented erase bridge use GPL
  exports.

  Test: ``vmlinux.symvers`` records the ten non-GPL exports and two GPL
  exports, and the generated Rust GPL header excludes the ten historical
  non-GPL names.

  Risk: A duplicate export or class change can break module loading and binary
  compatibility.

  Exit condition: Rust export metadata supports all original export classes
  without C metadata.

Augmented red-black erase dispatch
  Status: ``boundary-retained``.

  Owner: ``include/linux/rbtree_augmented.h`` and
  ``rust/kernel/rbtree/native.rs``.

  Reason: The inline augmented erase interface coordinates callback
  propagation, node replacement, and rebalancing as one operation.

  Test: Native, fallback, and C-only KUnit runs exercise augmented insertion,
  removal, and rotations.

  Risk: A callback or rebalance mismatch can corrupt subtree aggregates or
  detach the wrong child.

  Exit condition: Rust owns the complete augmented erase interface and no
  internal C-to-Rust bridge remains.

Priority-list layout and list primitives
  Status: ``boundary-retained``.

  Owner: ``include/linux/plist.h``, ``include/linux/plist_types.h``, and
  ``lib/plist.c``.

  Reason: C callers share the intrusive head, node, priority bucket, and
  common ``list_head`` representation.

  Test: Native, fallback, and C-only KUnit runs check priority order, FIFO
  order, bucket promotion, requeue, and debug validation.

  Risk: A layout or list primitive change can corrupt bucket links or break
  list hardening and barriers.

  Exit condition: Rust owns the complete intrusive representation and list
  primitive contract.

Priority-list diagnostics
  Status: ``boundary-retained``.

  Owner: ``lib/plist.c`` and ``include/linux/plist-rust.h``.

  Reason: ``WARN_ON()``, ``BUG_ON()``, and the optional full-list validation
  require kernel diagnostic infrastructure.

  Test: Every native and fallback run enables ``CONFIG_DEBUG_PLIST`` and runs
  its boot self-test together with the KUnit suite.

  Risk: A changed diagnostic can mask a corrupt bucket or alter the fatal
  requeue contract.

  Exit condition: Rust diagnostics preserve the warning, fatal, and optional
  validation behavior with no C diagnostic bridge.

Unexported Rust public symbols
  Status: ``boundary-retained``.

  Owner: ``rust/Makefile``.

  Reason: Automatic Rust exports are GPL, while the three priority-list
  functions have no module export.

  Test: ``vmlinux.symvers`` contains no priority-list symbol, and the
  generated Rust export header excludes all three names.

  Risk: An automatic export can unintentionally expand the module ABI.

  Exit condition: Rust export generation supports per-symbol unexported
  status without the skip list.

List sort layout and comparator ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/list.h`` and ``include/linux/list_sort.h``.

  Reason: C callers share the intrusive ``list_head`` layout and comparator
  callback ABI with Rust.

  Test: Native, fallback, and C-only KUnit runs check ordering, stability,
  edge cases, poisoning, and restored list links.

  Risk: A layout or callback change can corrupt list links or invoke the
  comparator with an invalid calling convention.

  Exit condition: Rust owns the complete intrusive list representation and
  comparator interface.

List sort export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/list_sort.c`` and ``rust/Makefile``.

  Reason: ``list_sort()`` retains its historical non-GPL export class while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records ``EXPORT_SYMBOL(list_sort)`` and the
  generated Rust GPL export header excludes the symbol.

  Risk: A duplicate export or class change can break module loading and
  binary compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

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

The red-black tree boundary is valid only while the shared node encoding,
intrusive layouts, augmented callback interface, internal erase bridge, and
historical export metadata remain named boundaries. Rust defines every public
entry and owns every tree mutation and traversal decision.

The priority-list boundary is valid only while the intrusive representation,
raw list primitives, diagnostics, and unexported ABI policy remain named C
boundaries. Rust defines every public entry and owns every placement, bucket,
and requeue decision.

The list sort boundary is valid only while the shared list layout, comparator
ABI, and historical export metadata remain named boundaries. Rust defines the
public entry and owns every merge, pending-list, stability, and final-link
decision.

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
