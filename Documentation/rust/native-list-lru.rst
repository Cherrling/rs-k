.. SPDX-License-Identifier: GPL-2.0

Native Rust list_lru operations
===============================

Status and scope
----------------

The ``CONFIG_RUST_LIST_LRU`` option selects native Rust implementations of
the node-local ``list_lru`` operations. The option depends on x86_64 and
``CONFIG_RUST``.

Rust directly defines fourteen production entry points for add, delete,
isolate, count, and walk operations. The original C node-local algorithms
remain available when the option is disabled.

This unit covers node-local list and count transitions. Memory cgroup slice
allocation and reparenting remain a separate lifecycle boundary.

Entry points and ownership
--------------------------

The public ``list_lru_*()`` interfaces keep their existing signatures and
export classes. With this option enabled, Rust directly defines the symbols.

Rust defines these exported GPL functions:

* ``list_lru_add()``

* ``list_lru_add_obj()``

* ``list_lru_del_obj()``

* ``list_lru_isolate()``

* ``list_lru_isolate_move()``

* ``list_lru_count_one()``

* ``list_lru_count_node()``

* ``list_lru_walk_one()``

* ``list_lru_walk_node()``

Rust also defines ``list_lru_add_irq()``, ``list_lru_del()``, and
``list_lru_walk_one_irq()``. These public functions remain unexported.
The caller-locked ``__list_lru_add()`` and ``__list_lru_del()`` functions are
internal Rust entry points and also remain unexported.

Rust owns:

* Item presence decisions for add and delete.

* List tail insertion, deletion, movement, and rotation decisions.

* Per-slice and per-node count transitions.

* The decision to notify the shrinker when the first item enters a slice.

* Count clamping for dead slices and transient negative states.

* Walk iteration, callback dispatch, and scan budget consumption.

* Every ``lru_status`` transition.

* Restart, stop, and isolated item accounting.

Retained C boundaries
---------------------

The intrusive layouts remain in C. List nodes, slices, nodes, memory-cgroup
slices, and the xarray keep their existing ABI.

Small C helpers retain:

* ``list_empty()``, ``list_add_tail()``, ``list_del_init()``,
  ``list_move()``, and ``list_move_tail()`` primitives.

* ``READ_ONCE()`` and ``WRITE_ONCE()`` access to slice counts.

* Atomic per-node count load, increment, and decrement operations.

* Spinlock acquisition and release, including interrupt modes.

* RCU protection, object page-to-memcg lookup, and xarray traversal.

* Shrinker bit notification.

* Invalid-status diagnostics.

These helpers perform storage and synchronization operations. Rust selects
every production count transition, list placement transition, and walk status
transition.

Initialization, destruction, dynamic per-memcg slice allocation, and memcg
reparenting remain C boundaries.

The native ``mm/list_lru.o`` object retains the lock accessor functions,
``__list_lru_init()``, and ``list_lru_destroy()``. It does not define the
node-local production algorithms.

Export classes
--------------

Nine Rust-defined node-local functions retain their historical GPL exports.
The three unexported public functions and the two caller-locked helpers have
no module export.

``rust/Makefile`` excludes all five unexported functions from the automatic
Rust GPL exporter. This prevents accidental expansion of the module ABI.

Important invariants
--------------------

A self-linked item is not queued. Add and delete use this state to make
duplicate operations idempotent for an item that belongs to the selected
slice.

Adding the first item to a slice increments the slice count from zero and
notifies the shrinker. Adding every item increments the per-node atomic
count. Deleting every queued item decrements both counts.

``list_lru_isolate()`` and ``list_lru_isolate_move()`` decrement only the
slice count. The walker decrements the per-node count after the callback
returns ``LRU_REMOVED`` or ``LRU_REMOVED_RETRY``.

The walker consumes one unit of ``nr_to_walk`` before it invokes the callback.
This prevents an unbounded retry loop.

Restart and stop callbacks release the slice lock before return. The Rust
walker reacquires the lock for ``LRU_RETRY`` and ``LRU_REMOVED_RETRY``. It
returns directly for ``LRU_STOP``.

Slice counts can become negative during memory cgroup reparenting.
``list_lru_count_one()`` reports zero for these dead or transient states.

Callback pointers, item links, and lock ownership follow the existing C
contract. Unknown callbacks and unknown status values terminate through the
existing invalid-status diagnostic.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* The same tests with native Rust list LRU operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Add, duplicate add, delete, and duplicate delete.

* Object-backed add and delete.

* Isolate and isolate-move transitions.

* Slice count clamping and per-node counts.

* ``LRU_ROTATE``, ``LRU_SKIP``, ``LRU_REMOVED``, ``LRU_RETRY``,
  ``LRU_REMOVED_RETRY``, and ``LRU_STOP``.

* Interrupt lock and whole-node walk variants.

* Concurrent add and delete workers on SMP.

* Rust-enabled KASAN, lockdep, and PREEMPT_RT runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that Rust defines all fourteen entry points, the native C
  object contains no node-local production algorithm, and fallback objects
  contain the complete C implementation.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``f08ee8ccfdc1``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_LIST_LRU=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus
                  ``CONFIG_KASAN_GENERIC=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_MEMCG=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_LIST_LRU_KUNIT_TEST=y``.

All nine QEMU runs reported twelve of twelve KUnit cases passed. The suite
checked duplicate operations, isolate accounting, count clamping,
object-backed operations, every walk status, interrupt lock variants,
whole-node walks, and four concurrent add and delete workers.

The KASAN configuration used generic inline KASAN. The lockdep configuration
enabled lockdep, spinlock debugging, and RCU lockdep checking. The RT kernel
reported ``SMP PREEMPT_RT``. No run reported KASAN, a BUG, warning, Oops,
kernel panic, lock inversion, or RCU stall.

The adjacent regression passed all 39 ``list-kunit-test`` cases, the
``list-private-kunit-test`` case, both ``list_sort`` cases, and both
``plist`` cases.

In the native configuration, ``rust/kernel.o`` defined all fourteen
node-local entry points. The native ``mm/list_lru.o`` object defined only the
lock accessors, ``__list_lru_init()``, and ``list_lru_destroy()`` from this
unit. ``vmlinux.symvers`` contained the nine historical
``EXPORT_SYMBOL_GPL()`` entries, and the generated Rust GPL export header
excluded the five unexported functions.

The Rust-fallback and pure C ``mm/list_lru.o`` objects defined the complete
node-local C implementation. The fallback Rust kernel object contained no
public list LRU symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, kernel-doc, strict
checkpatch, and ``git diff --check`` passed. Focused Sphinx generation emitted
only the existing unknown ``kernel-docs`` document and ``getting_llvm`` label
warnings.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The list, slice, node, memory-cgroup, or xarray layout changes.

* Spinlock, interrupt, RCU, atomic, or memory-order semantics change.

* The callback lock contract or any ``lru_status`` transition changes.

* The scan budget, restart, stop, or isolate accounting changes.

* Rust stops controlling node-local count or walk transitions.

* A fallback object starts to use a Rust list LRU symbol.

* The public function gains a different module export class.

* Another architecture passes the complete validation gate.
