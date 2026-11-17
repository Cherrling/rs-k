.. SPDX-License-Identifier: GPL-2.0

Native Rust list_lru operations
===============================

Status and scope
----------------

The ``CONFIG_RUST_LIST_LRU`` option selects native Rust implementations of
node-local ``list_lru`` operations. The option depends on x86_64 and
``CONFIG_RUST``.

The original C node-local algorithms remain available when the option is
disabled.

This unit covers add, delete, isolate, count, and walk operations. Memory
cgroup allocation and reparenting remain a separate migration unit.

Entry points and ownership
--------------------------

The public ``list_lru_*()`` interfaces keep their existing signatures and
export classes. When the option is enabled, their C entry points forward to
native Rust functions.

Rust owns:

* Item presence decisions for add and delete.

* List tail insertion, deletion, movement, and rotation decisions.

* Per-slice count transitions.

* The decision to notify the shrinker after the first item enters a slice.

* Per-node atomic count transitions.

* Count clamping for dead slices and transient negative states.

* Walk iteration, scan budget consumption, and restart handling.

* All six ``lru_status`` transitions.

* Isolated item accounting and per-node count decrements.

Retained C boundaries
---------------------

The intrusive layouts remain in C. The list nodes, list slices, per-node
counters, memcg slices, xarray, and shrinker identifiers keep their existing
ABI.

Small C helpers retain:

* ``list_empty()``, ``list_add_tail()``, ``list_del_init()``,
  ``list_move()``, and ``list_move_tail()`` primitives.

* ``READ_ONCE()`` and ``WRITE_ONCE()`` access to slice counts.

* Atomic per-node count load, increment, and decrement operations.

* Spinlock acquisition and release, including interrupt modes.

* Memory cgroup and xarray resolution.

* Shrinker bit notification.

* Invalid-status diagnostics.

These helpers perform storage operations and synchronization. Rust selects
every production count transition, list placement transition, and walk status
transition.

Initialization, destruction, memory cgroup allocation, and memory cgroup
reparenting remain C boundaries for a later unit.

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

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* The same tests with native Rust list_lru operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Add, duplicate add, delete, and duplicate delete.

* Isolate and isolate-move transitions.

* Slice count clamping and per-node counts.

* ``LRU_ROTATE``, ``LRU_SKIP``, ``LRU_REMOVED``, ``LRU_RETRY``,
  ``LRU_REMOVED_RETRY``, and ``LRU_STOP``.

* Concurrent add and delete workers on SMP.

* Rust-enabled KASAN and lockdep runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that the native C object calls every Rust entry point and
  that fallback objects reference no Rust list_lru symbol.

Validation results
------------------

On 2026-11-17, ten x86_64 virtual-machine boots passed. Each boot ran the
suite with one or four virtual CPUs. Every boot reported ``pass:10 fail:0
skip:0 total:10``.

The matrix covered the following configurations:

* Native Rust with ``CONFIG_RUST_LIST_LRU=y`` and ``CONFIG_MEMCG=y``.

* Rust fallback with ``CONFIG_RUST=y`` and ``CONFIG_RUST_LIST_LRU=n``.

* C-only fallback with ``CONFIG_RUST=n``.

* Native Rust with ``CONFIG_KASAN=y`` and
  ``CONFIG_KASAN_GENERIC=y``.

* Native Rust with ``CONFIG_PROVE_LOCKING=y``,
  ``CONFIG_DEBUG_SPINLOCK=y``, and ``CONFIG_LOCKDEP=y``.

The KASAN and lockdep boots reported no sanitizer error, lock cycle,
warning, panic, or RCU stall. The native and fallback builds reported no
compiler or linker warning.

Strict checkpatch reported one ``AVOID_BUG`` warning for the invalid-status
helper. The helper must terminate because an unknown callback status leaves
the lock disposition undefined. Recovery is not safe in that state.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The list, slice, node, or memory cgroup layout changes.

* Spinlock, interrupt, atomic, or memory-order semantics change.

* The callback lock contract or any ``lru_status`` transition changes.

* The scan budget, restart, stop, or isolate accounting changes.

* Rust stops controlling count transitions or walk transitions.

* A fallback object starts to use a Rust list_lru symbol.

* Another architecture passes the complete validation gate.
