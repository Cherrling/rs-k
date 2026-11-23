.. SPDX-License-Identifier: GPL-2.0

Native Rust priority lists
==========================

Status and scope
----------------

The ``CONFIG_RUST_PLIST`` option selects native Rust implementations of
``plist_add()``, ``plist_del()``, and ``plist_requeue()``. These functions
provide the production priority ordering, FIFO ordering, and bucket
maintenance for the priority-sorted intrusive list. Rust directly defines
all three public production entry points.

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
Rust directly defines all three symbols and preserves the original debug
validation sequence.

Rust reads and updates the intrusive ``plist_node`` and ``plist_head``
objects directly. It controls placement, bucket selection, promotion, and
requeue behavior. It also decides when to run the head validation, warning
checks, and fatal requeue checks. ``CONFIG_DEBUG_PLIST`` therefore checks the
complete priority bucket and serialized list before and after each Rust
operation.

Retained C boundaries
---------------------

The ``plist_head``, ``plist_node``, and ``list_head`` layouts remain in C.
These layouts are shared with C users in futex, real-time scheduling, swap,
and power management code.

Small C helpers retain ``list_add()``, ``list_add_tail()``,
``list_del_init()``, and list-empty behavior. These helpers use the
linked-list hardening, poisoning, write barriers, and debug rules in the
common list implementation.

C helpers also retain ``plist_check_head()``, ``WARN_ON()``, and
``BUG_ON()`` diagnostics. They do not select list placement, promotion, or
requeue policy.

The three public priority-list functions are not exported to modules. Rust
normally exports annotated functions as GPL, so ``rust/Makefile`` explicitly
excludes ``plist_add``, ``plist_del``, and ``plist_requeue`` from automatic
exports. No export metadata is added in C.

The complete C fallback algorithms remain compiled when the option is
disabled. Callers continue to serialize all list operations.

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

* Rust-enabled KASAN, lockdep, and PREEMPT_RT runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that Rust defines all three public entry points, the native C
  object contains no corresponding function body, the fallback and C-only
  objects contain the complete C implementation, and no new module exports
  are created.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``609bc1c0e71c``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_PLIST=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_DEBUG_KERNEL=y``, ``CONFIG_DEBUG_PLIST=y``,
``CONFIG_KUNIT=y``, and
``CONFIG_PLIST_KUNIT_TEST=y``.

All nine QEMU runs reported two of two KUnit cases passed. The ordering case
covered priority boundaries, FIFO order, bucket creation, representative
promotion, single-node buckets, and requeue. The model case performed 2,000
seeded operations on up to 241 nodes.

The existing ``CONFIG_DEBUG_PLIST`` self-test completed in every run.
The KASAN and lockdep logs contained no KASAN report, BUG, warning, Oops,
lock inversion, recursive locking report, or RCU stall. The KASAN run
initialized generic KASAN. The RT kernel reported ``SMP PREEMPT_RT``.

The adjacent list regression passed all 39 ``list-kunit-test`` cases, the
single ``list-private-kunit-test`` case, and both ``list_sort`` cases.

In the native configuration, ``rust/kernel.o`` defined ``plist_add()``,
``plist_del()``, and ``plist_requeue()``. The native ``lib/plist.o`` defined
no public priority-list function. It retained the list primitives and debug
helpers ``rust_plist_check_head()``, ``rust_plist_warn_on()``, and
``rust_plist_bug_on()``.

``vmlinux.symvers`` contained no entry for the three functions. The generated
Rust GPL export header also excluded them. The Rust-fallback and pure C
``lib/plist.o`` objects defined all three public functions, and the fallback
Rust kernel object contained no public priority-list symbol.

Rustfmt, Clippy, and Rustdoc passed. Strict checkpatch reported one
``AVOID_BUG`` warning for ``rust_plist_bug_on()``. The helper must preserve
the fatal ``BUG_ON()`` behavior of invalid requeue calls because the lock and
list disposition is undefined after the invariant fails. With that
intentional warning ignored, checkpatch reported no errors, warnings, or
checks. Focused Sphinx generation and ``git diff --check`` passed.
Focused Sphinx generation emitted only the existing unknown ``kernel-docs``
document and ``getting_llvm`` label warnings.

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

* The public functions gain a module export class.
