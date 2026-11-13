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
The ``CONFIG_RUST_SCHED_WAKE_PREEMPT`` option selects Rust for the production
wakeup-preemption state machine.
The ``CONFIG_RUST_SCHED_RESCHED_CURR`` option selects Rust for the production
current-task reschedule request state machine.
The ``CONFIG_RUST_SCHED_RESCHED_CPU`` option selects Rust for the production
per-CPU reschedule request decision.
The ``CONFIG_RUST_SCHED_WAKE_IF_IDLE`` option selects Rust for the production
idle CPU wakeup decision and locked recheck.
The ``CONFIG_RUST_SCHED_SELECT_TASK_RQ`` option selects Rust for the production
task CPU selection routing, scheduling-class callback dispatch, selected-CPU
flag publication, eligibility check, and fallback decision.
The ``CONFIG_RUST_SCHED_RQ_OPS`` option selects Rust for the production
runqueue enqueue and dequeue routing, conditional action order,
scheduling-class callback dispatch, and dequeue result.
The ``CONFIG_RUST_SCHED_CLASS_TRANSITION`` option selects Rust for the
production put-prev and set-next routing, deferred scheduling-server transfer,
same-task short circuit, and callback order.
The ``CONFIG_RUST_SCHED_PREV_BALANCE`` option selects Rust for the production
previous-task balance pass, active scheduling-class traversal, class balance
callback dispatch, and stop decision.
The ``CONFIG_RUST_SCHED_PICK_NEXT_TASK`` option selects Rust for the production
next-task selection path, fair-class fast path, active-class scan, retry
decision, class callback dispatch, and final class transition.
The ``CONFIG_RUST_SCHED_CORE_PICK_TASK`` option selects Rust for the production
core-scheduling task pick, deferred-server clear, active-class traversal,
pick-callback dispatch, and retry-sentinel propagation.
The ``CONFIG_RUST_SCHED_BLOCK_TASK`` option selects Rust for the production
task blocking state machine.
The ``CONFIG_RUST_SCHED_RQ_TRANSITION`` option selects Rust for the production
task activation and deactivation state machines.

Rust owns these production decisions:

* Reading the current and saved task states.
* Matching the requested wake mask against the current task state.
* Falling back to the saved task state when the current state does not match.
* Giving a current-state match precedence over a saved-state match.
* Reporting wake success independently from whether the task can run now.
* Publishing a matched saved state as ``TASK_RUNNING``.
* Checking whether the wake target remains queued after the runqueue lock is
  acquired.
* Updating the runqueue clock through a direct ``update_rq_clock()`` call,
  reading task blocked, on-CPU, and delayed state, deciding whether delayed
  enqueue is necessary, composing its flags, and calling ``enqueue_task()``
  directly.
* Handling proxy state, selecting preemption and calling
  ``wakeup_preempt()`` directly, and publishing the final running state.
* Decoding the selected-runqueue and migrated wake flags.
* Classifying a woken task against the selected scheduling class, calling the
  selected class wakeup-preemption callback, requesting rescheduling, updating
  the selected class, and requesting omission of a redundant clock update.
* Promoting a lazy reschedule request for an idle task, suppressing duplicate
  requests, selecting local or remote delivery, and ordering trace, thread
  flag, preemption count, reschedule IPI, and idle wake trace operations.
* Determining whether a per-CPU reschedule request targets an online or
  current CPU and requesting current-task rescheduling only for either case.
* Checking whether the target runqueue current task is idle, acquiring the
  runqueue lock only after a matching first check, rechecking under the lock,
  and requesting rescheduling only after both checks match.
* Reading the allowed-CPU count and migration-disabled state, choosing between
  the scheduling-class callback path and the single-CPU mask path, calling the
  selected class directly, and setting ``WF_RQ_SELECTED`` only for a class
  callback.
* Validating the selected CPU and requesting fallback placement from the task's
  current CPU when that selection is not allowed.
* Selecting and ordering the runqueue clock update, utilization-clamp change,
  scheduling-class enqueue or dequeue callback, pressure accounting,
  scheduler-statistics change, and core-scheduling change.
* Reading the optional core-scheduling state, invoking the selected scheduling
  class directly, and returning the scheduling-class dequeue result.
* Validating the runqueue donor, transferring the deferred scheduling server,
  short-circuiting an unchanged task, and invoking the put-prev and set-next
  callbacks in order.
* Reading the donor scheduling class, traversing active scheduling classes
  toward the idle class, skipping inactive sched_ext or fair slots, loading
  each optional balance callback, invoking it in class order, and stopping
  when a callback reports runnable work.
* Clearing the deferred scheduling-server pointer, selecting the fair-class
  fast path, invoking the fair and idle pick functions, traversing active
  scheduling classes, restarting after a retry request, invoking class
  ``pick_task`` callbacks in order, and transitioning to the selected task.
* Clearing the core-scheduling deferred server, traversing active scheduling
  classes, invoking each class ``pick_task`` callback in order, returning the
  selected task or retry sentinel, and returning class exhaustion to C.
* Reading task load and I/O wait state, updating the runqueue
  ``nr_uninterruptible`` and ``nr_iowait`` counts, ending delay accounting,
  constructing enqueue flags, calling task activation directly, selecting
  preemption and calling ``wakeup_preempt()`` directly, publishing the
  wakeup, and deciding whether to invoke and directly calling the
  scheduling-class ``task_woken`` callback.
* Classifying whether a blocking task contributes to load, classifying whether
  its state is special, updating its load flag, selecting special dequeue,
  and deciding whether dequeue and block accounting run.
* Reading task runqueue membership, detecting migrating tasks, selecting
  migrated enqueue, rejecting sleeping deactivation, publishing queued and
  migrating membership, and ordering dequeue.

Task-state storage outside the wake-state protocol, warning delivery, task and
runqueue locking, scheduler queue and proxy operations, wake-list handling,
cpumask operations, CPU eligibility queries, fallback placement, task CPU
selection callback implementations, scheduling-class preemption callback
implementations, thread-flag bit mutation, preemption-count updates,
tracepoint delivery, IPI delivery, statistics, and public scheduler ABI
adapters remain in C. Runqueue queue operations, scheduling-class
implementations, wake, select-task-runqueue, put-prev, and set-next callbacks,
and lock pin primitives also remain in C.
Scheduling-class balance implementations, ordering anchors, the scheduling
class size, and sched_ext static-key queries also remain in C.
Scheduling-class pick implementations, class anchors, the class size, the
retry sentinel, generated runqueue field offsets, task transition helpers, and
fatal scheduler invariant reporting also remain in C.
Blocking dequeue and block accounting primitives also remain in C. The KCSAN
runqueue membership writer assertion, warnings, and queue operations also
remain in C.

Entry points and ownership
--------------------------

``kernel/sched/core.c`` keeps the scheduler-facing ``__task_state_match()``
and ``ttwu_state_match()`` adapters. When the Rust option is enabled, the task
state result and wake-state transition come from
``rust/kernel/sched_core.rs``.

The Rust-to-C calls are:

* The invalid debug-preemption wake-mask warning.
* The runqueue clock update and proxy state query used by the runnable path.
  Rust calls ``update_rq_clock()`` directly.
* The delay-accounting completion primitive used by the activation path.
  Rust constructs the enqueue flags, updates ``nr_iowait``, and calls
  ``activate_task()`` directly.
* Scheduling-class wakeup-preemption callback implementations reached through
  an indirect call from Rust.
* Reschedule delivery through ``resched_curr()``.
* The reschedule tracepoints, current-CPU query, local thread-flag write,
  preemption-count update, remote atomic thread-flag update, and reschedule IPI
  delivery used by the current-task reschedule request path.
* The CPU-online and current-CPU queries used by the per-CPU reschedule
  request path.
* The RCU dereference of ``struct rq.curr`` used by the idle CPU wakeup
  first-check path.
* Wakeup tracepoint emission used by both wake paths.
* The ``cpumask_any()``, ``is_cpu_allowed()``, ``task_cpu()``, and
  ``select_fallback_rq()`` operations used by task CPU selection.
* The utilization-clamp, pressure, scheduler-statistics, and core-scheduling
  primitives used by runqueue enqueue and dequeue.
* Scheduling-class put-prev and set-next callback implementations reached
  through indirect calls from Rust.
* The runqueue donor mismatch warning.
* The fair, idle, and optional sched_ext class anchors, the scheduling-class
  size, the sched_ext static-key state, and scheduling-class balance callback
  implementations reached through indirect calls from Rust.
* The highest and lowest scheduling-class anchors, the fair and idle pick
  functions, the retry-task sentinel, the previous-balance adapter, the
  selected-task transition helper, and fatal invariant reporting used by
  next-task selection.
* The highest and lowest scheduling-class anchors, the fair and optional
  sched_ext anchors, the class size, the sched_ext static-key state, the
  retry-task sentinel, and scheduling-class ``pick_task`` implementations used
  by core-scheduling task picking.
* Scheduling-class ``task_woken`` callback implementations reached through
  an indirect call from Rust.
* Runqueue lock unpin and repin around a scheduling-class ``task_woken``
  callback.
* Blocking dequeue with selected sleep and special flags, and block
  accounting.
* The task runqueue membership writer assertion, migrated enqueue constants,
  the invalid sleeping-dequeue warning, and dequeue.

The C-to-Rust production entries are:

* ``rust_sched_task_state_match()``.
* ``rust_sched_ttwu_state_match()``.
* ``rust_sched_ttwu_runnable_locked()``.
* ``rust_sched_ttwu_activate_locked()``.
* ``rust_sched_wakeup_preempt()``.
* ``rust_sched_resched_curr()``.
* ``rust_sched_resched_cpu()``.
* ``rust_sched_wake_if_idle_needs_lock()``.
* ``rust_sched_wake_if_idle_locked()``.
* ``rust_sched_select_task_rq()``.
* ``rust_sched_enqueue_task_locked()``.
* ``rust_sched_dequeue_task_locked()``.
* ``rust_sched_class_put_prev_task()``.
* ``rust_sched_class_set_next_task()``.
* ``rust_sched_class_put_prev_set_next_task()``.
* ``rust_sched_prev_balance()``.
* ``rust_sched_pick_next_task()``.
* ``rust_sched_core_pick_task()``.
* ``rust_sched_block_task_locked()``.
* ``rust_sched_activate_task_locked()``.
* ``rust_sched_deactivate_task_locked()``.

The caller keeps the existing ``p->pi_lock`` serialization, or the current-task
preemption exclusion, around the state-match entries. For the runnable entry,
C acquires the task runqueue lock, calls Rust while the lock is held, and
releases the lock after Rust returns. Rust does not acquire or release
scheduler locks.

For task CPU selection, the caller validates and keeps ``p->pi_lock``. Rust
reads the affinity count, migration-disabled state, affinity mask pointer, and
scheduling-class callback while that lock is held. Rust does not acquire or
release any lock.

For runqueue enqueue and dequeue, C validates that it holds the runqueue lock
and calls Rust while that lock remains held. Rust owns every conditional
decision and the complete helper order. Rust reads the selected scheduling
class callback through a compile-time checked offset and calls it directly.
C retains only the scheduling-class implementation and the narrow accounting
and core-scheduling primitives. Rust returns the scheduling-class dequeue
result unchanged.

For scheduler-class transitions, the caller holds the runqueue lock across
the complete Rust call. Rust validates the donor, transfers
``rq->dl_server`` and the task server pointers, takes the same-task short
circuit, reads the selected put-prev and set-next callbacks, and invokes them
in order. The warning text and scheduling-class callback implementations
remain in C.

For the previous-task balance pass, the caller holds the runqueue lock across
the complete Rust call. Rust reads the donor class, obtains fixed class
anchors and the scheduling-class size from C, traverses active classes by byte
offset, queries the current sched_ext static-key state, invokes each optional
balance callback, and stops at the first non-zero result. The balance callback
implementations remain in C.

For next-task selection, the caller holds the runqueue lock across the
complete Rust call. Rust clears the deferred scheduling-server pointer, checks
the sched_ext state, performs the fair-only fast path, invokes the previous
balance pass on a restart, scans active classes with ``pick_task`` callbacks,
and transitions to the selected task. C supplies the runqueue field offsets,
class anchors, retry sentinel, fair and idle pick implementations, transition
helper, and fatal invariant report.

For core-scheduling task picking, the caller holds the runqueue lock across the
complete Rust call. Rust clears ``rq->dl_server``, reads the fixed class
anchors and class size, traverses active classes, and invokes each class
``pick_task`` callback. Rust returns a selected task, passes ``RETRY_TASK``
unchanged to its caller, or returns null after class exhaustion. C reports the
original fatal idle-class invariant only for the null result.

For the activation entry, C validates that it holds the runqueue lock and
passes the matching lock context to Rust. Rust selects and orders all state
changes and scheduler helpers. Rust reads the optional scheduling-class
callback and invokes it directly. Narrow C helpers unpin and repin the lock
exactly as the original C path did to preserve the callback lock contract.

For the current-task reschedule request entry, C validates that it holds the
runqueue lock and passes the request thread flag to Rust. Rust reads the
current task and runqueue CPU, applies the idle-task lazy-request promotion,
checks for duplicate requests, emits the scheduler trace, and selects local or
remote delivery. Narrow C helpers perform the thread-flag writes, preemption
count update, IPI delivery, and tracepoint emission in the original order.

For the per-CPU reschedule request entry, C maps the CPU to its runqueue and
holds that runqueue lock with interrupts disabled. Rust decides whether the
request is required and calls ``resched_curr()`` only for an online target or
the current CPU. Narrow C helpers perform the CPU-online and current-CPU
queries.

For the idle CPU wakeup entry, C holds the RCU read lock and calls Rust for the
first idle-task check. Rust calls a narrow C helper for the RCU dereference and
checks ``PF_IDLE``. The C caller acquires the runqueue lock with interrupts
disabled only when the first check succeeds. Rust then rechecks the current
task under the runqueue lock and calls ``resched_curr()`` only when both checks
see an idle task.

For the blocking entry, C validates that the task runqueue lock is held and
passes the task state to Rust. Rust classifies the state and calls the bitfield,
dequeue, and accounting helpers in order. C retains ownership of the runqueue
lock across the complete Rust call.

For activation and deactivation, C validates that it holds the runqueue lock
and passes the operation flags to Rust. Rust owns the transition decision and
calls the enqueue and dequeue functions directly through the bindgen-generated
FFI declarations. Rust also reads and writes task runqueue membership through
the kernel atomic API. C retains the lock across the complete Rust call.

Retained C boundary
-------------------

Rust reads and writes the task current state, saved state, blocked marker, and
scheduling delay state through volatile accesses. These accesses preserve the
single load or store behavior of the former C scalar helpers. C defines the
task layout, supplies the task pointer, and retains the serialization around
the wake-state protocol.

``WARN_ON_ONCE`` remains in C because report formatting and rate limiting
depend on kernel diagnostics. Rust decides whether the debug-preemption state
mask is invalid.

``try_to_wake_up()`` keeps its memory barriers, wake-list handling, CPU
selection, migration, full enqueue, statistics, and accounting in C. The
runnable unit changes only the path inside the task runqueue lock.

The runnable path retains runqueue lock acquisition and release, scheduler
queue and proxy operations, scheduling-class preemption callback
implementations, and tracing in C. Rust owns the decision to enqueue a
delayed task, the ``ENQUEUE_NOCLOCK`` and ``ENQUEUE_DELAYED`` flags, and the
direct ``enqueue_task()`` call. C retains the scheduling-class queue
operation reached through that call.

The runqueue clock algorithm remains in C. It combines lockdep validation,
scheduler feature checks, clock-source sampling, ``sched_ext`` clock update,
scheduler-class time accounting, IRQ time accounting, and paravirtual steal
time accounting. Rust calls ``update_rq_clock()`` directly while the task
runqueue lock is held. There is no clock-update wrapper. The C adapter cannot
return while Rust still holds the lock because the lock remains owned by the
C caller.

The per-CPU reschedule path retains CPU-to-runqueue mapping, runqueue lock
acquisition and release, IRQ state save and restore, and the CPU-online and
current-CPU queries in C. Rust owns the eligibility decision and the ordering
of the ``resched_curr()`` call. Rust queries the current CPU only when the
target CPU is offline. This preserves the original short-circuit behavior.
When the current-task reschedule option is also enabled,
``resched_curr()`` enters its Rust implementation through the existing C ABI
adapter.

Rust owns the wakeup-preemption state machine. It reads the donor, current
task, selected class, and clock-update flags from ``struct rq``. It compares
the woken task class with the selected class, calls the selected class
``wakeup_preempt`` operation, calls ``resched_curr()``, updates the selected
class, and requests a redundant clock-update skip in their original order.
C retains runqueue lock ownership, scheduling-class callback
implementations, reschedule delivery, and the public ``wakeup_preempt()``
adapter.

The task CPU selection path retains only the primitive operations that it must
share with the rest of the scheduler. ``cpumask_any()`` chooses from the
affinity mask, ``is_cpu_allowed()`` applies the CPU-hotplug and task-type
policy, ``task_cpu()`` supplies the current CPU to fallback placement, and
``select_fallback_rq()`` performs the existing fallback policy. Rust chooses
when each primitive runs and owns the returned CPU. The scheduling-class
``select_task_rq`` implementations and the public ``select_task_rq()`` adapter
remain in C.

The runqueue-operation path retains the runqueue lock and its assertion,
scheduling-class queue implementations, utilization-clamp accounting,
pressure accounting, scheduler-statistics accounting, core-scheduling
operations, and the public ``enqueue_task()`` and ``dequeue_task()`` adapters
in C. Rust reads the operation flags and core-scheduling state, selects each
conditional operation, invokes each primitive in the original order, calls the
selected scheduling-class callback directly, and returns the class result.

The scheduler-class transition path retains runqueue lock ownership,
scheduling-class callback implementations, and warning delivery in C. Rust
owns the donor comparison, deferred server transfer, same-task early return,
callback lookup, and callback order. The standalone put-prev callback receives
a null next task. A complete transition passes the next task to put-prev and
sets the set-next callback's first-task argument to true.

The previous-task balance path retains the scheduling-class balance
implementations in C. Rust reads the donor class and traverses the
linker-ordered scheduling-class array toward the idle anchor. C supplies the
fair, idle, and optional sched_ext anchors, the class size, and the current
sched_ext static-key state. Rust decides when the fair or sched_ext slot is
inactive, loads each optional balance callback once, and stops at the first
non-zero result.

The next-task selection path retains the scheduling-class ``pick_task``
implementations in C. Rust owns the fair-only condition, the fair and idle
pick order, active-class iteration, retry handling, callback invocation, and
final transition. C supplies the runqueue counters and deferred-server
pointer through compile-time checked offsets. The highest and lowest class
anchors bound the linker-ordered class array.

The core-scheduling task-pick path retains runqueue lock ownership,
scheduling-class pick implementations, class anchors, class size, sched_ext
static-key state, the retry sentinel, generated field offsets, and fatal
invariant reporting in C. Rust owns the deferred-server clear, active-class
iteration, callback lookup and invocation, and result precedence. A non-null
result, including ``RETRY_TASK``, returns immediately. A null result advances
to the next active class.

The activation path retains runqueue lock ownership, scheduling-class queue
operations reached through ``activate_task()``, scheduling-class callback
implementations, tracing, delay accounting, and the lock pin primitives in C.
Rust owns wake-flag decoding, the runqueue ``nr_uninterruptible`` and
``nr_iowait`` updates, the conditions and order for delay-accounting
completion and task activation, enqueue-flag construction, preemption, wake
publication, callback lookup, and callback invocation. Rust keeps the same
unpin-callback-repin order as the C path while retaining ownership of the
state machine.

The blocking path retains runqueue locking, scheduler queue operations,
accounting primitives, tracing, and public scheduler ABI adapters in C. Rust
owns the load and special-state masks, the contributes-to-load bitfield
write, the special dequeue decision, and the order of dequeue and accounting.
The accounting helper runs only after the scheduling class accepts the
dequeue.

The transition path retains the KCSAN writer assertion, warning delivery,
queue operations, tracing, and public scheduler ABI adapters in C. Rust owns
task runqueue membership reads and writes, migrated-state detection, the
condition that adds ``ENQUEUE_MIGRATED``, the condition that warns about
``DEQUEUE_SLEEP``, and the order of membership publication and dequeue. Rust
composes the enqueue flags and calls ``enqueue_task()`` and
``dequeue_task()`` directly. There is no runtime C wrapper for a queue
operation or a runqueue membership state transition. C supplies the
``ASSERT_EXCLUSIVE_WRITER`` mechanism and compile-time checked constants.
The C scheduler asserts that those constants match its private scheduler
definitions.

Rust maps the ``on_rq`` field to a one-byte atomic value. A relaxed atomic load
has the same LKMM semantics as ``READ_ONCE()``, and a relaxed atomic store has
the same LKMM semantics as ``WRITE_ONCE()``. The C writer assertion still runs
after each membership publication.

Rust accesses ``struct rq.nr_uninterruptible`` through a Kbuild-generated
offset. ``kernel/sched/rq-offsets.c`` derives the offset with
``offsetof(struct rq, nr_uninterruptible)``. C checks the field size, and Rust
performs the locked read-modify-write at the generated location.

Rust also accesses ``struct rq.nr_iowait`` through a generated offset. C checks
that the field is an ``atomic_t`` with the size and alignment of an ``int``.
Rust maps the field to ``Atomic<i32>`` and performs a relaxed atomic subtract.
The activation entry receives the task's locked runqueue, so Rust updates that
runqueue rather than resolving ``task_rq()`` again.

Rust reads ``struct sched_class.task_woken`` through a generated offset. C
checks the field size and alignment against a function pointer. Rust loads the
optional callback once and calls it directly with the runqueue and task.

Rust reads ``struct rq.nr_running`` and ``struct rq.cfs.h_nr_queued`` through
generated offsets. C checks both fields are ``unsigned int`` values and checks
their alignment. Rust uses the generated ``struct rq.dl_server`` offset to
clear the deferred-server pointer before the first pick attempt. Rust reads
``struct sched_class.pick_task`` through a generated offset after C checks the
field against a function pointer. The core-scheduling option also checks that
``dl_server`` has pointer size and alignment. Both pick paths use the same
checked callback offset and direct indirect-call ABI.

Rust reads ``struct sched_class.enqueue_task`` and
``struct sched_class.dequeue_task`` through generated offsets. C checks both
field sizes and alignments against function pointers, and Rust converts the
generated byte offsets to callback pointers. It reads each callback once and
calls it directly with the runqueue, task, and original operation flags.

Rust reads ``struct rq.donor``, ``struct rq.curr``, and
``struct rq.next_class`` through generated offsets. C checks the pointer field
sizes and the selected-class offset alignment. Rust compares scheduling-class
pointers with the same address ordering used by ``sched_class_above()``.

Rust reads the ``struct sched_class.wakeup_preempt`` callback through a
generated offset after C checks its size and alignment against a function
pointer. Rust calls the selected class callback directly while the runqueue
lock remains held. The callback implementations and the reschedule delivery
primitive remain in C.

Rust requests the redundant clock-update skip by setting the
``RQCF_REQ_SKIP`` bit in ``struct rq.clock_update_flags``. C checks the flag
value and field size. The runqueue lock serializes this read-modify-write.

Rust publishes a successful wake by clearing ``task_struct.is_blocked`` and
storing ``TASK_RUNNING`` into ``task_struct.__state``. Rust then calls the C
tracepoint helper. This preserves the original blocked-marker, state, and
trace ordering.

Important invariants
--------------------

A current-state match returns positive and wakes the task.

A saved-state-only match returns negative, reports wake success, and clears
the saved state to ``TASK_RUNNING``. The task becomes runnable only when the
lock or freezer restore completes.

No match returns zero and reports wake failure.

When both states match, the current state wins and the saved state is not
changed.

Task CPU selection uses the scheduling-class callback only when more than one
CPU is allowed and migration is not disabled. In that path, the callback
receives the original wake flags. Rust sets ``WF_RQ_SELECTED`` after the
callback returns. The single-CPU and migration-disabled path uses
``cpumask_any()`` and leaves the wake flags unchanged.

After either path, an ineligible selected CPU causes fallback placement from
the task's current CPU. A selected CPU that satisfies ``is_cpu_allowed()`` is
returned unchanged.

Runqueue enqueue updates the clock unless ``ENQUEUE_NOCLOCK`` is present. It
updates utilization clamp before the scheduling-class callback so delayed
task state remains visible. Pressure accounting follows the class callback.
Scheduler statistics run unless ``ENQUEUE_RESTORE`` is present.
Core-scheduling enqueue runs last when enabled.

Runqueue dequeue updates core-scheduling state first. It then updates the
clock unless ``DEQUEUE_NOCLOCK`` is present, runs scheduler statistics unless
``DEQUEUE_SAVE`` is present, updates pressure accounting, and updates
utilization clamp before the scheduling-class callback. The scheduling class
runs last and its boolean result becomes the Rust entry result unchanged.

A scheduler-class transition warns only when the runqueue donor differs from
the previous task. It always clears the previous task's server pointer, moves
the runqueue server pointer to the next task, and clears the runqueue pointer.
When both task pointers match, the transition returns after the server move
without invoking either class callback. Otherwise, put-prev runs before
set-next, and set-next receives the first-task flag.

The previous-task balance pass starts at the donor class and stops before the
idle class. It advances by the C-provided scheduling-class size. It skips the
fair class when sched_ext has switched all fair tasks and skips the sched_ext
class when sched_ext is disabled. A class with no balance callback is skipped.
A non-zero balance result stops the pass before any lower-priority class runs.

Next-task selection clears ``rq->dl_server`` before the first pick attempt.
The fair fast path runs only when sched_ext is disabled, the donor class is
not above fair, and ``nr_running`` equals the fair queue's
``h_nr_queued``. A fair pick retry restarts selection. A successful empty fair
pick falls back to the idle class. The general path invokes the previous
balance pass before each active-class scan. A class retry restarts the general
path, a returned task causes a class transition and return, and an empty class
continues to the next active class. Reaching the lowest class without a task
reports the original fatal idle-class invariant.

Core-scheduling task picking clears ``rq->dl_server`` before the first class
callback. It scans from the highest class toward the lowest class with the
same sched_ext skip rules as the C iterator. A selected task returns
immediately. ``RETRY_TASK`` also returns immediately because the outer
core-scheduling retry loop owns the retry. An empty class continues to the
next active class. Reaching the lowest class without a result returns null so
the C caller reports the original fatal idle-class invariant.

Under ``CONFIG_DEBUG_PREEMPT``, a wake mask that contains
``TASK_RTLOCK_WAIT`` with any other state bit presents an invalid
``TASK_NORMAL`` and must issue the existing warning.

The runnable path first checks that the task is queued. It updates the
runqueue clock only after that check. For a blocked task, it enqueues delayed
scheduler state first, then asks the proxy path whether the task must return.
A proxy return ends the wake attempt without preemption or wake publication.

For a queued task that must stay on this runqueue, Rust requests preemption
only when the task is not currently on CPU. The final action clears the
blocked marker and publishes ``TASK_RUNNING`` before emitting the wakeup
tracepoint while the runqueue lock remains held.

The activation path decrements ``nr_uninterruptible`` before activation when
the task contributes to load. A non-migrated I/O wait task ends delay
accounting and atomically decrements the selected runqueue's ``nr_iowait``
before activation. A migrated task does not perform I/O wait completion. Rust
activates the task before requesting preemption, publishes the wakeup before
the scheduling-class callback, and invokes that callback only when the
scheduling class supplies one. When present, the callback runs between the
runqueue lock unpin and repin operations.

The blocking path marks a task as contributing to load only when its state
contains ``TASK_UNINTERRUPTIBLE`` and lacks ``TASK_NOLOAD`` and
``TASK_FROZEN``. Stopped, traced, parked, dead, waking, and frozen states use
the special dequeue flag even when they do not contribute to load. Rust writes
the load flag before dequeue, requests sleeping dequeue on the current
runqueue, and requests block accounting only when dequeue succeeds.

Activation checks the migrating state before enqueue. A migrating task is
enqueued with ``ENQUEUE_MIGRATED`` and becomes queued only after enqueue
returns. Deactivation warns when ``DEQUEUE_SLEEP`` is present, publishes the
migrating state before dequeue, and preserves that state until a later
activation publishes the queued state.

Waking a task in the selected class calls that class preemption callback. A
task in a higher class also receives a reschedule request, and its class
becomes the selected class. Rust skips the next redundant runqueue clock
update only when the donor remains queued and the current task has the
reschedule flag set.

A per-CPU reschedule request invokes current-task rescheduling when the target
CPU is online or is the current CPU. The current-CPU query is skipped while
the target CPU is online.

An idle CPU wake acquires the target runqueue lock only after the first
``PF_IDLE`` check succeeds. The locked recheck suppresses rescheduling when
the target stopped being idle while the first checker waited for the lock.

Validation gate
---------------

The Rust KUnit cases cover the state-match table, saved-state transition,
activation action selection, activation and delayed-enqueue flags, the
uninterruptible and I/O wait count updates, wakeup-preemption action
selection, per-CPU reschedule eligibility, idle CPU wakeup action selection,
task CPU selection routing, scheduling-class dispatch and flag publication,
single-CPU and migration-disabled behavior, CPU fallback, runqueue enqueue and
dequeue action selection, scheduler-class transition action selection,
previous-task balance and active-class transition action selection,
next-task fast-path and result action selection, core-scheduling pick action
selection, blocking action selection, and a real uninterruptible timeout. A
state-publication case checks the blocked marker and running-state stores.
The transition action table covers migrated enqueue and invalid sleeping
dequeue. The runqueue-operation action tables cover the clock, statistics, and
core-scheduling conditions for both operations. The scheduler-class
transition table covers donor matches, complete transitions, and unchanged
tasks. The previous-task balance table covers absent, non-stopping, and
stopping callbacks. The active-class table covers normal, switched-all,
disabled-sched_ext, and enabled-sched_ext transitions. The next-task tables
cover runnable-count matching, fair retry, empty fair fallback, class retry,
class continuation, and selected-task transition. The core-scheduling action
table covers a selected task, the retry sentinel, and an empty class result.
Real wakeup cases cover current-task rejection, waking a stopped kernel
thread, waking a queued runnable task, waking a ``SCHED_FIFO`` task to invoke
its scheduling-class callback, real activation and deactivation traffic with
both the Rust and C implementations, and an idle CPU wake request against the
current CPU to execute the Rust first-check entry. The same real traffic enters
the Rust runqueue enqueue and dequeue entries. Every real context switch and
scheduler task-selection transition enters the Rust scheduler-class transition
entries. Scheduling attempts that take the general class scan also enter the
Rust previous-task balance pass. Real scheduling traffic enters the Rust
next-task selection entry for every non-core scheduling pick.
On an SMT machine, the core-scheduling path case creates a core-scheduling
cookie and schedules real tasks while the Rust core-pick entry is active.

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

The runqueue-transition unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU Rust runs passed all nine scheduler cases.
* Three consecutive four-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive four-vCPU C fallback runs passed all eight applicable
  cases with only ``CONFIG_RUST_SCHED_RQ_TRANSITION`` disabled.
* Three consecutive four-vCPU runs with only this Rust scheduler option
  enabled passed all four applicable scheduler cases and all twenty-five
  hrtimer cases.
* Three consecutive four-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all nine scheduler cases and all twenty-five hrtimer cases.
* The full Rust image built with Clippy, Rustfmt, and no unresolved symbols.
* Rust calls ``enqueue_task()`` and ``dequeue_task()`` directly through
  bindgen and owns their flags and ordering. The transition path has no
  per-operation C wrapper for a runqueue transition.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, or panic.

The shared task-runqueue-state unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive two-vCPU Rust runs passed all nine scheduler cases.
* Three consecutive two-vCPU hrtimer regression runs passed all twenty-five
  cases on the same image.
* Three consecutive two-vCPU runs with ``CONFIG_RUST_SCHED_RQ_TRANSITION``
  disabled passed all eight applicable scheduler cases.
* Three consecutive two-vCPU runs with
  ``CONFIG_RUST_SCHED_TTWU_RUNNABLE`` disabled passed all four applicable
  scheduler cases.
* Three consecutive two-vCPU runs each on PREEMPT_RT, KASAN, and lockdep
  images passed all nine scheduler cases.
* Every configuration passed three hrtimer regression runs with all
  twenty-five cases.
* The full Rust image built with Clippy, Rustfmt, strict checkpatch, and no
  unresolved symbols.
* Rust reads and writes ``task_struct.on_rq`` through the kernel atomic API.
  The runnable path, activation, and deactivation share these operations.
  The removed C helpers are ``rust_helper_sched_ttwu_rq_queued()``,
  ``rust_helper_sched_rq_on_migrating()``, and
  ``rust_helper_sched_rq_set_state()``.
* C retains one ``ASSERT_EXCLUSIVE_WRITER`` adapter and statically checks the
  scheduler membership and operation-flag constants used by Rust.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, or panic.

The task scalar and scheduler bitfield access unit was validated on 2026-11-13
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 12
     - 25
   * - Runnable fallback
     - 11
     - 25
   * - Activation fallback
     - 10
     - 25
   * - Blocking fallback
     - 9
     - 25
   * - Runnable only
     - 4
     - 25
   * - Activation only
     - 5
     - 25
   * - Blocking only
     - 6
     - 25
   * - PREEMPT_RT
     - 12
     - 25
   * - KASAN
     - 12
     - 25
   * - lockdep
     - 12
     - 25

The full Rust configuration passed Clippy and Rustfmt. Every configuration
produced a production image with no unresolved symbols. No run reported a
``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.

Rust reads ``task_struct.is_blocked`` and ``task_struct.on_cpu`` with volatile
loads. This access keeps the single-read behavior of the former C scalar
helpers. Rust reads and writes ``task_struct.sched_contributes_to_load`` and
reads ``task_struct.in_iowait`` through the bindgen-generated raw bitfield
accessors.

Rust removes these C helpers:

* ``rust_helper_sched_block_set_contributes_load()``.
* ``rust_helper_sched_ttwu_is_blocked()``.
* ``rust_helper_sched_ttwu_on_cpu()``.
* ``rust_helper_sched_ttwu_contributes_load()``.
* ``rust_helper_sched_ttwu_in_iowait()``.

KUnit reads and writes a zeroed task to verify each Rust field access. The
isolated runnable and activation builds exposed a missing conditional import
for ``c_int``. The import now covers every enabled scheduler entry that uses
the C integer ABI.

The wake-state storage and flag-decoding unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 13
     - 25
   * - Wake-state fallback
     - 11
     - 25
   * - Runnable fallback
     - 12
     - 25
   * - Activation fallback
     - 10
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 4
     - 25
   * - Activation only
     - 6
     - 25
   * - PREEMPT_RT
     - 13
     - 25
   * - KASAN
     - 13
     - 25
   * - lockdep
     - 13
     - 25

Rust reads and writes ``task_struct.__state`` and
``task_struct.saved_state`` with volatile accesses. Rust also reads
``task_struct.se.sched_delayed`` with a volatile access. These accesses keep
the single-load or single-store behavior of the former C helpers.

Rust decodes ``WF_RQ_SELECTED`` and ``WF_MIGRATED`` directly. C statically
asserts that the exported flag constants match the scheduler definitions.

Rust removes these C helpers:

* ``rust_helper_sched_task_state()``.
* ``rust_helper_sched_task_saved_state()``.
* ``rust_helper_sched_set_task_saved_state()``.
* ``rust_helper_sched_ttwu_is_delayed()``.
* ``rust_helper_sched_ttwu_rq_selected()``.
* ``rust_helper_sched_ttwu_migrated()``.

The scheduler-class transition unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 33
     - 25
   * - Class transition fallback
     - 32
     - 25
   * - Class transition only
     - 4
     - 25
   * - PREEMPT_RT
     - 33
     - 25
   * - KASAN
     - 33
     - 25
   * - lockdep
     - 33
     - 25

Rustfmt, strict checkpatch, and ``git diff --check`` passed. The no-KUnit
production image built with Clippy. It contained the three production Rust
transition symbols and the C warning helper. It contained no transition KUnit
selector symbols and no unresolved symbols.

Disassembly showed that the production entries compare ``rq->donor``, call the
warning helper only on a mismatch, move the deferred server pointer, and take
the same-task return only after the server move. The complete transition loads
the put-prev callback before the set-next callback. It calls put-prev first
with the next task, then calls set-next with the first-task argument set to
true. The standalone entries pass a null next task to put-prev and false to
set-next.

The production image reached the expected
``VFS: Unable to mount root fs on unknown-block(0,0)`` panic without another
scheduler diagnostic. No run reported a ``BUG``, warning, KASAN report, lock
dependency diagnosis, Oops, or unexpected panic.

The saved-state transition test verifies the direct read and write protocol.
The new KUnit cases verify delayed-state reads, individual wake-flag decoding,
and combined wake-flag decoding.

Every configuration produced a production image with no unresolved symbols.
The no-KUnit image built with Clippy while all scheduler Rust options were
enabled.
No final run reported a ``BUG``, warning, KASAN report, lock dependency
diagnosis, Oops, or panic. One lockdep launch stopped during QEMU boot before
KUnit started. The same image passed immediately on rerun. The final three
lockdep scheduler logs all contain complete 13-case results.

The wake activation enqueue ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 15
     - 25
   * - Wake-state fallback
     - 13
     - 25
   * - Runnable fallback
     - 14
     - 25
   * - Activation fallback
     - 10
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 4
     - 25
   * - Activation only
     - 8
     - 25
   * - PREEMPT_RT
     - 15
     - 25
   * - KASAN
     - 15
     - 25
   * - lockdep
     - 15
     - 25

Rust constructs ``ENQUEUE_WAKEUP``, ``ENQUEUE_NOCLOCK``,
``ENQUEUE_RQ_SELECTED``, and ``ENQUEUE_MIGRATED``. Rust calls
``activate_task()`` directly and that function has no activation wrapper.
Activation-only builds confirm that this path does not depend on another Rust
scheduler option.

Rust updates ``struct rq.nr_uninterruptible`` as an ``unsigned long`` at the
offset generated from the C layout. The KUnit case sets the count to two and
checks the Rust decrement result of one. C statically checks the field size and
all exported enqueue flag values.

Rust removes ``rust_helper_sched_ttwu_dec_uninterruptible()`` and
``rust_helper_sched_ttwu_activate()``. C retains the runqueue lock, the
scheduling-class queue operation reached through ``activate_task()``, I/O wait
delay accounting, preemption, tracing, and the class callback contract.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The all-Rust configuration with KUnit disabled also
passed Clippy. Both production images contained no unresolved symbols. One RT
scheduler launch stopped during QEMU boot before KUnit started. The same image
passed immediately on rerun. No final run reported a ``BUG``, warning, KASAN
report, lock dependency diagnosis, Oops, or panic.

The I/O wait counter ownership unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 16
     - 25
   * - Wake-state fallback
     - 14
     - 25
   * - Runnable fallback
     - 15
     - 25
   * - Activation fallback
     - 10
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 4
     - 25
   * - Activation only
     - 9
     - 25
   * - PREEMPT_RT
     - 16
     - 25
   * - KASAN
     - 16
     - 25
   * - lockdep
     - 16
     - 25

Rust performs the runqueue I/O wait decrement with ``Atomic<i32>`` and relaxed
ordering. The KUnit case initializes the count to two and checks the decrement
result of one. The activation-only build confirms that this path is
independent of the other Rust scheduler options.

Rust removes the compound ``rust_helper_sched_ttwu_io_wait_end()`` helper. The
remaining ``rust_helper_sched_ttwu_delayacct_blkio_end()`` helper only invokes
the delay accounting subsystem. It does not change the runqueue counter.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The all-Rust configuration with KUnit disabled also
passed Clippy. Both production images contained no unresolved symbols. No run
reported a ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or
panic.

The scheduling-class wake callback unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 17
     - 25
   * - Wake-state fallback
     - 15
     - 25
   * - Runnable fallback
     - 16
     - 25
   * - Activation fallback
     - 10
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 4
     - 25
   * - Activation only
     - 10
     - 25
   * - PREEMPT_RT
     - 17
     - 25
   * - KASAN
     - 17
     - 25
   * - lockdep
     - 17
     - 25

Rust loads the optional ``task_woken`` callback through the generated
``struct sched_class`` offset and invokes it directly. The callback lock
contract remains unpin, callback, repin. The new KUnit wakes a real
``SCHED_FIFO`` task, which invokes the RT scheduling-class callback.

Rust removes ``rust_helper_sched_ttwu_has_task_woken()`` and
``rust_helper_sched_ttwu_task_woken()``. There is no C callback dispatch
wrapper. C retains the callback implementations and two narrow runqueue lock
pin helpers.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The all-Rust configuration with KUnit disabled also
passed Clippy. Both production images contained no unresolved symbols. No run
reported a ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or
panic.

The wake-state publication unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 18
     - 25
   * - Wake-state fallback
     - 16
     - 25
   * - Runnable fallback
     - 17
     - 25
   * - Activation fallback
     - 11
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 5
     - 25
   * - Activation only
     - 11
     - 25
   * - PREEMPT_RT
     - 18
     - 25
   * - KASAN
     - 18
     - 25
   * - lockdep
     - 18
     - 25

Rust clears ``task_struct.is_blocked`` and stores ``TASK_RUNNING`` into
``task_struct.__state`` with volatile stores. The runnable and activation wake
paths share this publication function, so both paths preserve blocked marker,
running state, and tracepoint ordering.

KUnit invokes the same publication function through a test selector. The C
helper after publication only emits ``trace_sched_wakeup()``. It does not
publish wake state, and no empty callback dispatch wrapper remains in C.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit configuration passed Clippy with all five
scheduler Rust options enabled. Its production image contained no unresolved
symbols and no KUnit selector symbol. No run reported a ``BUG``, warning, KASAN
report, lock dependency diagnosis, Oops, or panic.

The delayed-enqueue ownership unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 19
     - 25
   * - Wake-state fallback
     - 17
     - 25
   * - Runnable fallback
     - 17
     - 25
   * - Activation fallback
     - 12
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 6
     - 25
   * - Activation only
     - 11
     - 25
   * - PREEMPT_RT
     - 19
     - 25
   * - KASAN
     - 19
     - 25
   * - lockdep
     - 19
     - 25

Rust owns the decision to enqueue a delayed runnable task, constructs
``ENQUEUE_NOCLOCK | ENQUEUE_DELAYED``, and calls ``enqueue_task()`` directly.
C removes ``rust_helper_sched_ttwu_enqueue_delayed()``. The delayed-enqueue C
helper had no state-machine work beyond composing the flags and calling the
scheduler queue function.

C statically checks the exported no-clock and delayed flag values. A KUnit
selector checks the composed flags. The runnable-only configuration passes all
six applicable scheduler cases and does not depend on another Rust scheduler
option.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit configuration passed Clippy with all five
scheduler Rust options enabled. Its production image contained no unresolved
symbols, no delayed-enqueue helper symbol, and no KUnit selector symbol. No run
reported a ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or
panic.

The direct runqueue-clock call unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 19
     - 25
   * - Wake-state fallback
     - 17
     - 25
   * - Runnable fallback
     - 17
     - 25
   * - Activation fallback
     - 12
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 6
     - 25
   * - Activation only
     - 11
     - 25
   * - PREEMPT_RT
     - 19
     - 25
   * - KASAN
     - 19
     - 25
   * - lockdep
     - 19
     - 25

Rust calls ``update_rq_clock()`` directly under the task runqueue lock. C
removes ``rust_helper_sched_ttwu_update_clock()``, which only forwarded to the
clock implementation. The real runnable wake KUnit cases exercise the direct
call.

The clock algorithm remains a reviewed C boundary because it coordinates
lockdep state, scheduler feature checks, clock-source sampling, ``sched_ext``
clock state, scheduler-class time accounting, IRQ time accounting, and
paravirtual steal-time accounting. Reopen this boundary when those contracts
or the clock-update ordering change.

The direct preemption call unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 19
     - 25
   * - Wake-state fallback
     - 17
     - 25
   * - Runnable fallback
     - 17
     - 25
   * - Activation fallback
     - 12
     - 25
   * - Wake-state only
     - 5
     - 25
   * - Runnable only
     - 6
     - 25
   * - Activation only
     - 11
     - 25
   * - PREEMPT_RT
     - 19
     - 25
   * - KASAN
     - 19
     - 25
   * - lockdep
     - 19
     - 25

Rust calls ``wakeup_preempt()`` directly from both wake paths under the task
runqueue lock. C removes ``rust_helper_sched_ttwu_preempt()``, which only
forwarded to the preemption implementation. The real runnable and activation
wake KUnit cases exercise the direct call.

The preemption algorithm remained in C for this direct-call unit. The next
unit moves that algorithm into Rust and retains only the callback
implementations and reschedule delivery in C.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit production image built with Clippy and all
five Rust scheduler options enabled. It contained no unresolved symbols and
no preemption helper symbol. No run reported a ``BUG``, warning, KASAN report,
lock dependency diagnosis, Oops, or panic.

The wakeup-preemption ownership unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 20
     - 25
   * - Wake-preemption fallback
     - 19
     - 25
   * - Wake-preemption only
     - 4
     - 25
   * - PREEMPT_RT
     - 20
     - 25
   * - KASAN
     - 20
     - 25
   * - lockdep
     - 20
     - 25

Rust owns the class comparison, selected-class callback call,
``resched_curr()`` ordering, selected-class update, and redundant clock-update
skip. It reads the runqueue donor, current task, selected class, and clock
flags through generated offsets. C checks the field sizes and callback
alignment, and retains the scheduling-class callback implementations and
reschedule delivery.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit production image built with Clippy and all
six Rust scheduler options enabled. Its symbol table contained the Rust entry
point and the C callback and reschedule boundaries. No run reported a
``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or panic.

The current-task reschedule request ownership unit was validated on
2026-11-13 with x86_64 ``LLVM=1`` builds and QEMU 11.1.3. The full Rust,
KASAN, and lockdep configurations each passed three consecutive four-vCPU
runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 23
     - 25
   * - KASAN
     - 23
     - 25
   * - lockdep
     - 23
     - 25

The PREEMPT_RT image passed all 23 scheduler cases and all 25 hrtimer cases.
The C fallback passed all 20 applicable scheduler cases. A configuration with
only this Rust scheduler option enabled passed all six applicable scheduler
cases. Every configuration passed all 25 hrtimer cases.

Rust owns the idle-task lazy-request promotion, duplicate-request check,
local or remote selection, and the ordering of tracing, thread-flag writes,
preemption-count updates, reschedule IPI delivery, and idle wake tracing. C
checks the runqueue current and CPU field sizes and alignment, and retains the
tracepoints, current-CPU query, thread-flag bit operations, atomic remote
set, preemption-count update, and IPI delivery. Rust reads the task and
runqueue fields through bindgen and generated offsets.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit production image contained the Rust entry
point and all narrow C boundaries and contained no KUnit selector symbol or
unresolved symbol. No run reported a ``BUG``, warning, KASAN report, lock
dependency diagnosis, Oops, or panic.

The per-CPU reschedule request ownership unit was validated on 2026-11-13
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed
three consecutive four-vCPU runs:

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 24
     - 25
   * - C fallback
     - 23
     - 25
   * - Reschedule-CPU only
     - 4
     - 25
   * - PREEMPT_RT
     - 24
     - 25
   * - KASAN
     - 24
     - 25
   * - lockdep
     - 24
     - 25

Rust owns the online-or-current decision and the conditional
``resched_curr()`` call. C retains CPU-to-runqueue mapping, runqueue locking,
IRQ state save and restore, the CPU-online and current-CPU queries, and the
public scheduler ABI adapter. Rust preserves the short-circuit by querying the
current CPU only after an online check fails. The full configuration passes
the request through the Rust current-task reschedule implementation.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit production image built with Clippy and all
eight Rust scheduler options enabled. Its symbol table contained the Rust
entry point and both C primitive helpers, and it contained no KUnit selector
symbol or unresolved symbol. No run reported a ``BUG``, warning, KASAN report,
lock dependency diagnosis, Oops, or panic.

The idle CPU wakeup ownership unit was validated on 2026-11-13 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three consecutive
four-vCPU runs for the applicable scheduler and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 26
     - 25
   * - C fallback
     - 24
     - 25
   * - Idle-wakeup only
     - 5
     - 25
   * - PREEMPT_RT
     - 26
     - 25
   * - KASAN
     - 26
     - 25
   * - lockdep
     - 26
     - 25

Rust owns the first idle check, the decision to acquire the runqueue lock,
the locked recheck, and the conditional ``resched_curr()`` call. C retains the
public ``wake_up_if_idle()`` entry, RCU read protection, the RCU dereference of
``struct rq.curr``, runqueue lock and IRQ-state ownership, and the reschedule
delivery ABI.

The action table covers a non-idle first check, a successful first check with
a non-idle locked recheck, and a successful first check with an idle locked
recheck. A real four-vCPU case calls ``wake_up_if_idle()`` for the current CPU
and executes the Rust first-check entry. The idle-wakeup-only build confirms
that this path does not depend on another Rust scheduler option.

One initial lockdep hrtimer run emitted a transient warning from
``rust_hrtimer_callback_exit()`` after scheduler case 23. The trace entered
only the hrtimer interrupt path. Three immediate reruns and twenty additional
lockdep hrtimer runs passed without warnings. A ten-run control on the
previous ``rs`` lockdep image also passed without warnings. Continue to track
the hrtimer callback-running warning as a separate timing-sensitive issue.

The KASAN test configuration used ``CONFIG_FRAME_WARN=4096`` because an
unrelated ``fs/coredump.o`` call frame exceeded the default 2048-byte limit.
The changed scheduler and Rust objects built without KASAN diagnostics.

The scheduler task CPU selection routing unit was validated on 2026-11-13
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs for the applicable scheduler and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 30
     - 25
   * - C fallback
     - 26
     - 25
   * - Task-selection only
     - 7
     - 25
   * - PREEMPT_RT
     - 30
     - 25
   * - KASAN
     - 30
     - 25
   * - lockdep
     - 30
     - 25

``select_task_rq()`` calls ``rust_sched_select_task_rq()`` when the option is
enabled. Rust reads the allowed-CPU count and migration-disabled state before
it loads the scheduling-class callback. The callback is loaded only on the
class path and receives the original wake flags. Rust sets
``WF_RQ_SELECTED`` only after that callback returns.

The single-CPU and migration-disabled path calls ``cpumask_any()`` and keeps
the wake flags unchanged. Both paths check ``is_cpu_allowed()``. An ineligible
CPU enters ``select_fallback_rq()`` with the task's current CPU. KUnit covers
the routing table, class dispatch, flag publication, both fast-path
conditions, and a valid but disallowed callback result. The full Rust
configuration also wakes a real ``SCHED_FIFO`` kthread, which exercises the
production callback load and RT scheduling-class selection.

C retains the four primitive adapters ``cpumask_any()``,
``is_cpu_allowed()``, ``task_cpu()``, and ``select_fallback_rq()``. It also
retains scheduling-class callback implementations and the public
``select_task_rq()`` adapter. Rust owns path selection, callback dispatch,
flag ordering, validity checking, fallback decision, and the returned CPU.

The no-KUnit production configuration enabled all ten scheduler Rust options
and built the image with ``CLIPPY=1``. Its symbol table contains the Rust
entry point and the four narrow C helpers. It contains no task-selection KUnit
selector symbol and no unresolved Rust scheduler symbol. Rustfmt, strict
checkpatch, and ``git diff --check`` passed.

Some scheduler and hrtimer runs emitted a transient clocksource watchdog
timeout message while the host was loaded. Every affected KUnit suite still
reported all expected cases as passing. No run reported a ``BUG``, KASAN
report, lock dependency diagnosis, Oops, or panic. Continue to track the
clocksource timeout as a separate timing-sensitive infrastructure issue.

The runqueue operation ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs for the applicable scheduler and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 32
     - 25
   * - C fallback
     - 30
     - 25
   * - RQ-operations only
     - 5
     - 25
   * - PREEMPT_RT
     - 32
     - 25
   * - KASAN
     - 32
     - 25
   * - lockdep
     - 32
     - 25

The full image contains direct C-to-Rust jumps from ``enqueue_task()`` and
``dequeue_task()`` to their Rust entries. Disassembly of the Rust entries
shows the original helper order: clock, utilization clamp, class callback,
pressure, statistics, and core scheduling for enqueue, then core scheduling,
clock, statistics, pressure, utilization clamp, and the class callback for
dequeue. The class callback is an indirect call loaded once from the selected
scheduling-class pointer.

The isolated RQ-operations build initially exposed a missing bindgen
declaration for ``update_rq_clock()``. The scheduler Rust header now declares
that boundary whenever either the runnable option or the runqueue-operation
option is enabled.

The no-KUnit production configuration enabled all eleven scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains both Rust
entries and all ten narrow C helpers. It contains no RQ-operation KUnit
selector and no unresolved Rust scheduler symbol. The production image booted
to the expected missing-root-filesystem panic without a scheduler ``BUG``,
warning, KASAN report, lock dependency diagnosis, Oops, or unexpected panic.
Rustfmt, strict checkpatch, and ``git diff --check`` passed.

The previous-task balance ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs for the applicable scheduler and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 35
     - 25
   * - C balance fallback
     - 33
     - 25
   * - Previous-balance only
     - 5
     - 25
   * - PREEMPT_RT
     - 35
     - 25
   * - KASAN
     - 35
     - 25
   * - lockdep
     - 35
     - 25

The previous-task balance action table covers absent, non-stopping, and
stopping callbacks. The active-class action table covers a regular transition,
the switched-all fair-class skip, the disabled sched_ext-class skip, and an
enabled sched_ext transition.

The no-KUnit production configuration enabled all thirteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains
``rust_sched_prev_balance()`` and the fair-class, idle-class,
sched_ext-class, class-size, and sched_ext-state C helpers. It contains
neither previous-balance KUnit selector and no unresolved symbol. Disassembly
shows the donor-class load, the C-provided byte stride, the idle-class
termination check, the optional callback load, the indirect call, and the
non-zero stop branch. The production image booted to the expected
missing-root-filesystem panic without another scheduler diagnostic.

An additional production image enabled ``SCHED_CLASS_EXT`` with BPF JIT and
BTF. Its symbol table contains the real ``ext_sched_class`` anchor and no
unresolved symbol. The image booted to the expected panic without another
scheduler diagnostic. The sched_ext static key is disabled during this boot,
so active-class traversal must skip the real sched_ext slot and continue to
the idle anchor.

The next-task selection ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs for the applicable scheduler and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 37
     - 25
   * - C pick fallback
     - 35
     - 25
   * - Next-task only
     - 6
     - 25
   * - PREEMPT_RT
     - 37
     - 25
   * - KASAN
     - 37
     - 25
   * - lockdep
     - 37
     - 25
   * - sched_ext and BTF
     - 37
     - 25

The next-task action tables cover runnable-count matching, fair retry,
empty fair fallback, class retry, class continuation, and selected-task
transition. Real scheduling traffic enters the Rust next-task selection entry
for every non-core scheduling pick.

The no-KUnit production configuration enabled all fourteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains
``rust_sched_pick_next_task()``, the fair and idle pick helpers, the retry
sentinel, the previous-balance adapter, the transition helper, the highest and
lowest class anchors, and the class-size, sched_ext-class, and sched_ext-state
helpers. It contains none of the three next-task KUnit selectors and no
unresolved symbol.

Disassembly shows the deferred-server clear, sched_ext gate, donor-class and
fair-class comparison, runnable-count check, fair and idle fallback, previous
balance call, indirect class callback scan, retry loop, final transition, and
the null return used by C for the fatal idle-class invariant. The production
image booted to the expected missing-root-filesystem panic without another
scheduler ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or
unexpected panic. Rustfmt, strict checkpatch, and ``git diff --check`` passed.

The core-scheduling task-pick ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration used a real SMT
topology with four vCPUs, two threads per core, two cores, and one socket.
Each configuration passed three consecutive runs for the applicable scheduler
and hrtimer suites.

.. list-table::
   :header-rows: 1
   :widths: 45 25 25

   * - Configuration
     - Scheduler cases
     - hrtimer cases
   * - Full Rust
     - 39
     - 25
   * - C core-pick fallback
     - 38
     - 25
   * - Core-pick only
     - 6
     - 25
   * - PREEMPT_RT
     - 39
     - 25
   * - KASAN
     - 39
     - 25
   * - lockdep
     - 39
     - 25
   * - sched_ext and BTF
     - 39
     - 25

The real-path case checks that SMT is active, creates a core-scheduling cookie
with ``PR_SCHED_CORE_CREATE``, and schedules a one-millisecond timeout. The
full Rust, C fallback, and core-pick-only images all execute this case and all
three runs pass. The core-pick-only image passes six applicable scheduler
cases and all twenty-five hrtimer cases.

The no-KUnit production configuration enabled all fifteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains
``rust_sched_core_pick_task()``, the highest and lowest class anchors, the fair
and sched_ext anchors, the class-size, sched_ext-state, and retry-sentinel
helpers. It contains neither the core-pick KUnit selector nor an unresolved
symbol.

Disassembly shows the deferred-server clear, highest-to-lowest class scan,
optional sched_ext slot skip, indirect ``pick_task`` callback, retry-sentinel
comparison, selected-task return, and null return used by C for the fatal
idle-class invariant. The production image booted on the same SMT topology to
the expected missing-root-filesystem panic without another scheduler ``BUG``,
warning, KASAN report, lock dependency diagnosis, Oops, or unexpected panic.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* Task current-state or saved-state representation changes.
* ``try_to_wake_up()`` changes the state-match gate or saved-state protocol.
* Debug-preemption wake-mask validation changes.
* The selected-runqueue or migrated wake-flag values or meanings change.
* Rust gains safe task-state accessors and kernel warning abstractions.
* The runqueue clock algorithm, lock contract, update ordering, or function
  declaration changes.
* The ``wakeup_preempt()`` algorithm, scheduling-class interface, lock
  contract, or preemption ordering changes.
* The runqueue donor, current, selected-class, or clock-flag layout, the
  wakeup-preemption callback ABI, or the reschedule primitive changes.
* The idle-task lazy-request promotion, duplicate-request test, local and
  remote selection, or reschedule trace, thread-flag, preemption-count, IPI,
  and idle wake trace ordering changes.
* The current task, runqueue CPU, task flags, or thread-info flags layout
  changes, or Rust cannot preserve the lock and volatile-access contracts.
* The reschedule helper ABI, runqueue lock requirement, or
  ``set_nr_and_not_polling()`` return contract changes.
* Rust gains safe thread-flag, preemption-count, tracepoint, or IPI
  abstractions that can replace the narrow C boundaries.
* CPU-online state, current CPU identity, CPU-to-runqueue mapping, or the
  per-CPU reschedule lock contract changes.
* The ``resched_curr()`` declaration, ABI, call ordering, or conditional
  requirement changes.
* The RCU protection, current-task dereference, ``PF_IDLE`` representation,
  initial idle check, runqueue lock and IRQ-state ownership, locked recheck,
  or conditional reschedule ordering changes.
* Rust gains safe CPU-state, runqueue-lock, or reschedule-delivery
  abstractions that can replace the narrow C boundaries.
* The task allowed-CPU count, affinity mask pointer, migration-disabled state,
  scheduling-class pointer, or ``select_task_rq`` callback layout and ABI
  changes.
* The scheduling-class selection condition, wake-flag ordering, CPU
  eligibility policy, or fallback placement policy changes.
* Rust gains safe affinity, CPU-state, task-runqueue, or scheduling-class
  callback abstractions that can replace the narrow C boundaries.
* The enqueue or dequeue operation-flag values, their meanings, or the
  conditional operation order changes.
* The runqueue clock, utilization-clamp, scheduling-class, pressure,
  scheduler-statistics, or core-scheduling contract changes.
* The ``sched_class.enqueue_task`` or ``sched_class.dequeue_task`` layout,
  callback signature, indirect-call ABI, or dequeue return meaning changes.
* Rust gains safe scheduling-class callback, runqueue operation, accounting,
  or core-scheduling abstractions that can replace the narrow C boundaries.
* The task or runqueue deferred-server pointer layout, donor invariant,
  same-task behavior, or server transfer order changes.
* The ``sched_class.put_prev_task`` or ``sched_class.set_next_task`` layout,
  callback signature, indirect-call ABI, or first-task meaning changes.
* The warning threshold or report mechanism changes, or Rust gains a safe
  kernel warning abstraction.
* Rust gains safe scheduling-class transition callback or deferred-server
  abstractions that can replace the narrow C boundaries.
* The scheduler-class linker order, class size, fair, idle, or sched_ext
  anchors, sched_ext static-key meanings, or active-class skip rules change.
* The ``sched_class.balance`` layout, callback signature, indirect-call ABI,
  or non-zero result meaning changes.
* Rust gains safe scheduling-class iteration, static-key, or balance callback
  abstractions that can replace the narrow C boundaries.
* The runqueue ``nr_running``, fair ``h_nr_queued``, donor, or deferred-server
  field layout changes, or the fair fast-path condition changes.
* The highest or lowest class anchors, retry sentinel, fair or idle pick
  implementation, class order, or fatal idle-class invariant changes.
* The ``sched_class.pick_task`` layout, callback signature, or indirect-call
  ABI changes.
* Rust gains safe runqueue counters, active-class iteration, pick callbacks,
  or fatal invariant abstractions that can replace the narrow C boundaries.
* The deferred-server clear order, highest-to-lowest core-pick scan, retry
  sentinel meaning, active-class skip rules, or fatal idle-class invariant
  changes.
* The core-scheduling ``pick_task`` callback contract or runqueue lock
  ownership changes.
* Rust gains safe core-scheduling active-class iteration or fatal invariant
  abstractions that can replace the narrow C boundaries.
* The runnable path changes its runqueue lock, delayed-enqueue condition,
  enqueue flags, direct enqueue call, proxy return, preemption, or
  wake-publication ordering.
* The activation path changes load or I/O wait accounting, enqueue flags,
  activation ordering, preemption ordering, wake publication, or the
  scheduling-class callback contract.
* The blocked-marker or current-state layout, store semantics, or wake
  publication and trace ordering changes.
* The scheduling-class ``task_woken`` field layout, callback signature,
  indirect-call ABI, or lock pin contract changes.
* The runqueue ``nr_uninterruptible`` layout, range, or lock serialization
  changes.
* The runqueue ``nr_iowait`` layout, atomic type, ordering, or update
  serialization changes.
* Activation stops passing the task's locked runqueue to Rust.
* Rust gains stable runqueue state accessors or safe scheduling-class callback
  abstractions.
* Task-state masks, load accounting, special-state classification, or the
  blocking dequeue and accounting order changes.
* Task scalar or scheduler bitfield layout changes.
* The compiler access or instrumentation semantics for volatile task-state
  accesses change.
* The runqueue-lock serialization assumptions for task blocked state, on-CPU
  state, load state, or I/O wait state change.
* Rust gains safe runqueue dequeue and block accounting abstractions.
* Runqueue membership states, migration enqueue flags, sleeping-dequeue
  validation, or activation and deactivation ordering changes.
* The kernel atomic API, ``task_struct.on_rq`` layout, or KCSAN writer
  assertion changes.
* Rust gains safe enqueue and dequeue abstractions.
