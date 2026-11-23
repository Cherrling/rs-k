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

List LRU
  Status: ``migrated``.

  Evidence: :doc:`native-list-lru`.

  Rust directly defines the fourteen node-local add, delete, isolate, count,
  and walk entry points. Nine functions retain GPL exports, three public
  helpers remain unexported, and two caller-locked helpers are internal. Rust
  owns item presence, list placement, slice and node count transitions,
  shrinker notification, scan budgets, retry and stop handling, and isolate
  accounting. The native C object retains lock accessors, initialization,
  destruction, memory-cgroup slice allocation, and reparenting. C fallback
  configurations retain the complete algorithms.

Generic bit searches
  Status: ``migrated``.

  Evidence: :doc:`native-find-bit`.

  Rust directly defines the sixteen public out-of-line searches and owns word
  traversal, range masks, partial-word filtering, population counts, nth-bit
  selection, clump extraction, and random-bit resolution. The native C object
  retains ordinary non-GPL export metadata. C also retains the shared bitmap
  storage layout and the random-number primitive. C fallback configurations
  retain the complete algorithms.

Generic radix trees
  Status: ``migrated``.

  Evidence: :doc:`native-genradix`.

  Rust directly defines the six public non-GPL entry points for sparse
  lookup, allocation, forward and reverse iteration, preallocation, and
  release. It owns root growth, tagged-root publication retries, traversal,
  iterator advancement, and recursive release. The native C object retains
  only export metadata. C also retains node allocation, tagged-root and node
  layouts, atomic root and child publication, root exchange, and the inline
  type-aware API. C fallback configurations retain the complete algorithms.

B+Tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-btree-entry`.

  Rust directly defines the nine public GPL entry points for lookup, update,
  last-entry selection, predecessor traversal, insertion, removal, merge,
  and visitor traversal. Rust owns every traversal, update, structural,
  merge, callback, and reclamation decision. The native C object retains node
  and geometry layouts, typed visitor callback adapters, mempool allocation
  and release, lifecycle initialization, and geometry objects. C fallback
  configurations retain the complete algorithms.

Kernel FIFO operations
  Status: ``migrated``.

  Evidence: :doc:`native-kfifo`.

  Rust directly defines the twenty public out-of-line FIFO entry points and
  owns ring copying, wrap-around, record framing, user-copy accounting, DMA
  segment preparation, and index commitment. The native C object retains
  allocation, initialization, user-access and barrier helpers, scatterlist
  mutation, and ordinary non-GPL export metadata. C fallback configurations
  retain the complete algorithms.

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
  ``include/linux/plist.h``, ``include/linux/list_sort.h``,
  ``include/linux/list_lru.h``, ``include/linux/generic-radix-tree.h``,
  ``include/linux/btree.h``, ``include/linux/find.h``, and
  ``include/linux/kfifo.h``.

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

Generic bitmap storage and inline interface
  Status: ``boundary-retained``.

  Owner: ``include/linux/bitmap.h`` and ``include/linux/find.h``.

  Reason: C callers and Rust share the ``unsigned long`` word layout, inline
  public names, and range contracts.

  Test: Native, fallback, and C-only runs exercise first, next, nth, last,
  clump, and random searches on partial and multiword bitmaps.

  Risk: A storage or range-contract change can make Rust read the wrong word
  or expose tail bits outside the logical size.

  Exit condition: Rust-owned bitmap accessors and inline interfaces replace
  the shared C representation.

Generic bit-search export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/find_bit.c`` and ``rust/Makefile``.

  Reason: The sixteen Rust-defined searches retain ordinary non-GPL exports
  while generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records sixteen ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all sixteen names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Random set-bit primitive
  Status: ``boundary-retained``.

  Owner: ``include/linux/random.h`` and the Rust bindings.

  Reason: Rust resolves the selected set-bit ordinal, while
  ``__get_random_u32_below()`` remains the kernel random-number primitive.

  Test: Empty, singleton, random-membership, concurrent, KASAN, lockdep, and
  PREEMPT_RT runs exercise the random path.

  Risk: A primitive range or return-contract change can select an out-of-range
  ordinal.

  Exit condition: A Rust random abstraction preserves the primitive range
  contract and kernel entropy policy.

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
  functions and five list LRU helpers have no module export.

  Test: ``vmlinux.symvers`` contains no priority-list symbol or list LRU
  helper, and the generated Rust export header excludes all eight names.

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

List LRU layout
  Status: ``boundary-retained``.

  Owner: ``include/linux/list_lru.h``.

  Reason: C and Rust access the same intrusive ``list_head``, node, slice,
  memory-cgroup slice, and memory-cgroup-aware xarray layouts.

  Test: Native, fallback, and C-only KUnit runs exercise add, delete,
  isolate, count, object-backed operations, every walk status, and concurrent
  workers on the same shared layouts.

  Risk: A layout change can invalidate direct Rust field access or make a C
  helper operate on the wrong slice or counter.

  Exit condition: Rust-owned types and accessors replace the shared C layouts.

List LRU locking and memory-cgroup resolution
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c`` and ``include/linux/list_lru-rust.h``.

  Reason: Spinlock and interrupt modes, RCU protection, object page-to-memcg
  lookup, dying-memcg traversal, and xarray slice iteration depend on MM and
  locking internals shared with C.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs execute locked,
  interrupt, RCU, retry, stop, and concurrent paths.

  Risk: A lock-mode or lifetime mismatch can leave a slice locked, use a dead
  memcg, or expose a reparented slice.

  Exit condition: Rust owns the complete lock, RCU, memcg, and xarray lifetime
  contract.

List LRU lifecycle and memory-cgroup slices
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c``.

  Reason: Initialization, destruction, dynamic per-memcg slice allocation,
  and reparenting retain their existing C lifecycle and allocation rules.

  Test: Every configuration uses C-initialized list LRUs and runs the full
  suite in native, fallback, pure C, KASAN, lockdep, and PREEMPT_RT builds.

  Risk: Incorrect allocation or reparent ordering can corrupt slice counts or
  detach live objects from the selected memcg.

  Exit condition: Rust owns list LRU allocation, destruction, and memcg
  reparenting with equivalent lifetime rules.

List LRU export metadata
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c`` and ``rust/Makefile``.

  Reason: Nine node-local functions retain GPL exports, while three public
  helpers and two caller-locked helpers have no module export.

  Test: ``vmlinux.symvers`` records the nine GPL exports, and the generated
  Rust GPL export header excludes all five unexported helpers.

  Risk: A duplicate export or class change can break module compatibility or
  expose an internal caller-locked interface.

  Exit condition: Rust export metadata supports per-symbol export and
  unexported status without the skip list.

Generic radix tree layouts and inline API
  Status: ``boundary-retained``.

  Owner: ``include/linux/generic-radix-tree.h``.

  Reason: C and Rust access the tagged root, interior child array, leaf data
  union, iterator, and type-aware container macros through one shared ABI.

  Test: Native, fallback, and C-only KUnit runs exercise shallow and deep
  trees, sparse entries, non-power-of-two objects, and repeated concurrent
  allocation.

  Risk: A layout, depth encoding, or offset-conversion change can make Rust
  traverse the wrong node or return a misaligned leaf entry.

  Exit condition: Rust-owned tree, node, iterator, and container interfaces
  replace the generic radix tree macros and layouts.

Generic radix tree atomic helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/genradix.c``.

  Reason: Root and child publication require kernel release compare-exchange,
  read-once child loads, and null exchange semantics.

  Test: Native SMP4, KASAN, lockdep, and PREEMPT_RT runs execute concurrent
  root and child allocation paths.

  Risk: A weaker or mismatched atomic primitive can publish an uninitialized
  child, lose an allocated node, or expose an incomplete tree.

  Exit condition: Rust owns equivalent layout-aware atomic publication and
  release primitives.

Generic radix tree allocation and release primitives
  Status: ``boundary-retained``.

  Owner: ``include/linux/generic-radix-tree.h`` and
  ``rust/helpers/genradix.c``.

  Reason: Zeroed page allocation, page release, and leaf data access retain
  the kernel allocator, memory-tagging, and layout contracts.

  Test: Every configuration checks zeroed pages, preallocated-node reuse,
  recursive release, and tree reset.

  Risk: An allocation or release mismatch can leak nodes, return nonzero
  entries, or free a published subtree.

  Exit condition: Rust owns allocation and release with equivalent zeroing,
  reuse, and lifetime behavior.

Generic radix tree export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/generic-radix-tree.c`` and ``rust/Makefile``.

  Reason: All six public symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records six ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all six names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

B+Tree layouts and geometry
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree.h``.

  Reason: C and Rust access the same tree head, geometry, node arrays, key
  slots, and value slots through one shared layout.

  Test: Native, fallback, and C-only traversal, structural, and visitor
  suites exercise 32-bit, 64-bit, and 128-bit keys in multi-level trees.

  Risk: A layout or geometry change can make Rust read or write the wrong key
  or value slot.

  Exit condition: Rust-owned tree, geometry, node, and key representations
  replace the shared C layout.

B+Tree allocation and lifecycle
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree-rust.h`` and ``lib/btree.c``.

  Reason: Zeroed mempool allocation, node release, geometry objects,
  initialization, destruction, allocator callbacks, and module setup retain
  kernel allocator and lifecycle contracts.

  Test: Native, fallback, and C-only suites check root growth, split rollback,
  failed allocations, merge ownership, node release, and tree reuse.

  Risk: An allocation or release mismatch can leak nodes, use a freed node,
  or attach a node from the wrong mempool.

  Exit condition: Rust owns node allocation, release, geometry setup, and tree
  lifecycle with equivalent zeroing and ownership behavior.

B+Tree typed visitor callbacks
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree.h`` and ``lib/btree.c``.

  Reason: The four public typed visitor adapters retain the existing 32-bit,
  64-bit, and 128-bit callback signatures while Rust uses the shared machine
  word key representation.

  Test: Native, fallback, and C-only visitor suites check callback keys,
  descending order, stable indices, counts, grim release, and tree reuse.

  Risk: A callback-ABI change can corrupt typed keys or break visitor
  compatibility.

  Exit condition: Rust directly supplies compatible typed visitor interfaces
  for every public key width.

Kernel FIFO layout and inline API
  Status: ``boundary-retained``.

  Owner: ``include/linux/kfifo.h``.

  Reason: C and Rust access the same index, mask, element-size, storage
  pointer, scatterlist, and record-prefix layout through the inline API.

  Test: Native, fallback, and C-only KUnit runs exercise in-place and pointer
  FIFOs, wrap-around, element scaling, records, user copies, and DMA ranges.

  Risk: A layout or inline-contract change can make Rust use the wrong range
  or publish an index before the corresponding data is visible.

  Exit condition: Rust-owned FIFO and inline container interfaces replace the
  shared C representation.

Kernel FIFO allocation and initialization
  Status: ``boundary-retained``.

  Owner: ``lib/kfifo.c``.

  Reason: Buffer allocation, NUMA-node placement, devres release, and
  caller-provided storage lifetime retain kernel allocator contracts.

  Test: Every configuration checks allocated and caller-provided FIFO setup,
  invalid sizes, element sizes, release, and reuse.

  Risk: An allocation or reset mismatch can leak storage or leave stale
  indices visible to Rust.

  Exit condition: Rust owns FIFO allocation and initialization with
  equivalent device-managed storage and lifetime behavior.

Kernel FIFO copy and scatterlist helpers
  Status: ``boundary-retained``.

  Owner: The architecture user-access layer, ``smp_wmb()``, and
  ``rust/helpers/scatterlist.c``.

  Reason: User copies, publication barriers, and scatterlist mutation remain
  architecture and kernel-helper boundaries.

  Test: Native, fallback, and C-only KUnit runs cover partial user copies,
  wrap splitting, DMA segment creation, and mapped-address preservation.

  Risk: A missing barrier or helper mismatch can expose partial records or
  corrupt DMA metadata.

  Exit condition: Rust-owned equivalents preserve the architecture ordering,
  user-access, and scatterlist contracts.

Kernel FIFO export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/kfifo.c`` and ``rust/Makefile``.

  Reason: All twenty out-of-line symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records twenty ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all twenty names.

  Risk: A duplicate export or class change can break module compatibility.

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

The list LRU boundary is valid only while the shared layouts, lock and
interrupt modes, RCU and memory-cgroup resolution, initialization and
destruction, and dynamic slice allocation and reparenting remain named C
boundaries. Rust defines every node-local public entry and owns every item,
count, scan, and walk transition.

The generic bit-search boundary is valid only while the shared
``unsigned long`` bitmap layout, inline public interface, random-number
primitive, and ordinary export metadata remain named boundaries. Rust defines
every public out-of-line search and owns every mask, traversal, count, and
selection decision.

The generic radix tree boundary is valid only while the tagged layouts,
inline container API, atomic publication helpers, allocation primitives, and
non-GPL export metadata remain named C boundaries. Rust defines every public
out-of-line entry and owns every growth, traversal, iteration, allocation
policy, and release transition.

The B+Tree boundary is valid only while the shared tree and node layouts,
mempool allocation primitives, typed visitor callbacks, geometry objects, and
lifecycle functions remain named C boundaries. Rust defines every public
algorithm entry and owns every traversal, update, structural, merge, visitor,
and reclamation decision.

The kernel FIFO boundary is valid only while the FIFO layout, allocation and
initialization, user-access and barrier helpers, scatterlist mutation, and
ordinary export metadata remain named C boundaries. Rust defines every public
out-of-line entry and owns every copy, framing, segment, and index decision.

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
