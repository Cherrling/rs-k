.. SPDX-License-Identifier: GPL-2.0

Native Rust Final Task Teardown
===============================

Status and scope
----------------

The ``CONFIG_RUST_TASK_TEARDOWN`` option selects native Rust for the
production orchestration of ``__put_task_struct()``. The original C sequence
remains available as a fallback.

This unit covers final task resource release after the last ``task_struct``
reference is dropped. It excludes the inline ``put_task_struct()`` reference
count fast path, individual subsystem cleanup implementations, and final task
memory release in ``free_task()``. It does not change the public C ABI.

The Rust code owns this cleanup order:

1. Validate the final task invariants.
2. Release deferred-unwind state.
3. Release io_uring and cgroup state.
4. Release NUMA and security state.
5. Release credentials and delay-accounting state.
6. Release the signal structure reference.
7. Release scheduler-core state.
8. Call the final ``free_task()`` boundary.

Entry points and ownership
--------------------------

Rust directly defines the exported GPL ``__put_task_struct()`` entry. The
native build compiles the original C cleanup sequence out of ``kernel/fork.c``.
C fallback builds retain the complete C function and its GPL export metadata.

The RCU callback ``__put_task_struct_rcu_cb()`` remains a C adapter. It
recovers the ``task_struct`` from its embedded ``rcu_head`` and calls the
existing ``__put_task_struct()`` entry point, which Rust defines.

The Rust implementation uses narrow C helpers for individual operations:

* The existing fatal-state and reference diagnostics.
* Deferred-unwind cleanup.
* io_uring, cgroup, NUMA, and security cleanup.
* Credential and delay-accounting cleanup.
* Signal-structure reference release.
* Scheduler-core cleanup.
* Final ``free_task()``.

These helpers do not select ordering. Rust executes the sequence and calls the
final memory-release boundary last.

Retained C boundary
-------------------

The following operations remain in C because they directly use subsystem,
architecture, or kernel internals:

* The inline task reference-count decrement and RCU scheduling path.
* Final task invariant diagnostics.
* Every subsystem-specific cleanup function.
* Signal-structure reference handling.
* Stack, architecture, BPF, and cache release inside ``free_task()``.
* The RCU callback ABI.

The inline ``put_task_struct()`` path stays in C because it is a hot,
performance-critical reference decrement. This unit starts at
``__put_task_struct()``, which runs only after that decrement reaches zero.

Ordering and invariants
-----------------------

The Rust sequence preserves the original C order. Credentials are released
before the signal structure. Scheduler-core state is released after signal
cleanup. ``free_task()`` is the last operation and consumes the task storage.

The original invariant checks remain in C. A zero reference count with a live
exit state and a task other than ``current`` is required before teardown.

The task cannot be accessed after the final helper returns. Rust returns
immediately after calling the final ``free_task()`` boundary.

Validation record
-----------------

The native configuration passed the focused case in three consecutive SMP4
boots and one SMP1 boot. Fallback, C-only, KASAN, lockdep, and PREEMPT_RT each
passed the focused case at SMP4 and SMP1. KVM was used with TCG as the backup
accelerator.

The KASAN configuration used generic inline KASAN. PREEMPT_RT and lockdep
were separate configurations.

The test creates 16 kernel threads. It waits for each thread function to start,
then takes the extra task reference required by ``kthread_stop_put()``. It stops
the thread, releases that reference, and calls ``rcu_barrier()`` to wait for
deferred task teardown callbacks.

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
     - 1 of 1
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
     - 1 of 1
   * - Lockdep
     - 1
     - 1
     - 1 of 1
   * - PREEMPT_RT
     - 1
     - 1
     - 1 of 1

Every focused run reported no failed case, BUG, KASAN report, lockdep
complaint, Oops, or unexpected panic.

The production build used Clippy with KUnit, PREEMPT_RT, and KASAN disabled.
Native ``rust/kernel.o`` contains the only strong ``__put_task_struct()``
definition. Native ``kernel/fork.o`` contains the C RCU callback and an
undefined reference to the Rust entry. The RCU callback relocates directly to
``__put_task_struct``.

``__put_task_struct()`` and ``__put_task_struct_rcu_cb()`` both remain
GPL-only exports. ``Module.symvers`` contains no old Rust wrapper or KUnit
selector symbol. ``nm -u vmlinux`` reports no unresolved symbol. The four-vCPU
production image boots to the expected missing-root-filesystem panic, is not
tainted, and reports no WARN, BUG, KASAN report, lockdep complaint, Oops, RCU
stall, or unexpected panic.

Rustfmt, Clippy, and Rustdoc pass. The focused Rust Sphinx build reports only
the existing unknown ``kernel-docs`` document and undefined ``getting_llvm``
label warnings. Strict checkpatch reports zero errors and zero warnings.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* A subsystem cleanup function gains a Rust-native safe abstraction.
* The subsystem cleanup dependency or ordering rules change.
* ``free_task()`` gains a Rust implementation with the same architecture and
  cache-lifetime contract.
* The task reference-count path becomes out of line and can move to Rust
  without a measured hot-path regression.
* A helper starts to select teardown policy instead of performing one
  primitive operation.
