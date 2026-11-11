.. SPDX-License-Identifier: GPL-2.0

Native Rust CPU-Priority State Machine
======================================

Status and scope
----------------

The ``CONFIG_RUST_CPUPRI`` option selects a native Rust implementation of the
scheduler CPU-priority state machine. The existing ``struct cpupri`` layout
and scheduler entry points remain unchanged.

The Rust code owns these production decisions:

* Scheduler priority conversion to CPU-priority vectors.
* Lowest-priority vector search for RT and deadline migration.
* Task-affinity and active-CPU mask intersection.
* Capacity fitness filtering and the priority-first fallback search.
* Ordered CPU transitions between priority vectors.

Cpumask storage allocation, cpumask primitives, atomics and memory barriers,
run-queue locking, invalid-priority diagnostics, and the public scheduler
adapters remain in C.

Entry points and ownership
--------------------------

``kernel/sched/cpupri.c`` keeps the scheduler-facing entry points
``cpupri_find()``, ``cpupri_find_fitness()``, and ``cpupri_set()``. When the
Rust configuration is enabled, each entry is a tail-call adapter to the state
machine in ``rust/kernel/cpupri_core.rs``.

The Rust-to-C helper calls are:

* Priority-vector mask lookup.
* Atomic vector-count reads, increments, and decrements.
* Cpumask intersection, empty, any-CPU, set, clear, and iteration operations.
* Read, before-atomic, and after-atomic memory barriers.
* Invalid task-priority warning and invalid CPU-priority bug checks.

The C-to-Rust production entries are:

* ``rust_cpupri_find()``.
* ``rust_cpupri_find_fitness()``.
* ``rust_cpupri_set()``.

``cpupri_init()`` and ``cpupri_cleanup()`` remain in C because they allocate and
free the vector cpumasks and the ``cpu_to_pri`` array. They do not select CPUs,
convert task priorities, search vectors, or execute transition policy.

Retained C boundary
-------------------

Cpumask operations remain in C because the mask representation depends on
architecture and ``CONFIG_CPUMASK_OFFSTACK``. The helpers expose individual
mask primitives only. They do not run search loops or decide which vector wins.

Atomic operations and barriers remain in C because their implementation and
memory ordering depend on the architecture. Rust reads the vector count,
performs the read barrier, then reads the vector mask. Rust also owns the exact
add-before-remove and count-before-mask ordering used during transitions.

The scheduler continues to hold the run-queue lock for ``cpupri_set()``. The
lock protects the selected CPU entry against concurrent schedulers. Rust
preserves this contract and does not add another lock.

Important invariants
--------------------

The vector index mapping is:

* ``CPUPRI_INVALID`` maps to ``CPUPRI_INVALID``.
* Scheduler priorities 0 through 98 map to vectors 99 through 1.
* Scheduler priority 99 maps to ``CPUPRI_NORMAL``, vector 0.
* Scheduler priority 100 maps to ``CPUPRI_HIGHER``, vector 100.

A search considers vectors with indexes lower than the task's converted CPU
priority. This preserves the existing strict-priority rule.

When a CPU changes vectors, the new mask is set and its count is incremented
before the old vector is changed. If both vectors exist, an after-atomic barrier
orders the new count before the old count decrement. The old count is
decremented before its mask is cleared. These orders prevent a concurrent
search from missing a CPU during a priority rise.

The fit callback can remove CPUs from the current candidate mask. If no
candidate fits, Rust repeats the search without the callback. This preserves
the scheduler rule that priority wins over CPU capacity fitting.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled, C fallback, KASAN, and lockdep boots.
* A ``CONFIG_CPUMASK_OFFSTACK`` boot with debug cpumask allocation.
* One-vCPU and three consecutive four-vCPU Rust KUnit runs.
* Priority-vector conversion boundaries.
* Affinity, active-CPU, lowest-priority, and equal-priority mask selection.
* Fitness filtering and priority-first fallback.
* Concurrent vector transitions and lock-free searches.
* Adapter disassembly and unresolved-symbol inspection.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The CPU-priority state machine was validated on 2026-11-11 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all 6 KUnit cases.
* The one-vCPU Rust run passed 3 cases and skipped the 3 tests that require
  two active CPUs.
* The four-vCPU C fallback, generic inline KASAN, lockdep, and PREEMPT_RT runs
  each passed all 6 KUnit cases.
* A four-vCPU Rust run with ``CONFIG_CPUMASK_OFFSTACK`` and
  ``CONFIG_DEBUG_PER_CPU_MAPS`` enabled also passed all 6 KUnit cases.
* The KUnit cases cover the full 101 by 101 priority search matrix, null and
  non-null candidate masks, affinity and active-CPU filtering, equal and
  different priority vectors, fitness filtering, priority-first fallback,
  invalid vector transitions, and concurrent vector changes.
* No run reported a BUG, warning, KASAN report, lockdep complaint, kernel
  panic, or atomic-scheduling violation.
* ``cpupri_find()``, ``cpupri_find_fitness()``, and ``cpupri_set()`` compile
  to direct tail calls into Rust. The Rust configuration contains no C
  conversion, vector search, or transition implementation.
* The C fallback contains no Rust CPU-priority symbols. The Rust object
  defines the three production entries and a shared native search routine.
* Clippy and Rustfmt pass with the kernel warning policy.
  ``git diff --check`` passes. Strict checkpatch reports no errors or checks.
  Its two warnings ask about new file paths and the retained ``BUG_ON``
  invariant. ``MAINTAINERS`` covers the new paths, and the ``BUG_ON``
  preserves the existing scheduler contract for invalid priority input.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Rust gains a stable abstraction for dynamic cpumasks.
* CPU-priority encoding or vector ordering changes.
* Scheduler locking for CPU-priority updates changes.
* ``CONFIG_CPUMASK_OFFSTACK`` support changes the mask representation.
* Benchmarks show that out-of-line scheduler entry points add unacceptable
  migration-path overhead.
