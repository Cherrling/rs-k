.. SPDX-License-Identifier: GPL-2.0

Native Rust list sort
=====================

Status and scope
----------------

The ``CONFIG_RUST_LIST_SORT`` option selects the native Rust implementation
of ``list_sort()``. Rust directly defines the public production entry point.
The function performs a stable sort of an intrusive circular doubly-linked
list.

Rust owns:

* Stable bottom-up merge ordering.

* The pending-list state used to select merges.

* Comparator invocation and the decision to merge a pair of sublists.

* Final merging and restoration of the circular doubly-linked list.

The original C algorithm remains available when the option is disabled.

Entry point and ownership
-------------------------

The public ``list_sort()`` interface and its signature do not change. With
this option enabled, Rust directly defines the symbol and executes the
complete sorting algorithm.

Rust reads and updates the intrusive ``list_head`` objects directly. It
controls merge order, pending-list bookkeeping, comparison order, and the
final reconstruction of the ``next`` and ``prev`` links.

Retained C boundaries
---------------------

The ``list_head`` layout and the ``list_cmp_func_t`` callback ABI remain in C.
These interfaces are shared with C callers and modules.

The native ``lib/list_sort.o`` object retains only the historical
``EXPORT_SYMBOL(list_sort)`` metadata. Rust normally exports annotated
functions as GPL, so ``rust/Makefile`` explicitly excludes ``list_sort`` from
automatic GPL exports.

The complete C fallback algorithm remains compiled when the option is
disabled. Callers continue to serialize all list operations.

Important invariants
--------------------

The input is a circular doubly-linked list with a sentinel head. During
sorting, Rust converts the input nodes to null-terminated singly-linked
lists and does not maintain their ``prev`` links until the final merge.

The pending list contains sorted sublists whose sizes are powers of two. Each
pair merges when the increment state requires it. This keeps the final merge
at worst 2:1 balanced.

The comparator must obey antisymmetry and transitivity. A comparison result
less than or equal to zero selects the first input node. This rule preserves
the original order of equal elements.

The final merge rebuilds every ``prev`` link. The last node links back to the
sentinel, and the sentinel links to both the first and last nodes.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Ordering and stability with many equal sort keys.

* Rejection of phantom elements, poison corruption, and changed list length.

* Empty, single-element, and two-element lists.

* The same tests with native Rust list sort disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Rust-enabled KASAN, lockdep, and PREEMPT_RT runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that Rust defines ``list_sort()``, the native C object
  contains no function body, the fallback and C-only objects contain the
  complete C algorithm, and the export class remains non-GPL.

Validation results
------------------

Validation on 2026-11-23 used parent commit ``c282602f44e7``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_LIST_SORT=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus
                  ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_DEBUG_KERNEL=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_TEST_LIST_SORT=y``.

All nine QEMU runs reported two of two KUnit cases passed. The ordering case
used 642 nodes, forced equal sort keys, and checked poison values, phantom
elements, sorted order, stable order, ``prev`` links, and the list length.
The edge case checked empty, single-element, and two-element lists.

The KASAN configuration used generic inline KASAN. The lockdep configuration
enabled lockdep and RCU lockdep checking. The RT kernel reported
``SMP PREEMPT_RT``. No run reported KASAN, a BUG, warning, Oops, kernel
panic, lock inversion, or RCU stall.

The adjacent regression passed all 39 ``list-kunit-test`` cases, the
``list-private-kunit-test`` case, both ``list_sort`` cases, and both
``plist`` cases.

In the native configuration, ``rust/kernel.o`` defined ``list_sort()``.
The native ``lib/list_sort.o`` object had no function body for the symbol.
``vmlinux.symvers`` contained one ``EXPORT_SYMBOL(list_sort)`` entry, and the
generated Rust GPL export header excluded the symbol. The Rust-fallback and
pure C ``lib/list_sort.o`` objects defined the complete C function, while the
fallback Rust kernel object contained no public list sort symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, kernel-doc, strict
checkpatch, and ``git diff --check`` passed. Focused Sphinx generation emitted
only the existing unknown ``kernel-docs`` document and ``getting_llvm`` label
warnings.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The ``list_head`` layout or linked-list debug rules change.

* The comparator callback ABI or return-value contract changes.

* Merge selection, stability, or final list restoration changes.

* Rust stops defining the public entry point.

* A fallback object starts to use a Rust list sort symbol.

* The public function gains a different module export class.
