.. SPDX-License-Identifier: GPL-2.0

Native Rust generic radix trees
===============================

Status and scope
----------------

The ``CONFIG_RUST_GENRADIX`` option selects the native Rust implementation of
the generic radix tree. The option depends on x86_64 and ``CONFIG_RUST``.

Rust directly defines the six public production entry points. It owns root
growth, tagged-root publication retries, sparse traversal, allocation policy,
forward and reverse iteration, preallocation, and recursive release.

The original C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public ``__genradix_*()`` interfaces keep their existing signatures and
ordinary non-GPL export class. With this option enabled, Rust defines these
symbols:

* ``__genradix_ptr()``

* ``__genradix_ptr_alloc()``

* ``__genradix_iter_peek()``

* ``__genradix_iter_peek_prev()``

* ``__genradix_prealloc()``

* ``__genradix_free()``

Rust owns:

* Root depth selection and root-growth retry decisions.

* Tagged-root observation and publication ordering.

* Existing-entry lookup and sparse allocation traversal.

* Empty-child compare-and-exchange installation decisions.

* Forward and reverse iterator advancement across missing subtrees.

* Page preallocation and partial-allocation error handling.

* Detached-subtree traversal and recursive release order.

* Non-power-of-two object offset transitions used by the iterator.

Retained C boundaries
---------------------

The generic radix tree layouts remain in C. The tagged root, interior child
array, leaf data union, iterator state, and type-aware container macros keep
their existing ABI.

Small C helpers retain:

* Zeroed node allocation and node release.

* Tagged-root and depth encoding.

* ``READ_ONCE()`` child and root loads.

* Release compare-exchange for root growth and empty-child installation.

* Null root exchange during release.

* Leaf data pointer access without duplicating the union layout.

These helpers perform allocation, layout, and atomic publication operations.
Rust selects every traversal, growth, allocation-policy, iterator, and release
transition.

The type-aware ``GENRADIX()`` wrappers, offset conversion, compile-time object
size checks, and inline iterator helpers remain in C.

The native ``lib/generic-radix-tree.o`` object retains only the six
``EXPORT_SYMBOL()`` metadata entries. It does not define a production
algorithm.

Export classes
--------------

All six public functions retain their historical ordinary ``EXPORT_SYMBOL()``
class. Rust normally emits GPL exports, so ``rust/Makefile`` excludes all six
names from the automatic Rust exporter.

Important invariants
--------------------

A generic radix tree stores pages of ``GENRADIX_NODE_SIZE`` bytes. Interior
nodes contain ``GENRADIX_ARY`` child pointers. A leaf uses the same storage as
zeroed entry data.

The root is a tagged pointer. Low bits store the tree depth, and the remaining
bits store the node address.

Root growth uses compare-and-exchange. A failed publication leaves the new
interior node private. A successful publication wraps the complete previous
root, so existing entries remain reachable.

Child installation uses compare-and-exchange only when a slot is empty.
Concurrent allocators can install different nodes, but only one node owns an
empty slot.

Sparse forward iteration rounds up to the next child range when a subtree is
missing. Sparse reverse iteration rounds down and detects underflow.

Release exchanges the root with NULL before traversal. The generic radix tree
lifetime contract excludes concurrent users during release.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* All ten generic radix tree KUnit cases.

* The same tests with ``CONFIG_RUST_GENRADIX`` disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Sparse allocation at shallow and deep indices.

* Zeroed allocation, preallocated-node reuse, and page preallocation.

* Forward and reverse iteration across missing pages.

* Non-power-of-two object layouts.

* Recursive release and empty-tree reset.

* Concurrent allocation on SMP.

* Rust-enabled KASAN, lockdep, and PREEMPT_RT runs.

* Adjacent XArray, IDR, maple tree, and B-tree KUnit regression.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that Rust defines all six entry points, the native C object
  contains only export metadata, and fallback objects contain the complete C
  implementation.

Validation results
------------------

Validation on 2026-11-24 used parent commit ``a68a823f29c3``.

================  ============================================================
Build             Configuration
================  ============================================================
Native SMP1       Native Rust, one virtual CPU
Native SMP4       Native Rust, four virtual CPUs
Rust fallback     ``CONFIG_RUST_GENRADIX=n``, one and four virtual CPUs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust configuration plus
                  ``CONFIG_KASAN_GENERIC=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
Native RT         Native Rust configuration plus ``CONFIG_PREEMPT_RT=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_MODULES=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_GENRADIX_KUNIT_TEST=y``.

All nine QEMU runs reported ten of ten KUnit cases passed. The suite checked
empty and zeroed allocation, sparse and deep indices, page preallocation,
preallocated-node reuse, forward and reverse iteration, release and reset,
non-power-of-two entries, and concurrent allocation.

The KASAN configuration used generic inline KASAN. The lockdep configuration
enabled lockdep, spinlock debugging, and RCU lockdep checking. The RT kernel
reported ``SMP PREEMPT_RT``. No run reported KASAN, a BUG, warning, Oops,
kernel panic, lock inversion, or RCU stall.

The adjacent XArray, IDR, maple tree, and B-tree KUnit suites passed on SMP4.

In the native configuration, ``rust/kernel.o`` defined all six public entry
points. The native ``lib/generic-radix-tree.o`` object had no function body
for those symbols. ``Module.symvers`` recorded six ordinary ``EXPORT_SYMBOL``
entries, and the generated Rust GPL export header excluded all six names.

The Rust-fallback and pure C ``lib/generic-radix-tree.o`` objects defined the
complete C implementation. The fallback Rust kernel object contained no
public generic radix tree symbol.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, kernel-doc, strict
checkpatch, and ``git diff --check`` passed. Focused Sphinx generation emitted
only the existing unknown ``kernel-docs`` document and ``getting_llvm`` label
warnings.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The tagged root representation, depth encoding, node layout, or iterator
  layout changes.

* The supported maximum entry size or non-power-of-two offset rules change.

* A supported target needs a different atomic publication primitive.

* An allocator or atomic helper starts to select traversal or growth policy.

* The inline container API starts to execute a C production algorithm.

* Rust stops defining a public out-of-line entry point.

* A fallback object starts to use a Rust generic radix tree symbol.

* The public functions gain a different module export class.

* Another architecture passes the complete validation gate.
