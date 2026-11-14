.. SPDX-License-Identifier: GPL-2.0

Native Rust Merge-or-Split Orchestration for VMA Modifications
==============================================================

Status and scope
----------------

The ``CONFIG_RUST_VMA_MODIFY`` option selects native Rust for the production
``vma_modify()`` path. The original C implementation remains available as a
fallback.

The path handles changes to VMA flags, policy, userfaultfd context, and
anonymous names over part or all of an existing VMA. It first tries to merge
the changed range with an adjacent VMA. If that merge fails for a reason other
than a requested OOM backup, it splits the leading and trailing ranges that
the caller did not select.

Rust owns these decisions:

1. Read the original middle VMA and requested range.
2. Attempt the existing-range merge.
3. Return a successful merged VMA.
4. Propagate a requested OOM backup failure.
5. Enforce the give-up-on-OOM split invariant.
6. Split a leading range when the VMA starts before the request.
7. Split a trailing range when the VMA ends after the request.
8. Return the original VMA after both splits.

Rust performs the leading split before the trailing split. It checks the VMA
bounds again after the first split. This order matches the original C path.

Retained C boundary
-------------------

The C helpers expose one field access or one operation. Rust selects the
control flow.

The C boundary contains:

* Merge-state and VMA field access.
* Existing-range merge entry.
* Linux error-pointer construction.
* The OOM backup invariant warning.
* The ``split_vma()`` adapter.

The caller continues to hold the mmap write lock. C owns VMA allocation,
policy and anonymous-VMA cloning, locking, structural completion, maple-tree
updates, and debug invariants reached by the merge and split operations. The
split orchestration itself is Rust-owned through ``CONFIG_RUST_VMA_SPLIT``.

Entry points and ownership
--------------------------

The exported Rust entry is ``rust_vma_modify()``. The C ``vma_modify()``
function becomes an adapter when the option is enabled. The original C
implementation is compiled when the option is disabled.

The four public modification constructors remain in C. They initialize the
merge state and retain the API boundary:

* ``vma_modify_flags()``
* ``vma_modify_name()``
* ``vma_modify_policy()``
* ``vma_modify_flags_uffd()``

``vma_modify_flags()`` also propagates sticky flags after a successful merge.
That post-merge API behavior remains in C.

Ordering and invariants
-----------------------

Rust follows this order:

1. Save the middle VMA, start, and end values through C accessors.
2. Call the existing-range merge entry.
3. Return immediately when a merged VMA exists.
4. Convert a nomem merge state into an ``ERR_PTR(-ENOMEM)`` result.
5. Warn when a give-up-on-OOM request cannot preserve the full VMA range.
6. Split the leading range when required.
7. Return the split error as an error pointer.
8. Split the trailing range when required.
9. Return the split error as an error pointer.
10. Return the modified VMA.

The merge entry can change merge-state fields. Rust reads the requested range
before the merge, as the C implementation does. The split checks use the
current VMA bounds after any failed merge.

Testing
-------

The VMA merge userspace suite covers these paths:

* Successful left, right, and two-sided existing-range merges.
* A leading split followed by a right-side merge.
* A trailing split followed by a left-side merge.
* A middle-range split with both leading and trailing ranges retained.
* Anonymous and file-backed VMA modifications.
* Incompatible properties that require a split.

The VMA unmap suite also runs because mapping replacement, unmapping, and heap
changes exercise the surrounding VMA lifecycle.

Verification results
--------------------

Each configuration ran the merge and unmap suites three times.

.. list-table::
   :header-rows: 1

   * - Configuration
     - Modify path
     - Merge result
     - VMA-unmap result
     - KUnit result
   * - Native Rust production
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - C fallback
     - C
     - 3 of 3
     - 3 of 3
     - Not built
   * - Rust modify with C merge stack
     - Rust
     - 3 of 3
     - 3 of 3
     - Not built
   * - KASAN and DEBUG_VM
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
   * - lockdep
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3
   * - PREEMPT_RT
     - Rust
     - 3 of 3
     - 3 of 3
     - 3 of 3

All QEMU runs used four vCPUs, 1536 MiB of RAM, and KVM. Every KUnit round
reported ``ok 1 vma_unmap``.

The KASAN configuration also enabled ``CONFIG_DEBUG_VM``,
``CONFIG_DEBUG_VM_MAPLE_TREE``, and ``CONFIG_DEBUG_MAPLE_TREE``. The lockdep
configuration also enabled ``CONFIG_DEBUG_LOCK_ALLOC`` and
``CONFIG_DEBUG_ATOMIC_SLEEP``.

The mixed configuration disabled the Rust compatibility, commit,
existing-range, and new-range merge implementations. The helper in this mode
enters the C existing-range merge stack from the Rust modification path.

The log scan found no BUG, kernel warning, KASAN report, lockdep complaint,
RCU stall, oops, failed test, or unexpected kernel panic. The expected
missing-root-filesystem panic from the separate production boot was the only
kernel panic.

Build and ownership checks:

* All six configurations used ``CONFIG_WERROR=y`` and completed with LLVM.
* Production Clippy, ``rustfmtcheck``, and ``git diff --check`` passed.
* The production image contains ``rust_vma_modify()`` and every modification
  helper.
* The C fallback image omits the Rust modification entry and helpers.
* The mixed image contains the Rust modification entry but omits all Rust VMA
  merge entries.
* The C fallback disassembly shows ``vma_modify()`` calling
  ``rust_vma_merge_existing()`` and both split branches in C.
* Production disassembly shows ``vma_modify_flags()`` calling
  ``rust_vma_modify()``. The Rust disassembly checks the merge, error, warning,
  leading split, and trailing split operations in the documented order.
* The mixed disassembly shows ``rust_vma_modify()`` calling
  ``rust_helper_vma_modify_merge()``. The helper body contains the C
  existing-range orchestration.
* ``nm -u vmlinux`` reports no undefined symbols in any configuration.
* The VMA merge test program compiled with GCC and Clang using ``-Werror``.
* The production image boots to the expected missing-root-filesystem panic.
  The kernel is not tainted. The post-Clippy merge and unmap suites also pass
  in all three rounds.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* Rust gains sound mmap-lock and VMA field access.
* The existing-range merge entry becomes a Rust-native internal function.
* Linux error-pointer construction gains a safe Rust abstraction.
* The give-up-on-OOM contract changes.
* The public modification constructors move to Rust.
* A helper starts selecting merge or split control flow instead of performing
  one access or operation.
