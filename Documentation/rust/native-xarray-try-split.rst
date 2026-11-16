.. SPDX-License-Identifier: GPL-2.0

Native Rust XArray one-shot multi-index split
=============================================

Status and scope
----------------

The ``CONFIG_RUST_XARRAY_TRY_SPLIT`` option selects experimental native Rust
implementations for the one-shot multi-index split policy:

* ``xas_try_split_min_order()`` selects the next order that avoids an
  unnecessary split step.

* ``xas_try_split()`` tries to split one level while the caller holds the
  XArray lock.

Rust owns minimum-order selection, one-level split validation, opportunistic
``GFP_NOWAIT`` allocation, prepared-node consumption, publication order, mark
propagation, and value accounting. The C fallback retains the complete
original policy.

This unit starts after the two-phase ``xas_split_alloc()`` and ``xas_split()``
implementation. It uses their shared split-node and pending-chain boundaries.

Entry points and ownership
--------------------------

The public ``xas_try_split_min_order()`` and ``xas_try_split()`` interfaces
and their ``EXPORT_SYMBOL_GPL`` export classes do not change.

With this option enabled, both entry points in ``lib/xarray.c`` forward
directly to exported Rust functions. The production implementation is in
``rust/kernel/xarray_split.rs``.

Retained C boundaries
---------------------

The XArray node, state, sibling, tagged-entry, and mark layouts remain C.

The node allocator and pending-chain primitives remain the allocation
boundaries introduced by ``RUST_XARRAY_NOMEM``. Slab allocation with
``GFP_NOWAIT`` and ``__GFP_ACCOUNT`` remains in C.

Relaxed parent and slot initialization remain C RCU operations. RCU slot
publication and XArray node update callbacks also remain C boundaries.

Mark bitmap access remains C. Rust owns which parent and child slots receive
each propagated mark.

Error encoding remains C. Rust owns the policy decision that rejects a
cascade split with ``EINVAL`` or records ``ENOMEM`` after allocation failure.

Important invariants
--------------------

``xas_try_split_min_order()`` returns zero for order zero. For other orders
divisible by the chunk shift, it returns the preceding order. Otherwise it
returns the start of the current chunk-aligned range.

A top cursor is a no-op.

A vertical one-shot split is supported only when the source split targets one
new child and the cursor has the exact sibling count required by the preceding
order. A horizontal split can update a complete sibling range without a new
node.

If the cursor has a prepared node, Rust consumes that node. If no node is
prepared, Rust makes one opportunistic ``GFP_NOWAIT`` allocation.

A request that needs sibling entries in a new child or more than one new child
releases the pending chain and records ``EINVAL``.

An opportunistic allocation failure releases the pending chain and records
``ENOMEM``.

The parent value count is adjusted once after all covered slots are updated.
The node update callback runs for a new child and for the final parent.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* Focused KUnit coverage for minimum-order selection, top cursors, horizontal
  splits, vertical splits with and without a prepared node, and cascade
  rejection.

* The same four runs with this Rust unit disabled.

* The same four runs with Rust enabled and KASAN enabled.

* The complete upstream XArray regression suite, including exhaustive
  one-shot split coverage, in every run.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol and disassembly checks proving that both production entry points
  enter Rust and that the C fallback retains both complete algorithms.

Validation results
------------------

This unit was validated against ``b473e90fbf01`` with the unit patch applied.

The Rust-enabled build passed one single-CPU run and three four-CPU runs.
Each run passed the focused suite with six of six tests and the complete
XArray suite with 158825682 of 158825682 tests.

The C fallback build passed the same four runs. The Rust-enabled KASAN build
passed the same four runs.

Clippy, Rustfmt, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with pre-existing cross-reference
warnings in unrelated documents.

The Rust object exports both production symbols. The enabled C object imports
both symbols, and both C entry points contain a direct jump to Rust. The
fallback C object retains both algorithms and has no Rust split reference.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The XArray node, state, sibling, tagged-entry, or mark layout changes.

* Split preparation stops using the parent field as its pending-chain link.

* RCU initialization, slot publication, or node update callback rules change.

* Mark propagation or parent value accounting changes.

* The set of supported one-shot split shapes changes.

* ``GFP_NOWAIT`` allocation or ``__GFP_ACCOUNT`` propagation changes.

* The two-phase split implementation stops sharing the split-node helpers.
