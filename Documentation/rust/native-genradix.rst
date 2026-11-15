.. SPDX-License-Identifier: GPL-2.0

Native Rust generic radix tree migration
========================================

Status and scope
----------------

The ``CONFIG_RUST_GENRADIX`` option selects an experimental native Rust
implementation of the generic radix tree algorithms in
``lib/generic-radix-tree.c``. The option currently supports x86_64.

Rust owns the production implementation of these operation groups:

* Root growth, tagged root publication retry, and depth selection.
* Sparse lookup and allocation traversal.
* Forward and reverse sparse iteration.
* Page preallocation.
* Recursive release of a detached tree.
* Traversal for entries whose layout produces non-power-of-two offsets.

The existing C algorithms remain available when the option is disabled.

Entry points and ownership
--------------------------

The public inline interface and symbol names do not change. With
``CONFIG_RUST_GENRADIX`` enabled, the exported adapters in
``lib/generic-radix-tree.c`` forward directly to Rust and do not execute a
tree algorithm.

The six Rust entry points are:

* ``rust_genradix_ptr()`` and ``rust_genradix_ptr_alloc()``.
* ``rust_genradix_iter_peek()`` and ``rust_genradix_iter_peek_prev()``.
* ``rust_genradix_prealloc()`` and ``rust_genradix_free()``.

The C adapters retain the original ``__genradix_*`` symbol names and export
classes. The production algorithms are in ``rust/kernel/genradix.rs``.

Retained C boundaries
---------------------

Node allocation and release remain in C. ``kzalloc()`` creates zeroed pages,
and ``kfree()`` releases nodes. Rust owns when allocation and release occur.

The tagged root representation remains in C. The low depth bits, pointer
mask, and pointer reconstruction use the existing generic radix tree
definitions.

Atomic memory operations remain in C. Rust calls ``READ_ONCE()``,
``cmpxchg_release()``, and ``xchg()`` helpers for root and child access.

Scatter child installation remains in C. The empty-slot compare-and-exchange
uses the existing atomic pointer helper.

Leaf data pointer access remains in C. The helper exposes the existing union
member without duplicating its storage layout in Rust.

The public ``__genradix_*`` functions remain C ABI adapters. They forward all
out-of-line operations and retain the existing ordinary ``EXPORT_SYMBOL``
interfaces.

Important invariants
--------------------

A generic radix tree stores pages of ``GENRADIX_NODE_SIZE`` bytes. Interior
nodes contain ``GENRADIX_ARY`` child pointers. A leaf uses the same node
storage for zeroed entry data.

The root is a tagged pointer. Low bits store the tree depth, and the remaining
bits store the node address.

Root growth uses compare-and-exchange. A failed publication leaves the new
interior node private. A successful publication wraps the complete previous
root, so entries reachable before the change remain reachable.

Child installation uses compare-and-exchange only when a slot is empty.
Concurrent allocators can install different nodes, but only one node can own
an empty slot.

Sparse forward iteration rounds up to the next child range when a subtree is
missing. Sparse reverse iteration rounds down and checks for underflow.

Release exchanges the root with NULL before traversal. The detached subtree
has no concurrent users under the generic radix tree lifetime contract.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.
* All ten generic radix tree KUnit cases with repeated SMP4 execution.
* A Rust-disabled C fallback with the same KUnit cases.
* A Rust-enabled x86_64 KASAN build with SMP1 and repeated SMP4 execution.
* Rustfmt, Clippy, Rust documentation, strict checkpatch, and
  ``git diff --check``.
* Export-class checks for all six public adapters.
* Disassembly checks showing public adapters enter Rust and contain no tree
  algorithm.

Measured validation results
---------------------------

The final Rust-enabled image passed all ten generic radix tree KUnit cases on
one virtual CPU and in three repeated four-CPU runs. The Rust-disabled C
fallback and the Rust-enabled KASAN image passed the same four runs.

None of the 12 boot logs contained a BUG, Oops, kernel panic, KASAN report,
warning, call trace, or RCU stall.

The Rust image contained all six ``rust_genradix_*`` production entry points
and all six original ``__genradix_*`` exports. The C fallback contained no
``rust_genradix_*`` or ``rust_helper_genradix_*`` symbols.

Object-level disassembly showed six direct branches from the C adapters to
Rust. The Rust kernel object contained the traversal, root growth, iteration,
preallocation, and recursive release implementation.

Rustfmt, Clippy, Rustdoc, focused Rust Sphinx generation with warnings as
errors, and ``git diff --check`` passed. The focused Sphinx build suppressed
pre-existing unresolved cross-reference warnings that were unrelated to this
page.

Strict checkpatch passed with the expected file-addition warning suppressed
because this change also updates ``MAINTAINERS``.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The tagged root representation or depth encoding changes.
* The node layout or supported maximum entry size changes.
* A supported target needs a different atomic publication primitive.
* An allocator or atomic helper starts to select tree policy.
* A C adapter stops forwarding directly to Rust.
* A public interface starts to execute a C tree algorithm again.
* Performance data shows that an inline Rust path needs separate review.
