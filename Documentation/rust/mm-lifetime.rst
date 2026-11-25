.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Lifetime
==================================

Status and scope
----------------

The ``CONFIG_RUST_MM_LIFETIME`` option selects direct Rust definitions of the
production ``mm_users`` lifetime entries. The configuration default is enabled
when Rust support and an MMU are available. The complete C implementation is
selected only when the Rust option is disabled.

This unit covers three public entry points:

* ``get_task_mm()``
* ``mmput()``
* ``mmput_async()``

The migration excludes device drivers. Final address-space teardown remains in
C when ``CONFIG_RUST_MM_TEARDOWN`` is disabled. When that option is enabled,
Rust owns the final teardown orchestration described in
``Documentation/rust/mm-teardown.rst``. Rust exports the same three public GPL
symbols, so this unit does not change the public C ABI.

The Rust code owns these production decisions:

* Rejecting a kernel thread before task-mm acquisition.
* Selecting whether a task has an address space to acquire.
* Incrementing ``mm_users`` before the task lock is released.
* Decrementing ``mm_users`` for both synchronous and asynchronous release.
* Selecting synchronous finalization when the user count reaches zero.
* Selecting asynchronous work queueing when the user count reaches zero.

Entry points and ownership
--------------------------

``rust/kernel/mm_core.rs`` directly defines ``get_task_mm()``, ``mmput()``,
and ``mmput_async()``. These functions use the production names and carry the
original GPL export class. The C definitions and export records in
``kernel/fork.c`` compile only when ``CONFIG_RUST_MM_LIFETIME`` is disabled.
Rust owns the complete decision and helper order after entry.

The Rust implementation uses these narrow C helpers:

* ``rust_helper_mm_users_ptr()`` returns the address of
  ``struct mm_struct.mm_users``.
* ``rust_helper_mm_task_is_kernel_thread()`` reads ``PF_KTHREAD``.
* ``rust_helper_mm_task_lock()`` and ``rust_helper_mm_task_unlock()`` acquire
  and release the existing task lock.
* ``rust_helper_mm_task_mm()`` reads ``task->mm`` while the task lock is held.
* ``rust_helper_mm_put_might_sleep()`` preserves the sleep-context diagnostic
  before synchronous release work.
* ``rust_helper_mm_put_final()`` calls ``__mmput()`` when native final
  teardown is disabled.
* ``rust_helper_mm_put_async_schedule()`` initializes and schedules the
  existing asynchronous work item.

Ordering and invariants
-----------------------

For ``get_task_mm()``, Rust checks ``PF_KTHREAD`` before taking the task lock.
The flag is fixed for the life of a task. Rust then takes ``task->alloc_lock``,
reads ``task->mm``, increments ``mm_users`` while the lock remains held, and
unlocks the task. The lock prevents ``task->mm`` from being replaced while the
reference is acquired.

For ``mmput()``, Rust first runs the sleep-context diagnostic and then performs
one atomic decrement. A result other than one returns without finalization. A
result equal to one transfers the final user reference to ``__mmput()``.

For ``mmput_async()``, Rust performs the same atomic decrement. A result equal
to one transfers the final user reference to ``INIT_WORK()`` and
``schedule_work()``. The work callback calls ``__mmput()`` later.

Rust uses ``AtomicI32`` operations with sequential consistency. On x86_64,
these operations compile to locked increment and decrement instructions. This
ordering is at least as strong as the existing fully ordered
``atomic_inc()`` and ``atomic_dec_and_test()`` operations.

The address of the atomic field is not open-coded in Rust. C checks that
``mm_users`` has the size of ``atomic_t`` and satisfies its alignment. C also
checks the field offset. Rust independently checks that ``AtomicI32`` has the
same size and alignment as ``bindings::atomic_t``.

Retained C boundary
-------------------

The following operations remain in C because they use established kernel or
architecture primitives:

* Task locking, task flag access, and task-mm field access.
* The address helper and compile-time field layout checks.
* The final address-space teardown in ``__mmput()`` when
  ``CONFIG_RUST_MM_TEARDOWN`` is disabled.
* Work item initialization, work queueing, and callback execution.
* The warning and fatal invariants inside ``__mmput()``.

The inline ``mmget()`` and ``mmget_not_zero()`` operations used by other
callers also remain in C. This unit migrates the production acquisition path
in ``get_task_mm()``. It does not claim that every ``mm_users`` increment in
the kernel is native Rust.

Validation record
-----------------

The x86_64 validation used four vCPUs and 1536 MiB of RAM in QEMU with KVM.
The native and PREEMPT_RT configurations ran three times at four vCPUs. Every
configuration also ran once at one vCPU.

.. list-table::
   :widths: 42 20 14 24
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Native Rust lifetime and teardown
     - SMP4: 3, SMP1: 1
     - 6 of 6
     - Pass
   * - C lifetime and Rust teardown
     - SMP4: 1, SMP1: 1
     - 4 of 4
     - Pass
   * - C lifetime and C teardown
     - SMP4: 1, SMP1: 1
     - 3 of 3
     - Pass
   * - Native Rust lifetime with KASAN
     - SMP4: 1, SMP1: 1
     - 6 of 6
     - Pass
   * - Native Rust lifetime with lockdep
     - SMP4: 1, SMP1: 1
     - 6 of 6
     - Pass
   * - Native Rust lifetime with PREEMPT_RT
     - SMP4: 3, SMP1: 1
     - 6 of 6
     - Pass

The six native cases cover acquisition-plan selection, release-plan selection,
final-cleanup-plan selection, real task-mm acquisition, synchronous final
teardown, and asynchronous final teardown. The C lifetime and Rust teardown
combination runs the four cases that remain applicable. The C-only combination
runs the three real lifetime cases.

An adjacent native SMP4 boot passed ``copy_mm`` 5 of 5 cases, ``exit_mmap`` 2
of 2 cases, ``dup_mmap`` 2 of 2 cases, ``vma_unmap`` 1 of 1 case,
``dup_task_struct`` 2 of 2 cases, ``task_teardown`` 1 of 1 case, and
``task_release`` 2 of 2 cases.

No focused run reported a BUG, warning, KASAN report, lock dependency
diagnosis, Oops, or unexpected panic.

Native ``rust/kernel.o`` defines ``get_task_mm()``, ``mmput()``, and
``mmput_async()``. Native ``kernel/fork.o`` has only undefined references to
``get_task_mm()`` and ``mmput()``. The C lifetime and C teardown combinations
have the reverse ownership. Each linked image has one strong definition of
each entry.

``vmlinux.symvers`` and ``Module.symvers`` record all three symbols with
``EXPORT_SYMBOL_GPL`` in every configuration. The Rust export header uses
``EXPORT_SYMBOL_RUST_GPL`` for each native entry.

The final production build used Clippy with KUnit disabled. The image contains
the three direct lifetime entries. The KUnit selector symbols are absent.
``llvm-nm -u vmlinux`` reports no unresolved symbols.

Disassembly shows locked increment and decrement operations. It also shows
direct calls to the C finalization and asynchronous scheduling boundaries.
The production image boots to the expected missing-root-filesystem panic.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``mm_users`` type, field layout, or atomic ordering contract changes.
* Task locking stops protecting replacement of ``task->mm``.
* ``__mmput()`` becomes safe to implement as a narrow primitive in Rust.
* Work queue initialization or scheduling gains a sound Rust abstraction.
* The final address-space teardown loses an invariant that requires C code.
* A supported architecture cannot map ``AtomicI32`` to ``atomic_t``.

Before a full Rust KUnit PREEMPT_RT run becomes a hard merge gate, update or
conditionally skip the ``rust_spinlock_irq_condvar`` IRQ-disabled assertion in
a separate validation unit.
