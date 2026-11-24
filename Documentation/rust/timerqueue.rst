.. SPDX-License-Identifier: GPL-2.0

Native Rust timer queue operations
==================================

Status and scope
----------------

The ``CONFIG_RUST_TIMERQUEUE`` option selects native Rust implementations for
cached timer queue insertion, deletion, and successor traversal. It also
selects native Rust insertion into linked timer queues. Rust directly defines
the four public GPL entry points. The public API and device driver scope
remain unchanged.

Callers continue to serialize queue access.  Timer callback rules, timer base
state, and locking remain outside this library.

Rust owns these production decisions:

* Ordered search by the signed ``expires`` value.
* Intrusive red-black node linking.
* Cached-leftmost maintenance.
* Queue deletion and restoration of the empty-node sentinel.
* Successor traversal from an intrusive node to its timer queue entry.
* Linked-tree insertion and the doubly linked in-order chain.

The existing red-black tree primitives perform rotations, color repair,
erasure, and successor lookup.  With ``CONFIG_RUST_RBTREE`` enabled, those
shared primitives are also implemented in Rust.

Entry points and state ownership
--------------------------------

With the option enabled, Rust directly defines these public entry points:

* ``timerqueue_add()``
* ``timerqueue_del()``
* ``timerqueue_iterate_next()``
* ``timerqueue_linked_add()``

The original C search, linking, cached-leftmost decisions, and
``timerqueue_*()`` definitions are excluded from the native build. The
generated Rust GPL export metadata retains the existing module ABI.

Retained C boundary
-------------------

The following operations remain inline in ``include/linux/timerqueue.h``:

* ``timerqueue_init()`` and ``timerqueue_init_head()``.
* ``timerqueue_getnext()``.
* ``timerqueue_node_queued()``.
* Linked queue getters and initialization.
* ``timerqueue_linked_del()``.

These operations are direct field access or calls to the existing linked-tree
erasure primitive. Keeping them inline preserves the established intrusive
data layout and lockless read paths. ``timerqueue_linked_del()`` uses the Rust
linked-tree erasure primitive when ``CONFIG_RUST_RBTREE`` is enabled.

The intrusive timer queue and linked-node layouts, cached red-black root, and
inline accessors remain shared C interfaces. The red-black tree insertion,
rotation, color repair, erasure, and successor operations remain architecture
or shared tree boundaries.

Important invariants
--------------------

An initialized but unqueued node has ``__rb_parent_color`` equal to its own
address.  Add operations warn when the node is not empty.  Delete operations
warn when the node is empty.

The cached leftmost node is the first entry in expiration order.  Equal
expiration values use the red-black tree's established right insertion rule.
Deletion updates the cache through the removed node's successor before
erasure.

The linked tree chain contains exactly the queued entries in traversal order.
Insertion updates both neighbor links and the cached leftmost entry.  Removal
must clear both chain pointers and the intrusive node sentinel.

Validation record
-----------------

Validation on 2026-11-24 used base commit ``0b7f0e1cacf0``. Seven x86_64
configurations were built:

* Native Rust timer queues with Rust red-black tree primitives.
* Rust timer queues with C red-black tree primitives.
* The C timer queue implementation with Rust red-black tree primitives.
* C-only build with ``CONFIG_RUST=n``.
* Native Rust with generic inline KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.

All 18 timer queue KUnit runs passed 4 of 4 cases. Native used one SMP1 run
and three SMP4 runs. KASAN used one SMP1 run and three SMP4 runs. Every other
configuration used one SMP1 and one SMP4 run. The tests covered signed
boundaries, duplicate expiration values, cached-leftmost maintenance,
deletion, successor traversal, and linked-chain insertion and removal.

The adjacent ``hrtimer_core`` suite passed 25 of 25 cases on SMP4 for native,
fallback, C-red-black-tree, KASAN, lockdep, and PREEMPT_RT configurations.
The C-only configuration passed its five applicable hrtimer cases.

The validation logs contained no ``BUG``, warning, Oops, kernel panic, KASAN
report, lockdep complaint, or RCU stall.

In the native, KASAN, lockdep, PREEMPT_RT, and C-red-black-tree
configurations, ``rust/kernel.o`` defined all four public entry points and
``lib/timerqueue.o`` defined none. The fallback and C-only configurations
defined all four in ``lib/timerqueue.o`` and none in Rust.
``rust/exports_kernel_generated.h`` emitted the four GPL exports in every Rust
timer queue configuration. Every ``Module.symvers`` contained exactly four
``EXPORT_SYMBOL_GPL`` entries for these names. No unresolved timer queue
symbols or old C ABI adapters remained.

Disassembly showed that ``timerqueue_add()`` directly executes the Rust
ordered search and tree insertion rather than tail-calling an adapter.

Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation, strict checkpatch,
and ``git diff --check`` passed. Sphinx reported only the existing
``kernel-docs`` and ``getting_llvm`` warnings.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Timer queue callers stop serializing queue access.
* A cache lookup becomes a production decision rather than direct field
  access.
* The timer queue or linked node layout changes.
* The red-black tree insertion, erasure, or successor contract changes.
* Kernel Rust gains safe abstractions that keep the inline paths sound and
  measurable.
