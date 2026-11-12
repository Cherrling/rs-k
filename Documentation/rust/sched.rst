.. SPDX-License-Identifier: GPL-2.0

Native Rust Scheduler Core Paths
================================

Status and scope
----------------

The ``CONFIG_RUST_SCHED_WAKE_STATE`` option selects native Rust state matching
for task wakeups and inactive-task waits. The existing ``struct task_struct``
layout and scheduler entry points remain unchanged.
The ``CONFIG_RUST_SCHED_TTWU_RUNNABLE`` option selects Rust for the runqueue
locked path that handles a wake target which is still queued.
The ``CONFIG_RUST_SCHED_TTWU_ACTIVATE`` option selects Rust for the production
woke-task activation state machine.
The ``CONFIG_RUST_SCHED_BLOCK_TASK`` option selects Rust for the production
task blocking state machine.

Rust owns these production decisions:

* Matching the requested wake mask against the current task state.
* Falling back to the saved task state when the current state does not match.
* Giving a current-state match precedence over a saved-state match.
* Reporting wake success independently from whether the task can run now.
* Clearing a matched saved state to ``TASK_RUNNING``.
* Checking whether the wake target remains queued after the runqueue lock is
  acquired.
* Updating the runqueue clock, handling delayed and proxy state, selecting
  preemption, and publishing the final running state.
* Removing uninterruptible-load accounting, ending I/O wait accounting,
  selecting enqueue flags, activating the task, selecting preemption,
  publishing the wakeup, and deciding whether to invoke the scheduling-class
  ``task_woken`` callback.
* Classifying whether a blocking task contributes to load, classifying whether
  its state is special, updating its load flag, selecting special dequeue,
  and deciding whether dequeue and block accounting run.

Task-state memory accesses, warning delivery, task and runqueue locking,
scheduler queue and proxy operations, wake-list handling, CPU selection,
preemption, tracing, statistics, and public scheduler ABI adapters remain in
C. Runqueue queue operations, the class callback, and its lock unpin and
repin also remain in C. The task load bitfield write, blocking dequeue, and
block accounting primitives also remain in C.

Entry points and ownership
--------------------------

``kernel/sched/core.c`` keeps the scheduler-facing ``__task_state_match()``
and ``ttwu_state_match()`` adapters. When the Rust option is enabled, the task
state result and wake-state transition come from
``rust/kernel/sched_core.rs``.

The Rust-to-C helper calls are:

* Task current-state and saved-state ``READ_ONCE`` accesses.
* The saved-state write.
* The invalid debug-preemption wake-mask warning.
* The queued-state read, runqueue clock update, delayed and proxy state
  queries, delayed enqueue, preemption, and final wake publication used by the
  runnable path.
* The load and I/O wait queries, load and I/O wait accounting updates,
  enqueue-flag selection, task activation, preemption, wake publication, and
  scheduling-class callback used by the activation path.
* Runqueue lock unpin and repin around a scheduling-class ``task_woken``
  callback.
* The task contributes-to-load bitfield write, blocking dequeue with selected
  sleep and special flags, and block accounting.

The C-to-Rust production entries are:

* ``rust_sched_task_state_match()``.
* ``rust_sched_ttwu_state_match()``.
* ``rust_sched_ttwu_runnable_locked()``.
* ``rust_sched_ttwu_activate_locked()``.
* ``rust_sched_block_task_locked()``.

The caller keeps the existing ``p->pi_lock`` serialization, or the current-task
preemption exclusion, around the state-match entries. For the runnable entry,
C acquires the task runqueue lock, calls Rust while the lock is held, and
releases the lock after Rust returns. Rust does not acquire or release
scheduler locks.

For the activation entry, C validates that it holds the runqueue lock and
passes the matching lock context to Rust. Rust selects and orders all state
changes and scheduler helpers. The C ``task_woken`` helper temporarily
unpins and repins the lock exactly as the original C path did to preserve the
scheduling-class callback lock contract.

For the blocking entry, C validates that the task runqueue lock is held and
passes the task state to Rust. Rust classifies the state and calls the bitfield,
dequeue, and accounting helpers in order. C retains ownership of the runqueue
lock across the complete Rust call.

Retained C boundary
-------------------

Task-state accesses remain in C because ``READ_ONCE`` and the task layout are
defined by the C scheduler ABI. The helper reads current and saved state only.
Rust receives values and performs the matching decision.

The saved-state write remains in C to preserve the existing task-structure
access. Rust decides when the write is required.

``WARN_ON_ONCE`` remains in C because report formatting and rate limiting
depend on kernel diagnostics. Rust decides whether the debug-preemption state
mask is invalid.

``try_to_wake_up()`` keeps its memory barriers, wake-list handling, CPU
selection, migration, full enqueue, statistics, and accounting in C. The
runnable unit changes only the path inside the task runqueue lock.

The runnable path retains runqueue lock acquisition and release, scheduler
queue and proxy operations, preemption, and tracing in C. Rust owns their
order and the decision to invoke each helper. The C adapter cannot return
while Rust still holds the lock because the lock remains owned by the C
caller.

The activation path retains runqueue lock ownership, enqueue-flag bit
definitions, scheduler queue operations, preemption, tracing, and the
scheduling-class callback in C. Rust owns the conditions and order for load
accounting, I/O wait completion, task activation, preemption, wake
publication, and callback selection. The C lock-unpin helper preserves the
existing callback lock contract while Rust still owns the caller's state
machine.

The blocking path retains task-state and bitfield writes, runqueue locking,
scheduler queue operations, accounting primitives, tracing, and public
scheduler ABI adapters in C. Rust owns the load and special-state masks, the
conditions for updating the load flag, the special dequeue decision, and the
order of dequeue and accounting. The accounting helper runs only after the
scheduling class accepts the dequeue.

Important invariants
--------------------

A current-state match returns positive and wakes the task.

A saved-state-only match returns negative, reports wake success, and clears
the saved state to ``TASK_RUNNING``. The task becomes runnable only when the
lock or freezer restore completes.

No match returns zero and reports wake failure.

When both states match, the current state wins and the saved state is not
changed.

Under ``CONFIG_DEBUG_PREEMPT``, a wake mask that contains
``TASK_RTLOCK_WAIT`` with any other state bit presents an invalid
``TASK_NORMAL`` and must issue the existing warning.

The runnable path first checks that the task is queued. It updates the
runqueue clock only after that check. For a blocked task, it enqueues delayed
scheduler state first, then asks the proxy path whether the task must return.
A proxy return ends the wake attempt without preemption or wake publication.

For a queued task that must stay on this runqueue, Rust requests preemption
only when the task is not currently on CPU. The final action publishes
``TASK_RUNNING`` and clears the blocked marker while the runqueue lock remains
held.

The activation path decrements ``nr_uninterruptible`` before activation when
the task contributes to load. A non-migrated I/O wait task ends delay
accounting and decrements the selected runqueue's ``nr_iowait`` before
activation. A migrated task does not perform I/O wait completion. Rust
activates the task before requesting preemption, publishes the wakeup before
the scheduling-class callback, and invokes that callback only when the
scheduling class supplies one.

The blocking path marks a task as contributing to load only when its state
contains ``TASK_UNINTERRUPTIBLE`` and lacks ``TASK_NOLOAD`` and
``TASK_FROZEN``. Stopped, traced, parked, dead, waking, and frozen states use
the special dequeue flag even when they do not contribute to load. Rust writes
the load flag before dequeue, requests sleeping dequeue on the current
runqueue, and requests block accounting only when dequeue succeeds.

Validation gate
---------------

The Rust KUnit cases cover the state-match table, saved-state transition,
activation action selection, blocking action selection, and a real
uninterruptible timeout. Real wakeup cases cover current-task rejection,
waking a stopped kernel thread, and waking a queued runnable task with both
the Rust and C implementations.

The unit must pass repeated four-vCPU Rust runs, a C fallback run, PREEMPT_RT,
KASAN, lockdep, and a no-KUnit production build. Static checks include Rustfmt,
Clippy, strict checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The wake-state unit was validated on 2026-11-12 with x86_64 ``LLVM=1`` builds
and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all four wake-state cases.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive four-vCPU C fallback runs passed both real wakeup cases
  with ``CONFIG_RUST_SCHED_WAKE_STATE`` disabled.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all four wake-state cases and all twenty-five hrtimer
  regression cases.
* The no-KUnit production image built with Clippy while the Rust wake-state
  option was enabled.
* The final Rust image contained no unresolved symbols and no run reported a
  ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.
* ``try_to_wake_up()`` calls ``rust_sched_ttwu_state_match()``.
  ``wait_task_inactive()`` calls ``rust_sched_task_state_match()``. Rust calls
  the current-state and saved-state read helpers and calls the saved-state
  write helper only after a saved-state match.
* The runnable wake path calls ``rust_sched_ttwu_runnable_locked()`` under the
  task runqueue lock. Rust calls the queued, clock, delayed, proxy, on-CPU,
  preemption, and wake helpers in the original order.

The runnable-path unit was validated on 2026-11-12 with x86_64 ``LLVM=1``
builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all five wake and runnable
  cases.
* Three consecutive four-vCPU C fallback runs passed all five applicable cases
  with ``CONFIG_RUST_SCHED_TTWU_RUNNABLE`` disabled.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on both the Rust and C fallback images.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all five wake and runnable cases and all twenty-five hrtimer
  regression cases.
* The no-KUnit production image built with Clippy while both scheduler Rust
  options were enabled.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, or panic.

The activation-path unit was validated on 2026-11-12 with x86_64 ``LLVM=1``
builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all six wake and activation
  cases.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive four-vCPU C fallback runs passed all five applicable cases
  with ``CONFIG_RUST_SCHED_TTWU_ACTIVATE`` disabled.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all six wake and activation cases and all twenty-five hrtimer
  regression cases.
* The no-KUnit production image built with Clippy while all three scheduler
  Rust options were enabled.
* Both ``try_to_wake_up()`` and ``sched_ttwu_pending()`` call
  ``rust_sched_ttwu_activate_locked()``. Rust calls the load, I/O wait,
  activation, preemption, wake, and optional class-callback helpers in the
  original order.
* The production image contained no unresolved symbols. No run reported a
  ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.

The blocking-path unit was validated on 2026-11-12 with x86_64 ``LLVM=1``
builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all eight scheduler cases.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive four-vCPU C fallback runs passed all six applicable cases
  with ``CONFIG_RUST_SCHED_BLOCK_TASK`` disabled.
* A build with only the blocking Rust option enabled passed three consecutive
  scheduler runs with all five applicable cases and three hrtimer runs with
  all twenty-five cases.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all eight scheduler cases and all twenty-five hrtimer cases.
* The no-KUnit production image built with Clippy while all four scheduler
  Rust options were enabled.
* ``__schedule()`` calls ``rust_sched_block_task_locked()``. Rust calls the
  load-flag write, blocking dequeue, and block-accounting helpers in the
  original order. The accounting helper runs only if dequeue succeeds.
* The production image contained no unresolved symbols. No run reported a
  ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* Task current-state or saved-state representation changes.
* ``try_to_wake_up()`` changes the state-match gate or saved-state protocol.
* Debug-preemption wake-mask validation changes.
* Rust gains stable task-state accessors and kernel warning abstractions.
* The runnable path changes its runqueue lock, delayed enqueue, proxy return,
  preemption, or wake-publication ordering.
* The activation path changes load or I/O wait accounting, enqueue flags,
  activation ordering, preemption ordering, wake publication, or the
  scheduling-class callback contract.
* Rust gains stable runqueue state accessors or safe scheduling-class callback
  abstractions.
* Task-state masks, load accounting, special-state classification, or the
  blocking dequeue and accounting order changes.
* Rust gains stable task bitfield access or safe runqueue dequeue and block
  accounting abstractions.
