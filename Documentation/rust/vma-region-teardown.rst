.. SPDX-License-Identifier: GPL-2.0

Native Rust VMA Region Unmapping and Release
============================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_REGION_TEARDOWN`` option selects native Rust for the
shared production sequences in ``unmap_region()`` and ``remove_vma()``. The
original C implementations remain available as a fallback.

These functions join runtime unmapping, partial duplication cleanup, mapping
error cleanup, and final address-space teardown. Rust owns these production
decisions:

1. TLB gather initialization for the source address space.
2. The high-water update before VMA page-table removal.
3. VMA page-table removal.
4. Reset of the maple-tree walk before page-table free.
5. Page-table free and TLB finish order.
6. Whether a VMA has a close callback.
7. File-reference and memory-policy release decisions.
8. The final close, file, policy, and VMA free order.

The unit does not migrate the TLB implementation, page-table algorithms,
maple-tree operations, callbacks, allocator, memory-policy implementation, or
architecture mapping code.

Retained C boundary
-------------------

The C ``unmap_region()`` adapter allocates the private ``struct mmu_gather``
on its stack and calls Rust. The C ``remove_vma()`` adapter passes the detached
VMA to Rust. The adapters do not select operations or ordering.

The helpers expose one existing operation or one indivisible callback
transition. The boundary contains:

* The sleep check.
* Close-callback presence and invocation.
* Replacement of callbacks with the dummy VMA operations table.
* VMA file and memory-policy field access.
* File and memory-policy release.
* VMA allocation release.
* TLB gather and finish.
* High-water accounting.
* VMA unmap and page-table free.
* Maple-tree walk reset.

The helpers remain in C for one of three reasons:

* TLB, page-table, high-water, and maple-tree operations keep existing
  architecture, locking, and accounting contracts.
* The close callback and dummy-operations replacement is one callback
  transition. The VMA must expose the dummy table as soon as the callback
  returns.
* File, memory-policy, and VMA release use object ownership and allocator
  paths owned by existing C subsystems.

The access helpers cross the C object boundary. They do not select teardown
policy.

Rust directly selects the conditional close, file, and memory-policy release
steps. No helper selects multi-step teardown policy.

Ordering and invariants
-----------------------

Region page-table teardown preserves the original order:

1. Read the address space from the first VMA.
2. Initialize the TLB gather.
3. Update the resident-set high-water mark.
4. Unmap the described VMA range.
5. Reset the maple-tree walk.
6. Free page tables in the described range.
7. Finish the TLB gather.

VMA release preserves the original order:

1. Permit sleeping.
2. Invoke the close callback when present.
3. Install the dummy operations table after the callback.
4. Release the file reference when present.
5. Release the memory policy.
6. Free the VMA object.

The close callback runs before the file reference is released. The callbacks
become unreachable before any other cleanup. The VMA remains live until its
field cleanup has completed.

Testing
-------

The existing VMA-unmap userspace test exercises runtime region teardown. The
exit-mmap userspace test exercises file-backed, anonymous, shared, private,
and guarded VMA release. The duplicate-mmap userspace and KUnit failure paths
exercise partial-region cleanup. The exit-mmap KUnit suite exercises populated
address-space VMA removal.

The host VMA suite checks the shared C fallback algorithms. KASAN, lockdep, and
PREEMPT_RT configurations exercise the Rust path with memory, lock-order, and
real-time instrumentation.

Validation record
-----------------

The final validation uses five x86_64 configurations: native Rust production,
C fallback, KASAN with DEBUG_VM, lockdep, and PREEMPT_RT. The userspace total
counts four suites, each run three times. The KUnit total counts three suites,
each run three times.

.. list-table::
   :header-rows: 1

   * - Configuration
     - Region path
     - Userspace
     - KUnit
   * - Native Rust production
     - Rust
     - 12 of 12
     - Not enabled
   * - C fallback
     - C
     - 12 of 12
     - Not enabled
   * - KASAN and DEBUG_VM
     - Rust
     - 12 of 12
     - 9 of 9
   * - lockdep
     - Rust
     - 12 of 12
     - 9 of 9
   * - PREEMPT_RT
     - Rust
     - 12 of 12
     - 9 of 9

The host VMA suite passes all 26 tests. The production image reaches the
expected missing-root panic. After Clippy, the production image was rebuilt
with the normal Rust compiler and the 12-run userspace matrix was repeated.

Each Rust configuration image contains both Rust entry points and 14 C helper
symbols. The C fallback contains neither. All five images have zero undefined
symbols. All validation log sets report no `BUG`, `WARNING`, `KASAN`, `Oops`,
or `RCU Stall` diagnostic.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* TLB gather or finish semantics change.
* High-water or page-table accounting ordering changes.
* Close callbacks no longer require immediate replacement with dummy
  operations.
* File or memory-policy ownership changes.
* VMA allocator release gains a failure return or new ordering requirement.
* A helper starts selecting multi-step teardown policy.
