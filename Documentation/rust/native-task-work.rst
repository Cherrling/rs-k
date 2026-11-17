.. SPDX-License-Identifier: GPL-2.0

Native Rust task work queues
============================

Status and scope
----------------

The ``CONFIG_RUST_TASK_WORK`` option selects native Rust implementations for
enqueue, cancellation, exit sealing, queue detachment, callback dispatch, and
reentrant draining in ``kernel/task_work.c``.

This unit covers the core task-work queue state machine. It does not migrate
task structure layout, the task PI lock, notification thread flags, IRQ work,
or the kernel callback ABI.

The existing C implementation remains available when the option is disabled.

Entry points and ownership
--------------------------

The existing ``task_work_*()`` functions remain C ABI entry points. When the
option is enabled, each entry point calls a native Rust implementation.

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

* Public ABI adapters and the callback ABI.

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

* Symbol checks for all five Rust entry points.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Ten final SMP boot tests passed with no failures. The native, KASAN, and
lockdep configurations passed six cases on one and four CPUs. The fallback
and C-only configurations passed five cases on one and four CPUs.

A scan of all ten boot logs found no BUG, warning, KASAN report, lock
inversion, panic, or RCU stall. KASAN initialized its generic mode for both
KASAN boots, and lockdep initialized for both lockdep boots.

The native, KASAN, and lockdep ``kernel/task_work.o`` objects reference all
five Rust entry points. The fallback and C-only objects reference none of
them. The five C ABI entry points tail-call their Rust implementations in the
native object.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The task-work notification modes or callback contract change.

* Task field access or the task PI lock requires a different synchronization
  contract.

* The IRQ work notification path moves out of architecture-independent code.

* Measurements show that crossing the C ABI on enqueue causes an unacceptable
  hot-path regression.
