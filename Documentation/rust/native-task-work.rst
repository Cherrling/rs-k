.. SPDX-License-Identifier: GPL-2.0

Native Rust task work queues
============================

Status and scope
----------------

Rust directly defines the public task_work entry points when
``CONFIG_RUST_TASK_WORK`` is enabled. The native implementation owns enqueue,
cancellation, exit sealing, queue detachment, callback dispatch, and reentrant
draining.

This unit covers the core task-work queue state machine. It does not migrate
task structure layout, the task PI lock, notification thread flags, IRQ work,
or the kernel callback ABI.

The existing C implementation remains available when the option is disabled.

Entry points and ownership
--------------------------

Rust directly defines all five public ``task_work_*()`` functions. They retain
their names, signatures, and unexported status. The native
``kernel/task_work.o`` object defines no public task_work entry body and
retains the narrow helper boundary.

Rust owns:

* The lockless compare-exchange enqueue loop and list ordering.

* Cancellation traversal, matching, and conditional list unlinking.

* Exit-sentinel publication and rejection of later additions.

* Atomic detachment of a callback list from a task.

* Callback invocation and scheduler progress between callbacks.

* Reentrant callbacks, including work added while an earlier work list drains.

C helpers retain:

* Task field access and the ``pi_lock``.

* ``READ_ONCE()`` and compare-exchange primitives.

* Notification flags, diagnostics, IRQ work, and ``cond_resched()``.

* The callback ABI.

The C helpers do not retain a separate queue or cancellation state machine.

Important invariants
--------------------

The task work list is LIFO. An addition fails with ``-ESRCH`` after an exiting
task's queue has been sealed with the shared exit sentinel.

Callbacks may add more work while ``task_work_run()`` executes. Rust must
detach only the currently published list and continue draining until the task
list is empty. If the task is exiting, the final empty drain publishes the
sentinel.

Cancellation serializes with queue detachment on ``task->pi_lock``. The
compare-exchange on each predecessor link prevents a concurrent enqueue or
detach from removing the wrong callback.

Notification flags are set only after the callback is visible on the list.
Unsupported modes preserve the historical behavior: the work remains queued
and the kernel emits a diagnostic.

Validation gate
---------------

The unit must pass these checks before integration:

* LIFO enqueue ordering and empty-list behavior.

* Exact-pointer, function, predicate, and repeated cancellation.

* Resume, signal, and signal-without-IPI notification flags.

* Reentrant callback addition and draining.

* Exit-sentinel rejection of later additions.

* Concurrent additions and cancellations across multiple CPUs.

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_TASK_WORK=n``.

* C-only builds with ``CONFIG_RUST=n``.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Symbol checks showing Rust defines all five public entry points and the
  native C object defines no public task_work body. The five entries remain
  unexported.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Seventeen final SMP boot tests passed with no failures. The native
configuration passed six cases on one CPU and in three consecutive four-CPU
runs. KASAN passed the same matrix. Lockdep passed six cases on one and four
CPUs, and PREEMPT_RT passed six cases on four CPUs. The fallback configuration
passed five cases on one CPU and in three consecutive four-CPU runs. C-only
passed five cases on one and four CPUs. The adjacent completion KUnit test
also passed six cases on four CPUs.

A scan of all boot logs found no BUG, warning, KASAN report, lock inversion,
panic, or RCU stall. KASAN initialized its generic mode for all four KASAN
boots, and lockdep initialized for both lockdep boots.

The native, KASAN, lockdep, and RT ``rust/kernel.o`` objects define all five
public entry points, while their ``kernel/task_work.o`` objects define none.
The fallback and C-only ``kernel/task_work.o`` objects define all five, while
the fallback Rust object defines none. All six ``Module.symvers`` files and
generated Rust export headers exclude the five public names and the old
``rust_task_work_*`` adapters. The KUnit-only synthetic-task drain bridge
exists in Rust test builds and remains unexported.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The task-work notification modes or callback contract change.

* Task field access or the task PI lock requires a different synchronization
  contract.

* The IRQ work notification path moves out of architecture-independent code.

* Measurements show that the retained helper boundary causes an unacceptable
  enqueue or drain hot-path regression.
