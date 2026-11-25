.. SPDX-License-Identifier: GPL-2.0

Native Rust PID Number Resolution
=================================

Status and scope
----------------

The ``CONFIG_RUST_PID_NUMBER`` option selects native Rust for PID namespace
number resolution. Rust directly defines the production entries. The complete
C implementation remains available as a fallback and in C-only builds.

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

Rust directly defines the production ``pid_nr_ns()``, ``pid_vnr()``, and
``__task_pid_nr_ns()`` entries. The native build compiles their C
implementations out of ``kernel/pid.c``. C retains only the original non-GPL
export metadata for ``__task_pid_nr_ns()``. The first two entries retain their
original GPL-only export class through the Rust export generator.

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

The x86_64 validation used the final source for every KUnit configuration.
KASAN used Clang, ``LLVM=1``, and generic inline KASAN.

.. list-table::
   :widths: 38 24 16 22
   :header-rows: 1

   * - Configuration
     - Boots
     - Cases
     - Result
   * - Native
     - 3 SMP4, 1 SMP1
     - 4 of 4
     - Pass
   * - Rust fallback
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - C-only
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - KASAN with native
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - Lockdep with native
     - SMP4, SMP1
     - 4 of 4
     - Pass
   * - PREEMPT_RT with native
     - SMP4, SMP1
     - 4 of 4
     - Pass

The tests cover valid hierarchy levels, missing inputs, excessive depth,
same-level namespace mismatch, all task PID types, and current namespace
fallback.

Every focused KUnit run reported zero failures and no BUG, Oops, KASAN
report, refcount report, or lockdep complaint.

The native SMP4 adjacent PID lifecycle, allocation, association, number,
task release, signal exit, and task exit notification suites passed 30 of 30
cases.

Native ``rust/kernel.o`` defines all three entries and native ``kernel/pid.o``
defines none. The linked image contains one definition of each entry, no
``rust_pid_*`` wrapper, no unresolved symbol, and the original export classes.
``pid_nr_ns()`` and ``pid_vnr()`` remain GPL-only exports, while
``__task_pid_nr_ns()`` remains an ordinary export.

The production Clippy build produced ``bzImage`` and modules. Its four-vCPU
KVM boot reached the expected missing-root-filesystem panic without an
unexpected diagnostic. A second production build disabled ``CONFIG_PID_NS``,
retained the same Rust entries, and reached the same expected panic.

Rustfmtcheck, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx reported only the two existing
cross-reference warnings in ``index.rst`` and ``quick-start.rst``.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains a safe abstraction for task PID slots or RCU pointer reads.
* PID hierarchy ordering or ``MAX_PID_NS_LEVEL`` semantics change.
* The current namespace fallback rule changes.
* PID lookup no longer relies on RCU for task association stability.
