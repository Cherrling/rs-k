.. SPDX-License-Identifier: GPL-2.0

Native Rust priority lists
==========================

Status and scope
----------------

The ``CONFIG_RUST_PLIST`` option selects native Rust implementations of
``plist_add()``, ``plist_del()``, and ``plist_requeue()``. These functions
provide the production priority ordering, FIFO ordering, and bucket
maintenance for the priority-sorted intrusive list.

Rust owns:

* Priority placement in the serialized ``node_list``.

* FIFO ordering between nodes with the same priority.

* Selection and ordering of the priority buckets in ``prio_list``.

* Promotion of a replacement bucket representative during removal.

* The requeue decision and the move to the end of the same-priority group.

The original C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public ``plist_add()``, ``plist_del()``, and ``plist_requeue()``
interfaces and their signatures do not change. With this option enabled,
each C entry point performs the existing debug validation and calls the
matching Rust production function.

The C entry points retain the existing debug checks around each call.
``CONFIG_DEBUG_PLIST`` therefore checks the complete priority bucket and
serialized list before and after each Rust operation.

Rust reads and updates the intrusive ``plist_node`` and ``plist_head``
objects directly. It controls placement, bucket selection, promotion, and
requeue behavior.

Retained C boundaries
---------------------

The ``plist_head``, ``plist_node``, and ``list_head`` layouts remain in C.
These layouts are shared with C users in futex, real-time scheduling, swap,
and power management code.

Small C helpers retain ``list_add()``, ``list_add_tail()``,
``list_del_init()``, and list-empty behavior. These helpers use the
linked-list hardening, poisoning, write barriers, and debug rules in the
common list implementation.

The C entry points retain the public ABI and the complete fallback
algorithms. Callers continue to serialize all list operations.

Important invariants
--------------------

The ``node_list`` ring contains all nodes. It is sorted by increasing numeric
priority, so ``INT_MIN`` is first and ``INT_MAX`` is last. Nodes with equal
priority use FIFO order.

The ``prio_list`` ring has no sentinel. It contains one representative for
each priority when the list has multiple priorities. It is sorted in the same
order as ``node_list``.

A single-priority list can contain one bucket whose representative list is
self-linked. This state is valid because no second bucket needs a link.

When a bucket representative is removed, the next node with the same priority
becomes the representative. If no next node has the same priority, the bucket
is removed.

Requeue has no effect when the node is last. It also has no effect when the
next node has a different priority. Otherwise, the node moves after every
node with the same priority.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Ordered insertion and FIFO ordering for equal priorities.

* ``INT_MIN`` and ``INT_MAX`` priority boundaries.

* Bucket representative promotion during removal.

* Removal of the only node in a priority bucket.

* Requeue of the first, middle, and last same-priority entries.

* A model comparison with randomized add, remove, and requeue operations.

* ``CONFIG_DEBUG_PLIST`` validation before and after each operation.

* The same tests with native Rust priority lists disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Rust-enabled KASAN and lockdep runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that the enabled C object calls the Rust entry points and
  that fallback objects contain the complete C paths.

Validation results
------------------

Validation on 2026-11-17 used parent commit ``7fc9abd192c2``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_PLIST=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_PLIST=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration also enabled ``CONFIG_SMP=y``,
``CONFIG_DEBUG_PLIST=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_PLIST_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
two of two KUnit cases passed. The ordering case covered priority boundaries,
FIFO order, bucket creation, representative promotion, single-node buckets,
and requeue. The model case performed 2,000 seeded operations on up to 241
nodes.

The existing ``CONFIG_DEBUG_PLIST`` self-test completed in every run.
The KASAN and lockdep logs contained no KASAN report, BUG, warning, Oops,
lock inversion, recursive locking report, or RCU stall.

In the native configuration, ``lib/plist.o`` referenced
``rust_plist_add``, ``rust_plist_del``, and ``rust_plist_requeue``.
``rust/kernel.o`` defined all three symbols. The Rust-fallback and pure C
``lib/plist.o`` objects referenced no Rust priority-list symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and ``git diff --check``
passed. Strict checkpatch passed after suppressing the generic file-addition
warning. This patch adds the matching ``MAINTAINERS`` entries for its new
files. Focused Sphinx rendered this page and its index link. It reported only
the existing unknown ``kernel-docs`` reference in ``rust/index.rst``.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The ``plist_head``, ``plist_node``, or ``list_head`` layout changes.

* Priority direction, FIFO ordering, or representative promotion changes.

* Requeue behavior changes.

* Linked-list hardening, poisoning, barriers, or debug rules change.

* Rust stops controlling placement, bucket maintenance, promotion, or
  requeue policy.

* A fallback object starts to use a Rust priority-list symbol.
