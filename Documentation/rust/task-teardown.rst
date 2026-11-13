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

The exported C ``__put_task_struct()`` entry point remains in
``kernel/fork.c``. When the option is enabled, its C ABI adapter calls
``rust_task_put_final()`` and the original C cleanup sequence is compiled out.

The RCU callback ``__put_task_struct_rcu_cb()`` remains a C adapter. It
recovers the ``task_struct`` from its embedded ``rcu_head`` and calls the
existing ``__put_task_struct()`` entry point, which enters Rust.

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

The x86_64 validation used four vCPUs, 1536 MiB of RAM, PREEMPT_RT, and
lockdep. KVM was used when available, with TCG as the fallback accelerator.

The test creates 16 kernel threads. It waits for each thread function to start,
then takes the extra task reference required by ``kthread_stop_put()``. It stops
the thread, releases that reference, and calls ``rcu_barrier()`` to wait for
deferred task teardown callbacks.

.. list-table::
   :widths: 42 16 16 26
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust task teardown
     - 3
     - 1 of 1
     - Pass
   * - C task teardown fallback
     - 3
     - 1 of 1
     - Pass
   * - KASAN with Rust task teardown
     - 3
     - 1 of 1
     - Pass

The KASAN configuration used generic inline KASAN in addition to PREEMPT_RT
and lockdep.

Every log reported zero failures and no BUG, warning, KASAN report, Oops,
lockdep complaint, or kernel panic.

The production build used Clippy with KUnit, PREEMPT_RT, and KASAN disabled.
The image contains ``rust_task_put_final``. KUnit selector symbols are absent,
and ``llvm-nm -u vmlinux`` reports no unresolved symbols. The production image
boots to the expected missing-root-filesystem panic.

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
