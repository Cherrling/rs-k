.. SPDX-License-Identifier: GPL-2.0

Native Rust klist operations
============================

Status and scope
----------------

The ``CONFIG_RUST_KLIST`` option selects native Rust implementations of klist
insertion, deletion, reference transitions, attachment state, and forward or
reverse iteration.

The option depends on x86_64. Other architectures use the complete C fallback
until their Rust builds and KUnit runs have passed the same validation gate.

The original C algorithms remain available when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

The public klist interfaces keep their existing signatures. With this option
enabled, the C entry points for node addition, deletion, attachment state, and
iteration forward to the matching Rust functions.

Rust owns:

* Initialization of a node's intrusive list link and initial reference.

* Placement at the head, tail, before a node, or after a node.

* The deleted-node bit stored in the low bit of ``n_klist``.

* Delayed removal while iterators hold references.

* Final node detachment and owner-pointer clearing.

* Iterator reference transfer between adjacent nodes.

* Dead-node skipping during forward and reverse traversal.

* The decision to invoke the owner's final put callback outside the lock.

Rust reads and updates the intrusive ``klist_node``, ``klist_iter``, and
``list_head`` objects directly.

Retained C boundaries
---------------------

The ``klist``, ``klist_node``, ``klist_iter``, ``list_head``, and ``kref``
layouts remain in C. These structures are shared with device core and other
kernel users.

Small C helpers retain spinlock acquisition, interrupt flag restoration,
``list_head`` pointer updates, list debugging, and refcount operations. These
helpers preserve the existing lockdep, lock poisoning, and architecture
specific refcount behavior.

``klist_init()`` remains in C because spinlock initialization is an
architecture and lock debugging boundary.

``klist_remove()`` remains in C. It registers a waiter, blocks in
``TASK_UNINTERRUPTIBLE``, and relies on the shared C wait and wakeup boundary.
It calls the active ``klist_del()`` implementation, including the native Rust
path.

The public C entry points retain the exported ABI. The complete C klist
algorithm remains compiled in fallback configurations.

Important invariants
--------------------

``n_klist`` is null for a detached node. For an attached node, the aligned
klist pointer occupies all bits except the low deleted bit.

A node starts with one reference. Iterators take an additional reference to
their current node.

``klist_del()`` marks the node dead and drops one reference. The node remains
linked while any iterator reference remains. The final reference detaches the
node, clears the owner pointer, and wakes matching ``klist_remove()`` waiters.

Final release runs while the owning klist spinlock is held. This preserves the
existing lock order between ``k_lock`` and the global remove-waiter lock.

The owner's ``put`` callback runs after the klist lock is released and only
when this operation dropped the final reference. The owner's ``get`` callback
runs after node initialization and before insertion.

Iteration skips dead nodes but continues to use their links until the final
reference removes them. Reverse iteration mirrors the same reference and dead
node rules.

Callers continue to serialize klist lifecycle operations under the existing
klist contract.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Head, tail, before, and behind insertion.

* Attachment state before, during delayed deletion, and after release.

* Immediate and delayed deletion with iterator-held references.

* Blocking ``klist_remove()``.

* Forward and reverse iteration from the list head and from a supplied node.

* Dead-node skipping and owner callbacks.

* Concurrent insertion, full-list iteration, and deletion on SMP.

* The same tests with native Rust klist operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Rust-enabled KASAN and lockdep runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that the native C object calls the Rust entry points and that
  fallback objects reference no Rust klist symbol.

Validation results
------------------

Validation on 2026-11-17 used parent commit ``9d0d7e2a4ec6``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_KLIST=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_KLIST=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, ``CONFIG_RUNTIME_TESTING_MENU=y``, and
``CONFIG_LIST_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
eleven of eleven KUnit cases passed. The suite covered the original eight
klist cases plus reverse and supplied-node iteration, dead-node skipping, and
a concurrent stress case.

The concurrent case used two kthreads. Each worker performed 256 unique
insertions, traversed the complete shared list, and deleted its node. This
produced 512 concurrent insert, iterate, and delete cycles.

The KASAN run initialized generic KASAN. The KASAN and lockdep logs contained
no KASAN report, BUG, warning, Oops, lock inversion, recursive locking report,
or RCU stall.

In the native configuration, ``lib/klist.o`` referenced all eleven
``rust_klist_*`` entry points and defined the shared
``klist_wake_waiters()`` boundary. ``rust/kernel.o`` defined all eleven Rust
symbols. The Rust-fallback and pure C ``lib/klist.o`` objects referenced no
Rust klist symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported no errors, warnings,
or checks. Focused Sphinx rendered this page and the updated index.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* Klist, node, iterator, list, or kref layout changes.

* Spinlock, interrupt, or lockdep semantics change.

* Refcount or list debugging behavior changes.

* The dead-bit representation or lock order changes.

* Blocking remove, waiter registration, or wakeup semantics change.

* Iterator reference transfer or dead-node skipping changes.

* Rust stops controlling insertion, lifecycle transitions, or iteration.

* A fallback object starts to use a Rust klist symbol.

* A second architecture passes the complete validation gate.
