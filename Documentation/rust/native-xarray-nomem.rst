.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray allocation lifecycle
=======================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_NOMEM`` option selects experimental native Rust
implementations for ``xas_nomem()`` and ``xas_destroy()``.

Rust owns these production operation groups:

* ENOMEM state detection and the decision to allocate.

* Pending preallocation chain traversal and release.

* Allocation retention and cursor restart.

The C fallback retains the complete original allocation-lifecycle policy.

Entry points and ownership
--------------------------

The public ``xas_nomem()`` and ``xas_destroy()`` interfaces and their
``EXPORT_SYMBOL_GPL`` export classes do not change.

With this option enabled, both entry points in ``lib/xarray.c`` forward
directly to exported Rust functions. The production implementation is in
``rust/kernel/xarray_nomem.rs``.

The static ``__xas_nomem()`` locking variant remains in C. It continues to use
the selected ``xas_destroy()`` implementation for stale preallocation cleanup.

Retained C boundaries
---------------------

The XArray node and state layouts remain C. Slab allocation uses
``radix_tree_node_cachep`` and the operation's optional LRU. Rust does not
encode or depend on the slab object's internal layout.

Node preparation remains C. The boundary clears the preallocation parent link
and runs the XArray debug invariant for an empty private list.

The raw RCU parent read remains C. The preallocation chain is detached from
the tree, but it uses the same parent field and annotation rules as other
XArray node chains.

The synchronous node reclamation primitive remains C. The boundary runs the
private-list invariant before it returns the node to the radix-tree slab.

Important invariants
--------------------

An XArray cursor that does not contain ``-ENOMEM`` releases every stale
preallocation and returns false. Its error or cursor state is otherwise
unchanged.

When ``-ENOMEM`` is present, one new node is allocated. Allocation failure
returns false and stores NULL in ``xa_alloc``.

A successful allocation clears the new node's parent, checks that its private
list is empty, stores it in ``xa_alloc``, and changes the cursor to
``XAS_RESTART``.

``xas_destroy()`` saves each parent pointer before it releases the current
node. It advances ``xa_alloc`` after each release, so an interrupted or
partially consumed chain cannot retain released nodes.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for allocation, accounted allocation, non-ENOMEM
  state, direct multi-node destruction, and stale-chain cleanup.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that both production entry points
  enter Rust and that the C fallback retains both complete algorithms.

Validation record
-----------------

Validation used base commit ``5f155a6f3505d7278e26be2916e17519102a9ad8``.
Every boot completed the focused suite and the complete upstream XArray suite:

.. list-table::
   :header-rows: 1

   * - Configuration
     - Runs
     - Focused result
     - XArray result
   * - Rust
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed
   * - C fallback
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed
   * - Rust with KASAN
     - SMP1, SMP4 rounds 1-3
     - 5 passed, 0 failed
     - 158825682 of 158825682 passed

The boot logs contained no KUnit failures, BUG reports, warnings, Oops reports,
panics, KASAN reports, call traces, or RCU stalls.

The Rust object defined ``rust_xarray_nomem`` and ``rust_xarray_destroy``. The
Rust-enabled ``lib/xarray.o`` defined both public entry points as direct tail
calls to those symbols. The C fallback object defined both complete C
algorithms and had no reference to either Rust symbol.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with no errors. It reported two existing
unresolved-reference warnings outside this unit.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, pending-chain parent, or private-list layout
  changes.

* The radix-tree node cache, LRU allocation, or reclamation primitive
  changes.

* XArray preallocations stop using the ``parent`` field as their chain link.

* ENOMEM encoding or cursor restart semantics change.

* ``__xas_nomem()`` starts using a different cleanup policy.

* A supported target needs different slab, RCU, or invariant primitives.
