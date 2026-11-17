.. SPDX-License-Identifier: GPL-2.0

Native Rust rcuwait
===================

Status and scope
----------------

The ``CONFIG_RUST_RCUWAIT`` option selects native Rust for the production
``rcuwait`` single-task wait state machine. The public API, object layout, and
export classes remain unchanged.

Rust owns wait object initialization, task publication, waiter activity
queries, task-state transitions, condition and signal decisions, timeout
accounting, scheduling selection, completion cleanup, and wake ordering.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust owns:

* Initializing and querying the RCU-protected waiter pointer.
* Publishing the current task before the first condition evaluation.
* Setting the requested task state before each condition evaluation.
* Waiting after a false condition and resuming after a true condition.
* Completing a finite wait from the first condition evaluation.
* Converting a zero timeout to successful completion or timeout retirement.
* Converting condition success and timeout expiry to the documented result.
* Interrupting interruptible waits with ``-EINTR``.
* Selecting normal scheduling or timeout scheduling.
* Clearing the waiter and restoring ``TASK_RUNNING``.
* Ordering the wake condition before the waiter pointer load.

C helpers retain:

* RCU pointer assignment, access, dereference, read locking, and unlocking.
* Current task state stores.
* Pending-signal queries.
* Normal and timeout scheduler calls.
* Task wakeup.
* The scheduler full memory barrier.

The C wait macros retain only the loop needed to expand and evaluate the
caller-provided C condition after Rust sets the task state. They do not choose
whether to wait, retire a timeout, accept a signal, or schedule a task.

Important invariants
--------------------

The caller serializes writes to the waiter pointer. Rust publishes the current
task before the first condition evaluation. Each condition is checked after
the task state is set and before a scheduling action.

``rcuwait_wake_up()`` takes an RCU read-side critical section and executes a
full memory barrier before loading the waiter pointer. The barrier pairs with
the full barrier in the task-state store. This order makes the reason for the
wakeup visible before the waiter observes the condition.

Finite waits preserve the existing return convention. A true initial
condition returns the original timeout. A true condition that completes a
zero-timeout observation returns one. A true condition before expiry returns
the remaining jiffies. An expired condition returns zero. An interruptible
wait returns ``-EINTR``.

Validation gate
---------------

The unit must pass these checks before integration:

* Initialization, activity, publication, completion, and running-state
  restoration.
* Condition ordering after publication and task-state setup.
* A sleeping waiter woken by ``rcuwait_wake_up()``.
* A finite wait woken before expiry.
* Initial condition success, timeout expiry, and zero-timeout results.
* Initial condition call counts and publication ordering.
* Interruptible signal interruption and signal cleanup.
* Repeated wait and wake handoffs.
* Native Rust builds and boots with one and four CPUs.
* Rust fallback builds with ``CONFIG_RUST_RCUWAIT=n``.
* C-only builds with ``CONFIG_RUST=n``.
* Native Rust with KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.
* Symbol and disassembly checks for the production Rust entry points.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

All builds used isolated output directories and the Clang LLVM toolchain.
Fifteen x86_64 boots covered the production matrix. Native Rust ran once on
SMP1 and twice on SMP4. Rust fallback and C-only each ran on SMP1 and SMP4.
KASAN ran on SMP1 and SMP4. Lockdep ran once on SMP1 and twice on SMP4.
PREEMPT_RT ran once on SMP1 and twice on SMP4. Every boot reported
``rcuwait: pass:8 fail:0 skip:0 total:8``.

The KASAN boots initialized generic KernelAddressSanitizer. The lockdep boots
initialized the lock dependency validator. A scan of all final boot logs found
no failing KUnit case, BUG, WARNING, Oops, panic, RCU stall, KASAN report, or
lockdep report.

The validated KASAN configuration used ``CONFIG_FRAME_WARN=8192``. The first
attempt with the generic value of 2048 stopped on a 2072-byte stack frame in
``vfs_coredump()`` before this unit ran. The completed KASAN build emitted one
unrelated objtool warning for ``rust_maple_tree_store_complex()``. No rcuwait
diagnostic appeared in any build.

Four additional SMP4 boots ran the focused regression suite. ``swait`` passed
5 cases, ``completion`` passed 6, ``mm_lifetime`` passed 6, and ``task_work``
passed 6. All four suites reported zero failures and zero skips.

The native, KASAN, lockdep, and PREEMPT_RT ``vmlinux`` files contained all
eight ``rust_rcuwait_*`` entry points. The Rust fallback and C-only files
contained none. Object relocations showed production calls to
``rust_rcuwait_wake_up()`` from ``kernel/exit.o`` and
``rust_rcuwait_finish()`` from ``kernel/rcu/update.o``.

Rustfmt, Clippy, Rustdoc, ``git diff --check``, and strict checkpatch passed.
Strict checkpatch reported 0 errors, 0 warnings, and 0 checks. Focused Sphinx
generation rendered ``native-rcuwait.html``. The ``-W`` build stopped only on
two existing cross-directory references in other Rust pages. Neither warning
concerned the new page.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for RCU pointers, task state, signals,
  scheduling, or task wakeup.
* The waiter pointer becomes a non-pointer token or uses a different RCU
  access mode.
* The condition evaluation order, timeout convention, or signal result
  changes.
* ``rcuwait_wait_event()`` gains another scheduling action.
* The wake path requires an operation that cannot remain a narrow scheduler
  primitive.
