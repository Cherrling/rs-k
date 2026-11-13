.. SPDX-License-Identifier: GPL-2.0

Native Rust Address-Space Lifetime
==================================

Status and scope
----------------

The ``CONFIG_RUST_MM_LIFETIME`` option selects native Rust for the production
``mm_users`` lifetime path. The configuration default is enabled when Rust
support and an MMU are available. The C implementation remains available as a
fallback.

This unit covers three public entry points:

* ``get_task_mm()``
* ``mmput()``
* ``mmput_async()``

The migration excludes device drivers and the final address-space teardown.
It does not change the public C ABI.

The Rust code owns these production decisions:

* Rejecting a kernel thread before task-mm acquisition.
* Selecting whether a task has an address space to acquire.
* Incrementing ``mm_users`` before the task lock is released.
* Decrementing ``mm_users`` for both synchronous and asynchronous release.
* Selecting synchronous finalization when the user count reaches zero.
* Selecting asynchronous work queueing when the user count reaches zero.

Entry points and ownership
--------------------------

The exported functions in ``kernel/fork.c`` remain C ABI adapters. When the
Rust option is enabled, they call these Rust production functions:

* ``rust_mm_get_task()``
* ``rust_mm_put()``
* ``rust_mm_put_async()``

The original C lifetime decisions are compiled out for these entry points.
The C adapter for ``mmput()`` keeps ``might_sleep()`` outside the Rust call.

The Rust implementation uses these narrow C helpers:

* ``rust_helper_mm_users_ptr()`` returns the address of
  ``struct mm_struct.mm_users``.
* ``rust_helper_mm_task_is_kernel_thread()`` reads ``PF_KTHREAD``.
* ``rust_helper_mm_task_lock()`` and ``rust_helper_mm_task_unlock()`` acquire
  and release the existing task lock.
* ``rust_helper_mm_task_mm()`` reads ``task->mm`` while the task lock is held.
* ``rust_helper_mm_put_final()`` calls the existing ``__mmput()`` teardown.
* ``rust_helper_mm_put_async_schedule()`` initializes and schedules the
  existing asynchronous work item.

Ordering and invariants
-----------------------

For ``get_task_mm()``, Rust checks ``PF_KTHREAD`` before taking the task lock.
The flag is fixed for the life of a task. Rust then takes ``task->alloc_lock``,
reads ``task->mm``, increments ``mm_users`` while the lock remains held, and
unlocks the task. The lock prevents ``task->mm`` from being replaced while the
reference is acquired.

For ``mmput()``, Rust performs one atomic decrement. A result other than one
returns without finalization. A result equal to one transfers the final user
reference to ``__mmput()``.

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
* The final address-space teardown in ``__mmput()``.
* Work item initialization, work queueing, and callback execution.
* The warning and fatal invariants inside ``__mmput()``.

The inline ``mmget()`` and ``mmget_not_zero()`` operations used by other
callers also remain in C. This unit migrates the production acquisition path
in ``get_task_mm()``. It does not claim that every ``mm_users`` increment in
the kernel is native Rust.

Validation record
-----------------

The x86_64 validation used four vCPUs and 1536 MiB of RAM in QEMU with KVM.
Each configuration ran three times.

.. list-table::
   :widths: 36 16 16 32
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust implementation with KUnit
     - 3
     - 5 of 5
     - Pass
   * - C fallback with KUnit
     - 3
     - 3 of 3
     - Pass
   * - Rust implementation with KASAN
     - 3
     - 5 of 5
     - Pass
   * - Rust implementation with lockdep
     - 3
     - 5 of 5
     - Pass
   * - Rust implementation with PREEMPT_RT
     - 3
     - 5 of 5
     - Pass

After the final source review fixes, the integration configuration was rebuilt
and rerun three times. Each run passed all five MM lifetime cases with no
warning, BUG, KASAN report, lockdep complaint, or panic. The C fallback was also
rebuilt and passed its three lifetime cases.

The five Rust cases cover acquisition-plan selection, release-plan selection,
real task-mm acquisition, synchronous final teardown, and asynchronous final
teardown. The C fallback runs the three real lifetime cases.

The PREEMPT_RT integration kernel also compiled the scheduler wake KUnit test.
Three targeted runs passed all 37 scheduler wake cases. Three targeted runs
passed all five MM lifetime cases. The targeted logs had no warning, BUG,
KASAN report, lockdep complaint, or panic.

A separate all-suite diagnostic boot used ``kunit.filter_glob=*`` on the
PREEMPT_RT kernel. Both target suites passed. That boot also emitted a warning
from the pre-existing ``rust_spinlock_irq_condvar`` test. The test asserts that
interrupts are disabled while holding a regular ``spinlock_t``. This assertion
does not hold under PREEMPT_RT spinlock semantics. The test and helper are not
changed by this unit.

The final production build used Clippy with KUnit disabled. The image contains
``rust_mm_get_task``, ``rust_mm_put``, and ``rust_mm_put_async``. The KUnit
selector symbols are absent. ``llvm-nm -u vmlinux`` reports no unresolved
symbols.

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
