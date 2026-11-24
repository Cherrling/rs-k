.. SPDX-License-Identifier: GPL-2.0

Native Rust rcuwait
===================

Status and scope
----------------

The ``CONFIG_RUST_RCUWAIT`` option selects native Rust for the production
``rcuwait`` single-task wait state machine. The public API, object layout, and
export classes remain unchanged.

Rust directly defines the public initialization, activity, publication,
completion, and wake entry points. Rust owns task-state transitions, condition
and signal decisions, timeout accounting, scheduling selection, and wake
ordering.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust owns:

* The public ``rcuwait_init()``, ``rcuwait_active()``,
  ``prepare_to_rcuwait()``, ``finish_rcuwait()``, and
  ``rcuwait_wake_up()`` entry points.
* Initializing and querying the RCU-protected waiter pointer.
* Publishing and completing the current task around the wait.
* Setting the requested task state before each condition evaluation.
* Waiting after a false condition and resuming after a true condition.
* Completing a finite wait from the first condition evaluation.
* Converting a zero timeout to successful completion or timeout retirement.
* Converting condition success and timeout expiry to the documented result.
* Interrupting interruptible waits with ``-EINTR``.
* Selecting normal scheduling or timeout scheduling.
* Clearing the waiter and restoring ``TASK_RUNNING``.
* Ordering the wake condition before the waiter pointer load.

The three ``rust_rcuwait_wait_*()`` helpers implement the state machine used
by the public wait macros. They remain internal GPL exports because the macros
expand in callers, including loadable modules.

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

The native ``kernel/exit.o`` and ``kernel/rcu/update.o`` objects retain the
GPL export metadata for wake and completion. The native ``lib/rcuwait.o``
object retains ordinary export metadata for initialization, activity, and
publication. Neither object contains the corresponding function body.

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

Validation on 2026-11-24 used base commit ``b976f1f3f6e8``. Six x86_64
configurations were built: native Rust, Rust fallback, C-only, generic inline
KASAN, lockdep, and PREEMPT_RT. Native used one SMP1 boot and three SMP4 boots.
PREEMPT_RT used five SMP1 boots and three SMP4 boots. Every other
configuration used one SMP1 boot and one SMP4 boot. All 20 final boots passed
8 of 8 cases.

The first PREEMPT_RT validation exposed a test-worker lifetime race. A worker
published its result and returned before the KUnit thread called
``kthread_stop()``, which allowed the task to be reclaimed first. The test
workers now wait for the stop request after publishing their result. Five
consecutive SMP1 runs and three SMP4 runs passed after this test fix.

The final logs contained no ``BUG``, warning, Oops, kernel panic, KASAN report,
lockdep complaint, or RCU stall.

The native SMP4 image also passed four adjacent suites: ``swait`` with five
cases, ``completion`` with six, ``mm_lifetime`` with six, and ``task_work``
with six. All 23 cases passed.

In native, KASAN, lockdep, and PREEMPT_RT configurations,
``rust/kernel.o`` defined all five public entry points and all three wait
helpers. ``kernel/exit.o`` and ``kernel/rcu/update.o`` contained no public
function bodies. The fallback and C-only configurations defined the public
and wait entry points in C and none in Rust.

Every Rust production ``vmlinux.symvers`` contained ordinary exports for
initialization, activity, and publication, and GPL exports for completion,
wake, and the three wait helpers. The Rust automatic exporter omitted the
five public names to preserve those C-defined export classes.

The native, fallback, and C-only module builds passed modpost. A temporary
external GPL module referenced all eight symbols and linked successfully.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed. Sphinx reported only the existing
``kernel-docs`` and ``getting_llvm`` warnings. Checkpatch required a targeted
ignore for its export-only placement false positives.

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
