.. SPDX-License-Identifier: GPL-2.0

Native Rust CPU-Priority Entry Points
=====================================

Status and scope
----------------

The ``CONFIG_RUST_CPUPRI`` option selects direct Rust definitions of the
production scheduler CPU-priority entry points. The existing ``struct cpupri``
layout and calling contracts remain unchanged. The native build compiles out
the C priority conversion, vector search, fitness selection, and transition
implementations. C fallback and C-only configurations retain them.

The Rust code owns these production decisions:

* Scheduler priority conversion to CPU-priority vectors.
* Lowest-priority vector search for RT and deadline migration.
* Task-affinity and active-CPU mask intersection.
* Capacity fitness filtering and the priority-first fallback search.
* Ordered CPU transitions between priority vectors.

Cpumask storage allocation, cpumask primitives, atomics and memory barriers,
run-queue locking, and invalid-priority diagnostics remain in C.

Entry points and ownership
--------------------------

When ``CONFIG_RUST_CPUPRI`` is enabled, ``rust/kernel/cpupri_core.rs`` directly
defines these production scheduler entry points:

* ``cpupri_find()``.
* ``cpupri_find_fitness()``.
* ``cpupri_set()``.

The native C object does not define these entries or the shared priority
conversion and search implementation.

The Rust-to-C helper calls are:

* Priority-vector mask lookup.
* Atomic vector-count reads, increments, and decrements.
* Cpumask intersection, empty, any-CPU, set, clear, and iteration operations.
* Read, before-atomic, and after-atomic memory barriers.
* Invalid task-priority warning and invalid CPU-priority bug checks.

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

* Rust-enabled, Rust fallback, and C-only x86_64 builds.
* One-vCPU native, fallback, and C-only boots.
* Three consecutive four-vCPU native boots.
* Four-vCPU fallback, C-only, KASAN, lockdep, and PREEMPT_RT boots.
* A ``CONFIG_CPUMASK_OFFSTACK`` boot with debug cpumask allocation.
* Priority-vector conversion boundaries.
* Affinity, active-CPU, lowest-priority, and equal-priority mask selection.
* Fitness filtering and priority-first fallback.
* Concurrent vector transitions, valid returned masks, and eventual search
  success after transitions finish.
* Production symbol ownership and exported-symbol inspection.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The CPU-priority entry points were validated on 2026-11-24 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Twelve boots passed:

* The one-vCPU native, fallback, and C-only runs passed 3 cases and skipped
  the 3 tests that require two active CPUs.
* Three consecutive four-vCPU native runs each passed all 6 KUnit cases.
* The four-vCPU fallback and C-only runs each passed all 6 KUnit cases.
* The four-vCPU KASAN, lockdep, PREEMPT_RT, and off-stack cpumask runs each
  passed all 6 KUnit cases.
* The KUnit cases cover the full 101 by 101 priority search matrix, null and
  non-null candidate masks, affinity and active-CPU filtering, equal and
  different priority vectors, fitness filtering, priority-first fallback,
  invalid vector transitions, and concurrent vector changes.
* The KASAN run exposed an invalid-vector test that indexed before
  ``pri_to_cpu``. The test now checks the invalid vector without that access.
* The concurrent test accepts the documented transient search miss during a
  transition. Every successful result must be a nonempty subset of the active
  CPUs, and the final search must succeed after all workers stop.
* No run reported a BUG, warning, KASAN report, lockdep complaint, kernel
  panic, or atomic-scheduling violation.
* Native ``rust/kernel.o`` defines ``cpupri_find()``,
  ``cpupri_find_fitness()``, and ``cpupri_set()``. Native
  ``kernel/sched/build_utility.o`` retains only initialization, cleanup, and
  the Rust helper boundary.
* Fallback and C-only integrated objects define the complete C state machine.
  The PREEMPT_RT ``vmlinux`` has one definition of each production entry.
  No old ``rust_cpupri_*`` production symbol remains, and
  ``vmlinux.symvers`` does not export the three unexported entries.

Review conditions
-----------------

Reopen the retained C boundary if any of these conditions changes:

* Rust gains a stable abstraction for dynamic cpumasks.
* CPU-priority encoding or vector ordering changes.
* Scheduler locking for CPU-priority updates changes.
* ``CONFIG_CPUMASK_OFFSTACK`` support changes the mask representation.
* Benchmarks show that out-of-line scheduler entry points add unacceptable
  migration-path overhead.
