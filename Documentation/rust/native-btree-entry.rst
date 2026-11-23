.. SPDX-License-Identifier: GPL-2.0

Native Rust B+Tree operations
=============================

Status and scope
----------------

The ``CONFIG_RUST_BTREE`` option selects native Rust implementations of the
nine production B+Tree algorithm entry points. Rust directly defines every
public symbol in this scope and owns all traversal, update, structural, merge,
and visitor decisions.

The earlier traversal, structural, and visitor migration documents describe
the algorithms in detail:

* :doc:`native-btree-traversal`

* :doc:`native-btree-structural`

* :doc:`native-btree-visitors`

The C fallback retains the complete algorithms when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these public GPL entry points:

* ``btree_lookup()``

* ``btree_update()``

* ``btree_last()``

* ``btree_get_prev()``

* ``btree_insert()``

* ``btree_remove()``

* ``btree_merge()``

* ``btree_visitor()``

* ``btree_grim_visitor()``

Rust owns:

* Key storage access, comparison, copying, zero detection, and predecessor
  underflow.

* Leaf lookup, leaf value publication, greatest-entry selection, ancestor
  path fallback, and predecessor retry.

* Node splitting, parent insertion, root growth, root shrink, underfull-node
  recovery, and node-content transfer.

* Ordinary tree merging and empty-target root transfer.

* Recursive visitor traversal, entry indices, callback dispatch, count-only
  visitors, reclamation, and tree reset.

The native ``lib/btree.o`` defines none of these nine function bodies. Rust
generates their GPL exports. The C fallback and C-only configurations define
the complete implementations and retain the original GPL export metadata.

Retained C boundaries
---------------------

The ``btree_head``, ``btree_geo``, and node array layouts remain in
``include/linux/btree.h``. Rust interprets the geometry and accesses the
shared key and value slots directly.

The mempool allocation and release primitives remain in ``lib/btree.c``.
``btree_rust_node_alloc()`` returns a zeroed node, and
``btree_rust_node_free()`` returns it to the tree mempool. Rust owns every
allocation decision and rollback path.

The four typed visitor adapters remain in C and retain their public callback
signatures. They convert the shared machine-word key representation to the
32-bit, 64-bit, or 128-bit callback type before invoking the caller.

The geometry objects, lifecycle functions, allocator callbacks, and module
initialization remain in C. These interfaces are outside the migrated
algorithm scope.

Important invariants
--------------------

Values and entry keys are non-NULL. Duplicate keys violate the public
contract and use the kernel fatal diagnostic path.

Internal-node keys select the first child whose boundary key is less than or
equal to the search key. A missing rightmost boundary is repaired before
descent.

A full node splits at half its pair capacity. An allocation failure releases
every node not yet linked into the tree and leaves the existing tree valid.

Removal shifts remaining entries left, clears the vacated pair, and merges an
underfull node with a sibling when possible. A non-leaf root with one child
shrinks to that child.

Ordinary merge moves entries from greatest to least. A failure can leave
entries already moved in the target, as permitted by the public API.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four virtual CPUs.

* Rust fallback builds with ``CONFIG_RUST_BTREE=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep and prove-locking.

* Rust-enabled PREEMPT_RT with four virtual CPUs.

* Traversal, structural, allocation-failure, merge, visitor, grim visitor,
  reclamation, and reuse tests for 32-bit, 64-bit, and 128-bit keys.

* Symbol checks that Rust defines all nine public entry points, the native C
  object defines none of their function bodies, and the fallback and C-only
  objects define the complete implementations with GPL exports.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation runs on 2026-11-24 with x86_64 LLVM builds and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_BTREE=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used ``CONFIG_BTREE=y``, ``CONFIG_SMP=y``,
``CONFIG_NR_CPUS=4``, ``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``,
``CONFIG_BTREE_TRAVERSAL_KUNIT_TEST=y``,
``CONFIG_BTREE_STRUCTURAL_KUNIT_TEST=y``, and
``CONFIG_BTREE_VISITOR_KUNIT_TEST=y``.

All six configurations compiled successfully. All nine QEMU runs passed 14
of 14 tests: four traversal cases, six structural cases, and four visitor
cases.

The KASAN run initialized generic KASAN. The lockdep run enabled lock
dependency tracking and prove-locking. The RT kernel reported an
``SMP PREEMPT_RT`` build. The logs contained no BUG, warning, KASAN report,
lock inversion, Oops, panic, or RCU stall.

``rust/kernel.o`` defined all nine public entry points. The native
``lib/btree.o`` defined none of the nine function bodies and retained only
the two node allocation helpers. The fallback and C-only objects defined all
nine bodies and GPL exports.

``vmlinux.symvers`` recorded nine ``EXPORT_SYMBOL_GPL`` entries. The
generated Rust GPL export header listed all nine public names.

Adjacent native SMP4 regressions passed: four red-black tree cases, three
interval tree cases, two list-sort cases, two priority-list cases, and ten
generic radix tree cases.

Rustfmt, Clippy, Rustdoc, kernel-doc, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx generation emitted only the two
existing warnings for ``../process/kernel-docs`` and the ``getting_llvm``
label.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* B+Tree key ordering, geometry, node layout, or empty-slot termination.

* Mempool allocation, zeroing, release, or ownership semantics.

* Root growth, split placement, underfull rebalancing, or reclamation policy.

* Visitor order, indices, callback signatures, or NULL-callback behavior.

* Empty-target merge or partial-failure ownership semantics.

* Rust stops defining a public algorithm entry point or owning a policy
  decision.
