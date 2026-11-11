.. SPDX-License-Identifier: GPL-2.0

Native Rust Timer Queue Migration
=================================

Status and scope
----------------

The ``CONFIG_RUST_TIMERQUEUE`` option selects native Rust implementations for
cached timer queue insertion, deletion, and successor traversal.  It also
selects native Rust insertion into linked timer queues.  The public API and
device driver scope remain unchanged.

Callers continue to serialize queue access.  Timer callback rules, timer base
state, and locking remain outside this library.

The Rust code owns these production decisions:

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

The exported ``timerqueue_*()`` functions remain C ABI adapters.  When the Rust
option is enabled, these adapters call the Rust production functions:

* ``rust_timerqueue_add()``
* ``rust_timerqueue_del()``
* ``rust_timerqueue_iterate_next()``
* ``rust_timerqueue_linked_add()``

The original C search, linking, and cached-leftmost decisions are compiled out.

Retained C boundary
-------------------

The following operations remain inline in ``include/linux/timerqueue.h``:

* ``timerqueue_init()`` and ``timerqueue_init_head()``.
* ``timerqueue_getnext()``.
* ``timerqueue_node_queued()``.
* Linked queue getters and initialization.
* ``timerqueue_linked_del()``.

These operations are direct field access or calls to the existing linked-tree
erasure primitive.  Keeping them inline preserves the established intrusive
data layout and lockless read paths.  ``timerqueue_linked_del()`` already uses
the Rust linked-tree erasure primitive when ``CONFIG_RUST_RBTREE`` is enabled.

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

The x86_64 validation used six configurations:

* Rust timer queues enabled with the Rust red-black tree implementation.
* The original C timer queue implementation and Rust red-black tree.
* Rust timer queues with generic inline KASAN.
* Rust timer queues with lockdep.
* Rust timer queues with PREEMPT_RT.
* Rust timer queues with the C red-black tree implementation.

The Rust production configuration passed all four KUnit cases in three
consecutive runs on four vCPUs.  The C fallback, KASAN, lockdep, PREEMPT_RT,
and C-red-black-tree configurations each passed all four cases on four vCPUs.
The tests covered signed boundaries, duplicate expiration values, cached
leftmost maintenance, deletion, successor traversal, and linked-chain
insertion and removal.

The validation logs contained no ``BUG``, warning, KASAN report, lockdep
complaint, or panic.  Disassembly showed that all four exported adapters
tail-call their Rust counterparts.  The C fallback had no Rust timer queue
symbols.  The configuration without the Rust red-black tree still ran the Rust
timer queue code and called the C tree primitives.  Every production image had
zero unresolved symbols.

Rustfmt, Clippy, strict checkpatch, and ``git diff --check`` passed.

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
