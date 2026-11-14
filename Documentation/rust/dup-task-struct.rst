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

Retained C boundary
-------------------

The C adapter remains in ``kernel/fork.c``. When the option is enabled,
``dup_task_struct()`` delegates to ``rust_dup_task_struct()`` and the original
C implementation is compiled out.

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

The Rust and C configurations each run the real task-creation case three
times. The test calls ``kthread_run()`` so the child passes through the
production ``copy_process()`` and ``dup_task_struct()`` path.

The child checks that task memory and the kernel stack exist, the stack-end
magic is present, task usage is non-zero, ``rcu_users`` is two, the stack
reference is live, and transient splice and page-frag references are clear.
The test does not check ``worker_private`` because the kthread layer assigns
that field after ``dup_task_struct()`` returns.

The Rust configuration also checks transition counters for task allocation,
stack allocation, successful completion, and the absence of failures.

The Rust configuration also injects three failures after separate resources
are acquired. These cases exercise task-only cleanup, stack cleanup, and
IPI-mask cleanup. Each case verifies that the production state machine returns
NULL and records exactly one failure.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Native Rust task duplication
     - 3
     - 2 of 2
     - Pass
   * - C task duplication fallback
     - 3
     - 1 of 1
     - Pass
   * - Native Rust task duplication with KASAN
     - 3
     - 2 of 2
     - Pass
   * - Native Rust task duplication with PREEMPT_RT
     - 3
     - 2 of 2
     - Pass
   * - Native Rust task duplication with lockdep
     - 3
     - 2 of 2
     - Pass

Build and production checks
---------------------------

Final local validation:

* The production configuration builds ``bzImage`` with LLVM.
* ``LLVM=1 rustfmtcheck`` and both Clippy and ``git diff --check`` pass.
* The production image contains ``rust_dup_task_struct`` and no task
  duplication test symbols.
* ``llvm-nm -u vmlinux`` reports no undefined symbols.
* ``copy_process()`` directly calls ``rust_dup_task_struct()``.
* QEMU boots the production image to the expected missing-root-filesystem
  panic. The kernel is not tainted, and no earlier warning, BUG, KASAN report,
  RCU stall, or oops occurs.
* The Rust Sphinx build passes. It reports only the existing Graphviz and
  unrelated Rust reference warnings.
* Strict checkpatch reports zero errors and one expected
  ``FILE_PATH_CHANGES`` warning because the change adds files and updates
  ``MAINTAINERS``.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* Task or thread-stack allocation gains a safe Rust abstraction.
* Architecture task copying gains a Rust interface with the same state-copy
  contract.
* Stack accounting, IPI-mask, or shadow-stack setup moves out of ``fork.c``.
* Configuration-dependent task fields gain a stable Rust abstraction.
* A later task-creation stage starts to depend on the internal cleanup stages.
