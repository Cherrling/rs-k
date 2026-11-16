.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree leaf-gap calculation
===========================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_LEAF_GAP`` option selects the native Rust
production implementation of the largest allocation gap in a Maple Tree leaf.

Rust owns these operations:

* Scan dense nodes for the longest run of NULL slots.
* Measure a left-edge gap from the node minimum to the first pivot.
* Measure internal gaps between adjacent pivots.
* Measure a right-edge gap from the final pivot to ``ULONG_MAX``.
* Preserve the rule that two adjacent slots cannot both be gaps.
* Return the largest measured allocation gap.

The ordinary allocation-gap update path through ``mas_max_gap()`` uses the Rust
implementation when this option is enabled. Replacement-node gap calculation
in the native spanning, split, rebalance, and root stores uses the same Rust
function. Those stores do not call the C leaf-gap wrapper when the option is
enabled.

Retained C boundary
-------------------

The C boundary contains these operations:

* Identify dense and pivot-bearing node layouts.
* Return the slot count for a concrete node type.
* Return slot and pivot array addresses.
* Determine the data end from layout metadata or encoded pivots.
* Maintain the RCU and write-lock rules that protect a live leaf.

These operations remain in C because Maple Tree stores layout tags and metadata
at type-specific offsets. Rust owns the bounded slot and pivot search policy.

Leaf-gap algorithm
------------------

For a dense node, Rust scans every slot and tracks the longest consecutive run
of NULL entries. The node uses the compact dense layout and has no explicit
pivots.

For a pivot-bearing leaf, a NULL first slot creates a gap from the node minimum
through the first pivot. The scan starts at slot two because two adjacent gaps
cannot occur. Rust then visits the remaining data slots and calculates a gap
from the previous pivot to the current pivot.

The rightmost leaf can represent a gap that extends to the maximum unsigned
long value. Rust checks the slot after the last internal pivot and reports that
range when it is larger than every internal gap.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* Every slot and pivot read remains within the concrete leaf layout.
* The dense branch counts complete NULL runs, including a trailing run.
* The range branch includes implied gaps at the left and right boundaries.
* The skip after an internal gap preserves the no-adjacent-gaps rule.
* Unsigned pivot subtraction uses Maple Tree wrap semantics.
* The C implementation remains available when the option is disabled.

Testing
-------

Direct KUnit tests cover dense runs, an entirely empty dense leaf, a left-edge
gap, an internal gap, and the right-edge ``ULONG_MAX`` case.

The existing store KUnit suites cover gap reuse in allocation-range trees. The
complete Maple Tree self-test exercises allocation, deletion, large trees, and
gap propagation through the production path.

The unit validation gate includes:

* Native Rust and C-fallback focused KUnit runs on one and four CPUs.
* The complete Maple Tree self-test in every focused run.
* Native Rust KASAN runs on one and four CPUs.
* Native Rust lockdep runs on one and four CPUs.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.
* A C-only build with ``CONFIG_RUST=n``.
* Object inspection for the production Rust and C fallback paths.

Validation results
------------------

Validation on 2026-11-17 used parent commit
``062da81205f5ae143ea79fc28bcf0f8f528674a9``. The native Rust runs passed all
56 focused KUnit cases on one and four CPUs. The leaf-gap fallback runs passed
all 51 configured cases on one and four CPUs.

The native Rust, leaf-gap fallback, and KASAN runs passed 5,357,658 complete
Maple Tree checks on one CPU and 5,357,709 checks on four CPUs. The lockdep
runs passed 3,809,321 checks on both CPU counts.

The KASAN logs contained no KASAN report. The lockdep logs contained no
lockdep warning or call trace. Lockdep used ``CONFIG_DEBUG_MAPLE_TREE=n``.

The KASAN image required
``KCFLAGS=-Wno-error=frame-larger-than``. This option downgraded two existing
frame-size warnings in ``fs/coredump.c`` and
``lib/tests/maple_tree_store_kunit.c``. Other warnings remained fatal.

Rustfmtcheck, Clippy, Rustdoc, strict checkpatch, and ``git diff --check``
passed. ``get_maintainer.pl`` matched every new file to the Maple Tree entry
in ``MAINTAINERS``. Checkpatch used ``--ignore FILE_PATH_CHANGES`` because its
new-file warning does not inspect the updated maintainer entry.

A forced Rust Sphinx rebuild reported two existing cross-reference warnings
in ``Documentation/rust/index.rst`` and
``Documentation/rust/quick-start.rst``. It reported no warning for this
document.

A full ``CONFIG_RUST=n`` image built successfully. Its
``maple_tree_store`` KUnit suite passed all 19 cases on four CPUs. The C object
contains no Rust leaf-gap reference.

The complete C-store fallback image also built successfully. Its 51 configured
KUnit cases and 5,357,709 complete Maple Tree checks passed on four CPUs.

In the native configuration, ``lib/maple_tree.o`` references
``rust_maple_tree_leaf_gap``. The Rust object defines that symbol and references
only the retained C ``data_end`` and slot-count primitives. The leaf-gap
fallback object defines the old C wrapper, and no production object references
the native leaf-gap symbol.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded node type, slot, pivot, or metadata layout changes.
* A dense node gains explicit pivots.
* Slot count or data-end discovery changes.
* The no-adjacent-gaps invariant changes.
* A C helper starts to implement gap scanning or maximum-gap selection.
