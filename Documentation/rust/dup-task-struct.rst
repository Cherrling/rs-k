.. SPDX-License-Identifier: GPL-2.0

Native Rust Task Structure Duplication
======================================

Status and scope
----------------

The ``CONFIG_RUST_DUP_TASK_STRUCT`` option selects native Rust for the
production ``dup_task_struct()`` path in ``kernel/fork.c``. The original C
implementation remains available as a fallback.

This unit covers task-structure and kernel-stack allocation for a new task.
It includes the NUMA node decision, architecture task-state copy, stack
publication and accounting, IPI-mask allocation, shadow-call-stack
preparation, initial thread-stack setup, reference counts, CPU-affinity
inheritance, transient field resets, subsystem initialization, and staged
cleanup on failure.

It excludes the later ``copy_process()`` work that copies credentials, files,
namespaces, address spaces, signal state, and thread state.

Rust owns these decisions and transitions:

1. Resolving ``NUMA_NO_NODE`` to a task-fork node.
2. Ending task creation when task memory cannot be allocated.
3. Advancing through task, stack, IPI-mask, and shadow-stack allocation
   stages.
4. Selecting the correct cleanup depth after each fallible operation.
5. Publishing the stack reference and accounting entry.
6. Initializing thread stack, reference counts, affinity state, and common
   transient fields.
7. Invoking configuration-dependent field resets and subsystem hooks in the
   original order.

Entry point and ownership
-------------------------

Rust directly defines the production ``dup_task_struct()`` entry. The native
build compiles the original C implementation out of ``kernel/fork.c``. C
fallback builds retain the complete static C function.

Rust calls narrow helpers for operations that need private allocator state,
architecture hooks, generated configuration fields, or established kernel
primitives:

* Fork-node selection, task allocation, and thread-stack allocation.
* Architecture task-state copy.
* Stack reference-count and accounting setup.
* IPI-mask allocation and shadow-call-stack preparation.
* Thread-stack setup, transient flag clearing, stack-end marking, and random
  stack-canary generation.
* CPU-affinity pointer inheritance and user CPU-mask duplication.
* Task and rcu reference-count initialization.
* KCOV, KMSAN, and kmap-local setup.
* Configuration-dependent field initialization.
* IPI-mask, stack-accounting, thread-stack, and task allocation release.

These helpers perform one primitive, one configuration boundary, or one
subsystem operation. They do not decide the allocation sequence or cleanup
depth.

Ordering and cleanup
--------------------

The Rust sequence preserves the original order. Rust uses a cleanup stage to
track the last successfully acquired resource:

* Architecture-copy or stack-allocation failure releases only task memory.
* IPI-mask failure releases the stack accounting entry, stack, and task.
* Shadow-stack failure also releases the IPI mask.

No operation after shadow-stack preparation can fail on this path. The helper
for user CPU-mask duplication keeps the original behavior and does not make
creation fail when its internal allocation fails.

Validation record
-----------------

The native configuration passed both focused cases in three consecutive SMP4
boots and one SMP1 boot. The fallback and C-only configurations each passed
the focused case at SMP4 and SMP1. KASAN, lockdep, and PREEMPT_RT each passed
both focused cases at both CPU counts.

.. list-table::
   :widths: 40 14 14 32
   :header-rows: 1

   * - Configuration
     - SMP4
     - SMP1
     - Focused cases
   * - Native Rust
     - 3
     - 1
     - 2 of 2
   * - C fallback
     - 1
     - 1
     - 1 of 1
   * - C-only
     - 1
     - 1
     - 1 of 1
   * - KASAN
     - 1
     - 1
     - 2 of 2
   * - Lockdep
     - 1
     - 1
     - 2 of 2
   * - PREEMPT_RT
     - 1
     - 1
     - 2 of 2

The real task-creation case calls ``kthread_run()`` so the child passes
through the production ``copy_process()`` and ``dup_task_struct()`` path.
The child checks task memory, the kernel stack, the stack-end magic, task
usage, ``rcu_users``, the stack reference, and the transient splice and
page-frag references. It does not check ``worker_private`` because the kthread
layer assigns that field after ``dup_task_struct()`` returns.

The Rust configuration also checks transition counters for task allocation,
stack allocation, successful completion, and the absence of failures. It
injects three failures after separate resources are acquired. These cases
exercise task-only cleanup, stack cleanup, and IPI-mask cleanup.

Build and production checks
---------------------------

Final local validation:

* The production configuration builds ``bzImage`` and modules with LLVM and
  Clippy.
* ``LLVM=1 rustfmtcheck`` and ``git diff --check`` pass.
* Native ``rust/kernel.o`` contains one strong ``dup_task_struct()``
  definition. Native ``kernel/fork.o`` contains only an undefined reference.
  The linked image contains one strong definition.
* The relocations in ``copy_process()`` contain a direct call to
  ``dup_task_struct``.
* ``dup_task_struct()`` has no export-table entry. ``Module.symvers``
  contains no old wrapper or task duplication test symbol. ``nm -u vmlinux``
  reports no undefined symbol.
* QEMU boots the four-vCPU production image to the expected
  missing-root-filesystem panic. The kernel is not tainted. No WARN, BUG,
  KASAN report, lockdep complaint, Oops, RCU stall, or unexpected panic occurs.
* Rustdoc passes. The focused Rust Sphinx build reports only the existing
  unknown ``kernel-docs`` document and undefined ``getting_llvm`` label
  warnings.
* Strict checkpatch reports zero errors and zero warnings.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* Task or thread-stack allocation gains a safe Rust abstraction.
* Architecture task copying gains a Rust interface with the same state-copy
  contract.
* Stack accounting, IPI-mask, or shadow-stack setup moves out of ``fork.c``.
* Configuration-dependent task fields gain a stable Rust abstraction.
* A later task-creation stage starts to depend on the internal cleanup stages.
