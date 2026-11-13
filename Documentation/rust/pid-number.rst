.. SPDX-License-Identifier: GPL-2.0

Native Rust PID Number Resolution
=================================

Status and scope
----------------

The ``CONFIG_RUST_PID_NUMBER`` option selects native Rust for PID namespace
number resolution. The original C implementation remains available as a
fallback.

This unit covers ``pid_nr_ns()``, ``pid_vnr()``, and
``__task_pid_nr_ns()``. It decides whether a PID is visible in a selected
namespace, which hierarchy level supplies its number, and when to use the
current task's active PID namespace.

The Rust code owns these decisions:

1. Return zero for a missing PID or PID namespace.
2. Reject a namespace deeper than the PID hierarchy.
3. Verify that the selected hierarchy level belongs to the requested namespace.
4. Select the number from the verified hierarchy level.
5. Use the current task's active PID namespace when no namespace is supplied.
6. Resolve PID, thread-group, process-group, and session identifiers through
   the same namespace rules.

Entry points and ownership
--------------------------

The exported ``pid_nr_ns()``, ``pid_vnr()``, and ``__task_pid_nr_ns()``
functions remain C ABI adapters. When the option is enabled, each adapter calls
the matching Rust production entry point and the original C implementation is
compiled out.

The Rust entry point for ``__task_pid_nr_ns()`` establishes an RCU read-side
critical section. The other entry points rely on the caller's PID lifetime or
the current task invariant, as the original implementation did.

Retained C boundary
-------------------

The following operations remain in C because they directly use kernel or
architecture primitives:

* Task PID slot selection and RCU pointer dereference.
* RCU read-side lock and unlock.
* Current task and active PID namespace retrieval.
* PID hierarchy number address calculation.

The helper does not decide namespace visibility or select the returned number.

Ordering and invariants
-----------------------

A PID can only be named by a namespace at or above its deepest recorded level.
The number is valid only when the selected hierarchy entry names the requested
namespace. A different namespace at the same level is not interchangeable.

The task form reads the selected PID slot under RCU. The current namespace
fallback occurs after RCU protection begins and before the task PID slot is
dereferenced.

All PID identifier forms use the same depth, ownership, and fallback rules.

Validation record
-----------------

Each KUnit configuration ran three consecutive times with four vCPUs.

.. list-table::
   :widths: 42 16 16 26
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust with PREEMPT_RT and lockdep
     - 3
     - 4 of 4
     - Pass
   * - C fallback with PREEMPT_RT and lockdep
     - 3
     - 4 of 4
     - Pass
   * - Generic inline KASAN with Rust
     - 3
     - 4 of 4
     - Pass

The tests cover valid hierarchy levels, missing inputs, excessive depth,
same-level namespace mismatch, all task PID types, and current namespace
fallback.

Every KUnit log reported zero failures and no BUG, warning, KASAN report,
Oops, refcount report, or lockdep complaint.

The production build used Clippy with KUnit, KASAN, and ``PROVE_LOCKING``
disabled. The enabled ``PREEMPT_RT`` configuration still selected ``LOCKDEP``.
The C adapters contain direct relocations to all three Rust entry points. The
production image contains no test symbols or unresolved symbols.

The production image started with four vCPUs under KVM and reached the expected
missing-root-filesystem panic without an unexpected diagnostic. A second
production configuration disabled ``CONFIG_PID_NS`` and repeated the build,
link, relocation, and boot checks with the same result.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains a safe abstraction for task PID slots or RCU pointer reads.
* PID hierarchy ordering or ``MAX_PID_NS_LEVEL`` semantics change.
* The current namespace fallback rule changes.
* PID lookup no longer relies on RCU for task association stability.
