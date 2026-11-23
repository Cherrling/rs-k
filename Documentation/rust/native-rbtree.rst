.. SPDX-License-Identifier: GPL-2.0

Native Rust red-black tree operations
=====================================

Status and scope
----------------

The ``CONFIG_RUST_RBTREE`` option selects native Rust implementations of the
intrusive red-black tree operations used by the kernel.

Rust directly defines the public production entry points. It owns insertion,
removal, augmented red-black tree callbacks, traversal, node replacement, and
linked-tree maintenance. The complete existing C implementation remains
available when the option is disabled or when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these public interfaces:

* ``rb_insert_color()``

* ``rb_erase()``

* ``rb_erase_linked()``

* ``__rb_insert_augmented()``

* ``__rb_erase_color()``

* ``rb_next()``

* ``rb_prev()``

* ``rb_replace_node()``

* ``rb_replace_node_rcu()``

* ``rb_next_postorder()``

* ``rb_first_postorder()``

Rust owns:

* Red-black color repair after intrusive node linking.

* Removal, successor selection, and color rebalancing.

* Augmented callback invocation for propagation, copy, and rotation.

* Forward, reverse, and postorder traversal.

* Normal and RCU-protected node replacement.

* Maintenance of the optional doubly-linked order.

The native ``lib/rbtree.o`` defines no public red-black tree function body.
It retains the ten historical non-GPL ``EXPORT_SYMBOL()`` metadata entries.
Rust generates the GPL export for ``rb_erase_linked()`` and the internal
``rust_rb_erase_augmented()`` dispatch helper.

Retained C boundaries
---------------------

The ``rb_node``, ``rb_root``, ``rb_node_linked``, and ``rb_root_linked``
layouts remain in shared C headers. The parent pointer and color use the low
bits of ``__rb_parent_color``. Inline linking helpers initialize this shared
representation.

The augmented callback structure and the inline augmented erase dispatcher
remain in ``include/linux/rbtree_augmented.h``. The dispatcher calls the
internal Rust ``rust_rb_erase_augmented()`` operation so Rust can coordinate
callbacks, node replacement, and rebalancing as one state transition.

C also retains the non-GPL export metadata for the ten historical public
symbols. ``rust/Makefile`` excludes those names from automatic Rust GPL
exports. This preserves the existing module ABI without duplicate exports.

The complete C red-black tree algorithms remain compiled in fallback
configurations.

Important invariants
--------------------

The root is black. A red node has no red child. Every path from a node to a
leaf has the same number of black nodes. The parent pointer and color are
packed into ``__rb_parent_color``.

Child pointer publication uses atomic write-once semantics. RCU replacement
publishes the changed parent or root pointer after the replacement node and
its child links are visible.

Augmented trees propagate the aggregate value from the modified node to the
root. Rotations update the augmented state through the registered callback.

For linked trees, traversal in key order matches the doubly-linked order.
Erasing a linked node preserves both the red-black tree and the linked order.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_RBTREE=n`` and the same tests.

* C-only builds with ``CONFIG_RUST=n`` and the same tests.

* Native Rust with KASAN.

* Native Rust with lockdep and prove-locking.

* Rust-enabled PREEMPT_RT with four CPUs.

* Insertion and removal checks for color, red-node, and black-height
  invariants.

* Forward, reverse, and postorder traversal.

* Augmented subtree maintenance after every mutation.

* Normal and RCU-protected replacement, including replacement of the root.

* Linked-tree insertion and removal.

* The adjacent interval tree suite in native, fallback, and C-only builds.

* Symbol checks that Rust defines all eleven public entry points, the native C
  object contains no corresponding function body, the fallback and C-only
  objects contain the complete C implementation, and the mixed GPL and
  non-GPL export classes are unchanged.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``d9c11af53ed7``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_RBTREE=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_RBTREE_RUST_KUNIT_TEST=y``.

All nine QEMU runs reported four of four KUnit cases passed.

The insertion and removal case inserted 128 nodes in key order, checked tree
invariants after every insertion and removal, and verified forward, reverse,
and postorder traversal. The augmented case checked aggregate subtree values
after every mutation. The replacement case replaced interior nodes and the
root through normal and RCU interfaces. The linked case inserted and removed
128 nodes while checking both key order and doubly-linked order.

The KASAN run initialized generic KASAN. The KASAN and lockdep logs contained
no KASAN report, BUG, warning, Oops, lock inversion, recursive locking report,
or RCU stall. The lockdep run initialized expanded lock dependency state. The
RT kernel reported ``SMP PREEMPT_RT``.

The interval tree regression passed all three cases in native, Rust fallback,
and C-only builds.

In the native configuration, ``rust/kernel.o`` defined all eleven public
symbols and ``rust_rb_erase_augmented()``. The native ``lib/rbtree.o`` defined
no public function and contained only export metadata.

``vmlinux.symvers`` recorded the ten historical interfaces as
``EXPORT_SYMBOL``. ``rb_erase_linked()`` and
``rust_rb_erase_augmented()`` were ``EXPORT_SYMBOL_GPL``. The generated Rust
GPL export header contained only those two symbols. The Rust-fallback and pure
C ``lib/rbtree.o`` objects defined all eleven public functions. The Rust
fallback kernel object contained no public red-black tree symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch reported no errors, warnings,
or checks. Focused Sphinx generation emitted only the existing unknown
``kernel-docs`` document and ``getting_llvm`` label warnings.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The node, root, linked-node, or color encoding layout changes.

* The augmented callback interface or inline dispatch changes.

* The non-GPL export class or C export metadata changes.

* Rust stops controlling insertion, removal, augmentation, traversal,
  replacement, or linked-tree maintenance.

* A fallback object starts to use a Rust red-black tree public symbol.

* A new architecture enables the native implementation before passing the
  complete validation gate.
