.. SPDX-License-Identifier: GPL-2.0

Native Rust Maple Tree node copying
===================================

Status and scope
----------------

The ``CONFIG_RUST_MAPLE_TREE_NODE_COPY`` option selects native Rust
implementations of the production Maple Tree partial-node copy and node
finalisation operations used by the native write path.

Rust owns these operations:

* Check and preserve the requested source and destination copy ranges.
* Copy bounded slot arrays into a replacement node layout.
* Copy bounded pivot arrays and calculate the destination maximum.
* Copy allocation-range gap arrays into the replacement layout.
* Select when copied children need encoded parent information.
* Clear unused slots, gaps, and pivots after a copy.
* Select the largest gap for parent gap metadata.
* Set leaf and allocation-range end and gap metadata.

The native spanning, split, rebalance, and root stores use the Rust copy
operations. They do not call the C implementations of ``node_copy()`` or
``node_finalise()`` when this option is enabled.

Retained C boundary
-------------------

The C boundary contains these operations:

* Return slot, pivot, and gap array addresses for encoded node layouts.
* Encode a destination node as the parent of copied children.
* Store encoded child parent fields in one bounded slot range.
* Store metadata fields in the layout-specific metadata area.
* Allocate replacement nodes and publish the completed subtree.

These operations exist because Maple Tree stores layout tags, child parent
slots, and metadata in encoded pointer bits and type-specific unions. Rust owns
the copy, clearing, maximum calculation, and metadata selection policy.

Node copy algorithm
-------------------

Rust copies the requested number of slots from the source offset to the
destination offset. When the destination is an internal node and the source is
the private copy node, Rust requests encoded parent initialization for every
copied child slot.

If the destination layout contains allocation-range gaps, Rust copies the
corresponding bounded gap range. Rust then determines the maximum value of the
new destination.

The final source pivot becomes the destination boundary only when the source
range does not end at the source node maximum. When the destination has room
for another pivot, Rust writes the destination maximum there. Rust copies the
remaining bounded pivot prefix and returns the destination maximum.

Node finalisation
-----------------

Rust clears every unused slot and gap after the final data entry. It clears the
unused pivots while preserving the final layout boundary.

For an internal allocation-range node, Rust scans the finalized gap values and
records the offset of the largest gap. It then writes the data end and selected
gap offset into the node metadata.

State invariants
----------------

The native implementation preserves these Maple Tree invariants:

* Every copied slot, pivot, and gap remains inside its concrete layout.
* A copied child in an internal node contains the encoded destination parent.
* The destination maximum describes the final copied source boundary.
* Unused storage does not retain stale values from a reused node.
* The final pivot remains outside the clearing range.
* Internal allocation-range metadata identifies the largest finalized gap.
* Replacement nodes remain private until the final RCU publication.
* The C implementation remains available when the option is disabled.

Testing
-------

The native copy operations are exercised by node, spanning, split, rebalance,
root, gap reuse, locked, and RCU store tests. The complete Maple Tree self-test
provides broad coverage of different node types, tree heights, gaps, and
parent layouts.

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

The unit starts at commit ``19eed138379c``, which implements native rebalance
stores in Rust. All native Rust, C-fallback, KASAN, and lockdep configurations
passed 19 of 19 focused KUnit tests on one and four CPUs.

Native Rust, C fallback, and KASAN passed 5,357,658 complete Maple Tree
self-tests on one CPU and 5,357,709 self-tests on four CPUs. Lockdep passed
3,809,321 self-tests on both CPU configurations. KASAN and lockdep emitted no
report.

The KASAN build used
``KCFLAGS=-Wno-error=frame-larger-than`` for existing Clang frame-size warnings
in ``fs/coredump.c``, ``net/ethtool/cmis_fw_update.c``,
``drivers/block/loop.c``, and the existing spanning-store test. The changed
Rust code emitted no warning.

The Rust source passed Clippy, Rustdoc, and ``rustfmtcheck``.
``git diff --check`` and strict checkpatch passed. The focused Rust Sphinx
build generated this document. Sphinx reported only the existing warnings in
``Documentation/rust/index.rst`` and ``Documentation/rust/quick-start.rst``.

The production Rust object has no reference to the C node copy or finalisation
wrappers. It references only the narrow encoded child-parent helper. A separate
configuration with ``CONFIG_RUST_MAPLE_TREE_NODE_COPY=n`` and all other native
store options enabled compiled the Rust dispatcher and referenced both C copy
operations.

The ``CONFIG_RUST=n`` build compiled the Maple Tree object and the store KUnit
object. The object contains the complete C node copy path and has no Rust
Maple Tree symbol references.

Reopen conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Encoded node type, slot, pivot, gap, or parent layout changes.
* Maple node metadata layout changes.
* Gap-array addressing no longer has bounded behavior.
* Replacement-node allocation or RCU publication rules change.
* The copied source and destination ranges no longer have disjoint storage.
* A C helper starts to implement bounded copy, clearing, largest-gap selection,
  or destination maximum policy.
