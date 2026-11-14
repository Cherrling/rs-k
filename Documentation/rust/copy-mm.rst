.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Construction and Copying
==================================================

Status and scope
----------------

The ``CONFIG_RUST_COPY_MM`` option selects native Rust for the production
``mm_init()`` and ``copy_mm()`` paths in ``kernel/fork.c``. The configuration
depends on the native Rust address-space lifetime and teardown units. The
original C implementations remain available as fallbacks.

This unit covers:

* Construction of an allocated ``struct mm_struct``.
* Ordered construction of its non-fallible fields.
* Staged acquisition and cleanup of fallible address-space resources.
* The private or shared address-space decision for a new task.
* Complete ``dup_mm()`` orchestration, including VMA and binary-format
  handling.
* Publication of the resulting address-space pointers in the new task.

This unit excludes device drivers, the address-space slab allocator,
architecture context implementations, VMA duplication internals, and
subsystem-specific resource implementations. ``CONFIG_RUST_DUP_MMAP``
selects the separate native Rust VMA-tree unit described in
:doc:`dup-mmap`. With that option disabled, the parent unit enters the
original C VMA duplication implementation.

Entry points and ownership
--------------------------

The static C ``mm_init()`` adapter calls ``rust_mm_init()`` when the option is
enabled. The static C ``copy_mm()`` adapter calls ``rust_copy_mm()``. The
original decision and cleanup sequences are compiled out for these paths.
Neither change exposes a new public kernel ABI.

Rust owns these production decisions:

1. The complete order of non-fallible field construction.
2. The inherited memory-manager flags and default VMA flags.
3. The order and failure-depth tracking for page tables, architecture
   context, CPU ID, scheduler state, and RSS counters.
4. The transient scheduler state reset in the new task.
5. The ``CLONE_VM`` sharing decision and the private duplication decision.
6. The complete private address-space duplication sequence.
7. The binary-format module-reference decision and failure cleanup.
8. The point at which ``task->mm`` and ``task->active_mm`` become visible.

Rust also owns the kernel-thread case. If ``current->mm`` is null, Rust clears
the new task's address-space pointers and returns without acquiring a
reference.

Retained C boundary
-------------------

C helpers expose one field constructor, one indivisible existing lock
constructor, or one established subsystem operation. They do not select the
order of construction or cleanup.

The C boundary contains:

* Address-space slab allocation and release.
* Maple-tree and ``mmap_lock`` construction.
* Atomic count, list, counter, lock, CPU-mask, AIO, owner, PASID, executable
  file, MMU-notifier, TLB-flush, transparent-huge-page, uprobe, hugetlb, and
  futex field constructors.
* Legacy MM flag access and inherited default flag masking.
* Page-table and architecture address-space ID allocation and release.
* Architecture context creation and destruction.
* CPU-ID and scheduler accounting allocation and destruction.
* Per-CPU RSS counter initialization.
* LRU-generation initialization.
* Uprobe duplication serialization.
* VMA-tree duplication only when ``CONFIG_RUST_DUP_MMAP`` is disabled.
* Binary-format module-reference acquisition and release.
* Task scheduler-cache fork setup.

These boundaries use kernel-private inline helpers, architecture hooks,
subsystem constructors, or type-specific locking contracts that Rust cannot
currently express without duplicating their implementation. The Rust source
names every helper in the required order.

Ordering and invariants
-----------------------

The Rust non-fallible construction order matches the original C sequence.
The address-space memory tree is initialized before its external
``mmap_lock`` is attached. Counts, sequence counters, lists, scalar state,
locks, masks, subsystem pointers, and flags follow in their original order.

The inherited MM flags are selected after common field construction. A live
``current->mm`` supplies the legacy flag word and default VMA-mask bits. A
kernel-thread context receives the boot coredump filter and zero default VMA
flags.

Rust then acquires fallible resources in this order:

1. Page tables.
2. Architecture address-space ID.
3. Architecture task context.
4. Per-mm CPU ID.
5. Scheduler accounting.
6. Per-CPU RSS counters.

Each successful step advances an explicit cleanup stage. A failure releases
only resources acquired by earlier steps, then returns the address-space
storage. This preserves the original reverse-order cleanup.

Private address-space duplication copies the initialized source structure into
a new slab object before construction. Rust then starts uprobe serialization,
duplicates the VMA tree, records high-water marks, and acquires the optional
binary-format module reference.

A VMA duplication failure releases the new address space while uprobe
serialization remains active. Rust ends serialization after cleanup, matching
the original lock ordering. A binary-format failure clears the inherited
pointer and owner before the final user reference release. The binary-format
module reference is not released because it was never acquired.

No failure path publishes the duplicate address space in a task. Rust writes
``task->mm`` and ``task->active_mm`` only after the sharing reference or the
complete duplicate exists. Scheduler-cache fork setup runs after those
pointers are set.

Validation record
-----------------

The KUnit suite has five Rust cases:

* Real ``mm_alloc()`` construction.
* Six constructor failure injections, one for each fallible resource.
* Private address-space duplication.
* VMA-duplication and binary-format failure injection.
* Kernel-thread attachment with null source address space.

The C fallback runs the real ``mm_alloc()`` construction case.

The userspace selftest performs 32 private ``fork()`` operations and 32
``pthread_create()`` operations. It checks that private children do not modify
parent memory and that threads share memory. It reports through
``/dev/kmsg`` and powers off only after reporting the result.

All tests used four vCPUs and 1536 MiB of RAM in QEMU with KVM, with TCG as the
fallback accelerator. Each entry in the table represents three final runs.

.. list-table::
   :widths: 38 14 18 30
   :header-rows: 1

   * - Configuration
     - Runs
     - KUnit result
     - Userspace result
   * - Native Rust
     - 3
     - 5 of 5 pass
     - 3 of 3 pass
   * - C fallback
     - 3
     - 1 of 1 pass
     - 3 of 3 pass
   * - KASAN with native Rust
     - 3
     - 5 of 5 pass
     - 3 of 3 pass
   * - PREEMPT_RT with native Rust
     - 3
     - 5 of 5 pass
     - 3 of 3 pass
   * - lockdep with native Rust
     - 3
     - 5 of 5 pass
     - 3 of 3 pass

The final logs contain no failed KUnit case, BUG, warning, KASAN report,
lockdep complaint, RCU stall, oops, or unexpected panic.

Build and production checks
---------------------------

Final local checks:

* The production configuration builds ``bzImage`` with LLVM.
* Clippy and ``rustfmtcheck`` pass.
* ``git diff --check`` passes.
* Strict checkpatch reports zero errors and one expected
  ``FILE_PATH_CHANGES`` warning because the patch adds files.
* ``scripts/get_maintainer.pl`` identifies the Rust and memory-management
  maintainers for every added file.
* The Rust Sphinx build passes. It reports only the existing Graphviz and
  unrelated Rust reference warnings.
* The production image contains ``rust_copy_mm`` and ``rust_mm_init``.
* KUnit and failure-injection symbols are absent from the production image.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* Disassembly shows a direct call from ``copy_process()`` to
  ``rust_copy_mm()``.
* Disassembly shows ``mm_alloc()`` jumping directly to ``rust_mm_init()``.
* The Rust constructor disassembly calls the field constructors and fallible
  resource operations in the documented order.
* The production image boots to the expected missing-root-filesystem panic.
  The kernel is not tainted, and no earlier warning, BUG, KASAN report, RCU
  stall, or oops occurs.

Review conditions
-----------------

Reopen a retained C boundary if any of these conditions changes:

* A helper starts selecting initialization or cleanup policy.
* Field construction or lock initialization gains a sound Rust abstraction.
* Architecture context or address-space ID hooks gain a stable Rust
  interface.
* CPU-ID, scheduler accounting, or RSS initialization moves out of ``fork.c``.
* A retained VMA duplication primitive gains a Rust-safe interface with the
  same locking and failure contract.
* Binary-format module references or scheduler-cache setup gain safe Rust
  abstractions.
* Measurements show that the helper-call boundary causes an unacceptable
  regression in task-creation performance.
