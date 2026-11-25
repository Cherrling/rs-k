.. SPDX-License-Identifier: GPL-2.0

Native Rust Scheduler Core Paths
================================

Status and scope
----------------

The ``CONFIG_RUST_SCHED_WAKE_Q`` option selects direct Rust definitions of
``wake_q_add()``, ``wake_q_add_safe()``, and ``wake_up_q()``. Rust owns queue
membership, duplicate insertion, task-reference acquisition and transfer,
queue traversal, node clearing, and wake order.
The ``CONFIG_RUST_SCHED_WAKE_API`` option selects direct Rust definitions of
``wake_up_process()``, ``wake_up_state()``, and ``default_wake_function()``.
Rust owns normal-state mask selection, caller state-mask forwarding, wait-queue
callback validation, warning selection, and wake-result propagation.
The ``CONFIG_RUST_SCHED_WAKE_STATE`` option selects native Rust definitions of
``task_state_match()`` and ``ttwu_state_match()`` for task wakeups and
inactive-task waits.
The ``CONFIG_RUST_SCHED_TRY_WAKEUP`` option selects a direct Rust definition
of ``try_to_wake_up()``. Rust owns the complete preemption-disabled wakeup
entry, including its current-task and pi-lock paths, retry decision,
runnable-wake short circuit, on-CPU and wake-list decision, migration policy,
final queue decision, success accounting, and lock release order.
The ``CONFIG_RUST_SCHED_TTWU_RUNNABLE`` option selects a native Rust
definition of ``ttwu_runnable()``. Rust acquires and releases the task
runqueue lock and owns the complete queued-wake path.
The ``CONFIG_RUST_SCHED_TTWU_ACTIVATE`` option selects a native Rust definition
of ``ttwu_do_activate()``.
The ``CONFIG_RUST_SCHED_WAKE_PREEMPT`` option selects a native Rust definition
of ``wakeup_preempt()``.
The ``CONFIG_RUST_SCHED_RESCHED_CURR`` option selects direct Rust definitions
of ``resched_curr()`` and ``resched_curr_lazy()``. The
``CONFIG_RUST_SCHED_RESCHED_CPU`` option selects a direct Rust definition of
``resched_cpu()``.
The ``CONFIG_RUST_SCHED_WAKE_IF_IDLE`` option selects a direct Rust definition
of ``wake_up_if_idle()``. Rust owns the idle CPU wakeup decision, lock ordering,
and locked recheck.
The ``CONFIG_RUST_SCHED_SELECT_TASK_RQ`` option selects a direct Rust
definition of ``select_task_rq()``. Rust owns task CPU selection routing,
scheduling-class callback dispatch, selected-CPU flag publication, eligibility
check, and the fallback decision.
The ``CONFIG_RUST_SCHED_RQ_OPS`` option selects direct Rust definitions of
``enqueue_task()`` and ``dequeue_task()``. Rust owns the conditional action
order, scheduling-class callback dispatch, and dequeue result.
The ``CONFIG_RUST_SCHED_CLASS_TRANSITION`` option selects direct Rust
definitions of ``put_prev_task()``, ``set_next_task()``, and
``put_prev_set_next_task()``. Rust owns the donor check, deferred
scheduling-server transfer, same-task short circuit, callback lookup, and
callback order.
The ``CONFIG_RUST_SCHED_PREV_BALANCE`` option selects a direct Rust definition
of ``prev_balance()``. Rust owns active scheduling-class traversal, class
balance callback dispatch and ordering, sched_ext skip decisions, and the stop
decision. The Rust next-task selection path calls the same balance state
machine directly.
The ``CONFIG_RUST_SCHED_PICK_NEXT_TASK`` option selects Rust for the production
next-task selection path, fair-class fast path, active-class scan, retry
decision, class callback dispatch, and final class transition.
The ``CONFIG_RUST_SCHED_CORE_PICK_TASK`` option selects a direct Rust
definition of ``pick_task()``. Rust owns the deferred-server clear,
active-class traversal, pick-callback dispatch, retry-sentinel propagation,
and fatal idle-class invariant. The native C implementation is compiled out.
The ``CONFIG_RUST_SCHED_BLOCK_TASK`` option selects a native Rust definition of
``block_task()``.
The ``CONFIG_RUST_SCHED_RQ_TRANSITION`` option selects direct Rust definitions
of ``activate_task()`` and ``deactivate_task()``. Rust owns the activation and
deactivation state machines.

Rust owns these production decisions:

* Reading the current and saved task states.
* Matching the requested wake mask against the current task state.
* Falling back to the saved task state when the current state does not match.
* Giving a current-state match precedence over a saved-state match.
* Reporting wake success independently from whether the task can run now.
* Publishing a matched saved state as ``TASK_RUNNING``.
* Claiming a task's embedded wake-queue node exactly once after the required
  pre-atomic barrier and appending it to the context-local queue.
* Acquiring one task reference for an ordinary insertion and transferring a
  caller-owned reference for a safe insertion. A failed safe insertion
  releases that caller reference immediately.
* Traversing queued tasks in insertion order, clearing each embedded node
  before waking its task, and releasing the queue-owned task reference after
  each wake.
* Defining the direct process, caller-state-mask, and default wait-queue wake
  entries and selecting the normal task-state mask.
* Validating default wait-queue wake flags, requesting the exact original
  invalid-flag warning, forwarding the wait entry's private task, and
  propagating the task wakeup result.
* Calling the Rust ``wake_up_process()`` entry directly from ``wake_up_q()``
  when the direct wake option is enabled.
* Defining the complete task wakeup entry and owning its preemption-disabled
  lifetime, wake-flag decoration, and current-task fast path.
* Acquiring and releasing the task pi lock for a non-current target, ordering
  the state-match and runqueue-membership reads, and selecting the runnable
  wake, on-CPU wait, or final queue path.
* Publishing ``TASK_WAKING`` before the on-CPU and wake-list decision,
  selecting the task CPU after the previous CPU releases the task, applying
  migration and I/O wait policy, and ordering the final queue and success
  accounting.
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
* Defining the public runqueue enqueue and dequeue entries, and selecting and
  ordering the runqueue clock update, utilization-clamp change,
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
* Defining the production core-scheduling ``pick_task()`` entry, clearing the
  deferred server, traversing active scheduling classes, invoking each class
  callback in order, returning the selected task or retry sentinel, and
  reporting the fatal idle-class invariant after class exhaustion.
* Reading task load and I/O wait state, updating the runqueue
  ``nr_uninterruptible`` and ``nr_iowait`` counts, ending delay accounting,
  constructing enqueue flags, calling task activation directly, selecting
  preemption and calling ``wakeup_preempt()`` directly, publishing the
  wakeup, and deciding whether to invoke and directly calling the
  scheduling-class ``task_woken`` callback.
* Classifying whether a blocking task contributes to load, classifying whether
  its state is special, updating its load flag, selecting special dequeue,
  and deciding whether dequeue and block accounting run.
* Defining the public task activation and deactivation entries, reading task
  runqueue membership, detecting migrating tasks, selecting migrated enqueue,
  rejecting sleeping deactivation, publishing queued and migrating
  membership, and ordering dequeue.

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
class size, sched_ext static-key queries, and the previous-task balance
fallback also remain in C.
Scheduling-class pick implementations, class anchors, the class size, the
retry sentinel, generated runqueue field offsets, task transition helpers, and
fatal scheduler invariant reporting also remain in C.
Blocking dequeue and block accounting primitives also remain in C. The KCSAN
runqueue membership writer assertion, warnings, and queue operations also
remain in C.
The wake-queue and wake-node layouts, tail sentinel, compare-exchange and
pre-atomic memory barrier, node-to-task conversion, and task reference
operations remain in C. The normal wake-state mask, supported default
callback flags, wait-queue layout, warning formatting, ordinary export
metadata, and ``try_to_wake_up()`` fallback also remain in C.

Entry points and ownership
--------------------------

``rust/kernel/sched_core.rs`` directly defines ``wake_q_add()``,
``wake_q_add_safe()``, ``wake_up_q()``, ``wake_up_process()``,
``wake_up_state()``, ``default_wake_function()``, ``task_state_match()``,
``ttwu_state_match()``, ``try_to_wake_up()``, ``ttwu_runnable()``,
``ttwu_do_activate()``, ``block_task()``, and ``wakeup_preempt()``. Each
corresponding C fallback is compiled only when its Rust option is disabled.

The Rust-to-C calls are:

* The wake-node compare-exchange and pre-atomic barrier, node-to-task
  conversion, and task reference acquisition and release used by deferred
  wake queues.
* The normal wake-state and supported wake-flag constants, invalid default
  callback warning delivery, and the task wakeup implementation or C fallback
  used by the direct wake entries.
* The invalid debug-preemption wake-mask warning.
* The task pi-lock acquisition and release used by ``task_state_match()``.
* The task pi-lock acquisition and release, preemption-count changes, memory
  barriers, conditional on-CPU wait, current-task blocked-state preamble,
  wake-list attempt, final runqueue queue, PSI dequeue, task CPU read and
  migration primitive, scheduler-statistics update, and wake tracepoints used
  by ``try_to_wake_up()``.
* The task runqueue lock and unlock used by ``ttwu_runnable()``.
* The proxy state query used by the runnable path. Rust calls
  ``update_rq_clock()`` and ``enqueue_task()`` directly.
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
* The CPU-to-runqueue mapping, RCU read protection, RCU current-task
  dereference, runqueue lock and interrupt-state ownership, and lockdep
  assertion used by the idle CPU wakeup path.
* Wakeup tracepoint emission used by both wake paths.
* Runqueue lock unpin and repin around a scheduling-class ``task_woken``
  callback.
* The C size of ``struct rq_flags``. Rust allocates alignment-safe storage
  with that size and passes its address to the runqueue lock helpers.
* The ``cpumask_any()``, ``is_cpu_allowed()``, ``task_cpu()``, and
  ``select_fallback_rq()`` operations used by task CPU selection.
* The task pi-lock held assertion used by the task CPU selection entry point.
* The utilization-clamp, pressure, scheduler-statistics, and core-scheduling
  primitives used by runqueue enqueue and dequeue.
* The runqueue-held lockdep assertion used by the direct enqueue and dequeue
  entries.
* Scheduling-class put-prev and set-next callback implementations reached
  through indirect calls from Rust.
* The runqueue donor mismatch warning.
* The fair, idle, and optional sched_ext class anchors, the scheduling-class
  size, the sched_ext static-key state, and scheduling-class balance callback
  implementations reached through indirect calls from Rust.
* The highest and lowest scheduling-class anchors, the fair and idle pick
  functions, the retry-task sentinel, the selected-task transition helper,
  and fatal invariant reporting used by next-task selection.
* The highest and lowest scheduling-class anchors, the fair and optional
  sched_ext anchors, the class size, the sched_ext static-key state, the
  retry-task sentinel, scheduling-class ``pick_task`` implementations, and
  fatal invariant helper used by core-scheduling task picking.
* Scheduling-class ``task_woken`` callback implementations reached through
  an indirect call from Rust.
* Blocking dequeue with selected sleep and special flags, and block
  accounting.
* The task runqueue membership writer assertion, migrated enqueue constants,
  the invalid sleeping-dequeue warning, and dequeue.

Other C-to-Rust production entries are:

* ``wake_q_add()``.
* ``wake_q_add_safe()``.
* ``wake_up_q()``.
* ``wake_up_process()``.
* ``wake_up_state()``.
* ``default_wake_function()``.
* ``put_prev_task()``.
* ``set_next_task()``.
* ``put_prev_set_next_task()``.
* ``prev_balance()``.
* ``rust_sched_pick_next_task()``.
* ``pick_task()``.

For deferred wake queues, the caller provides exclusively owned
``struct wake_q_head`` storage. Rust calls the C compare-exchange primitive
after the required pre-atomic barrier. A successful claim makes Rust append
the task's embedded node through the context-local tail pointer.
``wake_q_add()`` then acquires a queue-owned task reference.
``wake_q_add_safe()`` instead consumes the caller-owned reference and releases
it when another waker owns the node.

``wake_up_q()`` traverses entries in insertion order. For each entry, Rust
reads the next pointer, clears the current node, calls ``wake_up_process()``,
and releases the queue-owned reference. The head remains at its previous
value after traversal, matching the original contract. Callers that reuse a
head call the existing inline ``wake_q_init()`` helper.
When ``CONFIG_RUST_SCHED_WAKE_API`` is enabled, that call stays in Rust.
Otherwise it uses the C fallback directly.

For the direct wake entries, Rust defines ``wake_up_process()``,
``wake_up_state()``, and ``default_wake_function()``. The first always passes
``TASK_NORMAL`` and no wake flags. The second passes the caller's state mask
and no wake flags. The default wait-queue callback validates that its flags
contain only ``WF_SYNC`` and ``WF_CURRENT_CPU``, requests the original warning
when they do not, and wakes the entry's private task with the supplied mode
and flags. Each entry propagates the result unchanged. The constant and
layout checks, warning formatting, wait-queue layout, ordinary export
metadata, and task wakeup fallback stay in C.

``task_state_match()`` acquires ``p->pi_lock`` with interrupts disabled and
releases it before returning. The caller of ``ttwu_state_match()`` holds
``p->pi_lock`` across the complete Rust call. ``try_to_wake_up()`` disables
preemption for its complete body. It uses only disabled preemption on the
current-task path and acquires ``p->pi_lock`` with interrupts disabled on the
non-current path. ``ttwu_runnable()`` calls the narrow C ``__task_rq_lock()``
and ``__task_rq_unlock()`` helpers, so Rust owns the runqueue lock lifetime and
all operations between acquisition and release.

The callers of ``ttwu_do_activate()``, ``block_task()``, and
``wakeup_preempt()`` hold the runqueue lock across the complete Rust call.
``ttwu_do_activate()`` receives the matching ``struct rq_flags`` lock context
because the scheduling-class ``task_woken`` callback may temporarily drop and
reacquire the runqueue lock.

For task CPU selection, Rust directly defines ``select_task_rq()``. The caller
holds ``p->pi_lock``. Rust reads the affinity count, migration-disabled state,
affinity mask pointer, and scheduling-class callback while that lock is held.
Rust does not acquire or release any lock.

For runqueue enqueue and dequeue, Rust directly defines both public entries.
The caller holds the runqueue lock. When lockdep is enabled, Rust calls the
narrow C assertion helper before it performs any other operation. Rust owns
every conditional decision and the complete helper order. Rust reads the
selected scheduling-class callback through a compile-time checked offset and
calls it directly. C retains the runqueue-held assertion, the
scheduling-class implementations, the narrow accounting and core-scheduling
primitives, and the fallback definitions used when the Rust option is
disabled. Rust returns the scheduling-class dequeue result unchanged.

For scheduler-class transitions, Rust directly defines all three production
entries. The caller holds the runqueue lock across the complete Rust call.
Rust validates the donor, transfers ``rq->dl_server`` and the task server
pointers, takes the same-task short circuit, reads the selected put-prev and
set-next callbacks, and invokes them in order. The warning text,
scheduling-class callback implementations, and fallback definitions used when
the Rust option is disabled remain in C.

For the previous-task balance pass, Rust directly defines ``prev_balance()``.
The caller holds the runqueue lock across the complete Rust call. Rust reads
the donor class, obtains fixed class anchors and the scheduling-class size from
C, traverses active classes by byte offset, queries the current sched_ext
static-key state, invokes each optional balance callback, and stops at the
first non-zero result. The balance callback implementations and fallback
definition used when the Rust option is disabled remain in C.

For next-task selection, the caller holds the runqueue lock across the
complete Rust call. Rust clears the deferred scheduling-server pointer, checks
the sched_ext state, performs the fair-only fast path, invokes the previous
balance state machine directly on a restart when the Rust balance option is
enabled, scans active classes with ``pick_task`` callbacks, and transitions to
the selected task. When the Rust balance option is disabled, Rust calls a
narrow C fallback helper. C supplies the runqueue field offsets, class anchors,
retry sentinel, fair and idle pick implementations, transition helper, and
fatal invariant report.

For core-scheduling task picking, the caller holds the runqueue lock across the
complete Rust call. Rust directly defines ``pick_task()``. It clears
``rq->dl_server``, reads the fixed class anchors and class size, traverses
active classes, and invokes each class ``pick_task`` callback. Rust returns a
selected task or passes ``RETRY_TASK`` unchanged to its caller. If class
traversal exhausts, Rust calls the narrow C ``BUG()`` helper and reports the
original fatal idle-class invariant.

For the activation entry, C validates that it holds the runqueue lock and
passes the matching lock context to Rust. Rust selects and orders all state
changes and scheduler helpers. Rust reads the optional scheduling-class
callback and invokes it directly. Narrow C helpers unpin and repin the lock
exactly as the original C path did to preserve the callback lock contract.

For current-task reschedule requests, Rust directly defines ``resched_curr()``
and ``resched_curr_lazy()``. The caller holds the runqueue lock. Rust reads the
current task and runqueue CPU, applies the idle-task lazy-request promotion,
checks for duplicate requests, emits the scheduler trace, and selects local or
remote delivery. Narrow C helpers query the lazy-preemption mode and perform
the thread-flag writes, preemption-count update, IPI delivery, and tracepoint
emission in the original order.

For per-CPU reschedule requests, Rust directly defines ``resched_cpu()``. Rust
maps the CPU to its runqueue, acquires the runqueue lock with interrupts
disabled, decides whether the request is required, calls ``resched_curr()``
only for an online target or the current CPU, and releases the lock with the
saved interrupt state. Narrow C helpers perform the mapping, raw lock, online
query, and current-CPU query.

For the idle CPU wakeup entry, Rust directly defines ``wake_up_if_idle()``.
Rust maps the CPU to its runqueue, holds the RCU read lock, performs the first
idle-task check, and acquires the runqueue lock with interrupts disabled only
when the first check succeeds. Rust rechecks the current task under the
runqueue lock, calls ``resched_curr()`` only when both checks see an idle task,
and releases the runqueue and RCU locks in order.

For the production task wakeup entry, Rust directly defines
``try_to_wake_up()``. Rust disables preemption for the complete call, adds
``WF_TTWU``, and takes the lock-free current-task path when the target is
current. Otherwise it acquires the pi lock with interrupts disabled, applies
the lock-acquisition barrier, matches the requested state, checks runqueue
membership, and calls the runnable wake path when possible. If the task is not
runnable, Rust publishes ``TASK_WAKING``, attempts a wake-list queue after an
acquire load of ``on_cpu``, waits for a previous CPU when necessary, selects a
CPU, applies migration and I/O wait policy, and queues the task. It releases
the pi lock before success accounting and enables preemption last.
C retains the lock, barrier, preemption, conditional-load, trace, PSI,
migration, wake-list, queue, and statistics primitives.

For the blocking entry, C validates that the task runqueue lock is held and
passes the task state to Rust. Rust classifies the state and calls the bitfield,
dequeue, and accounting helpers in order. C retains ownership of the runqueue
lock across the complete Rust call.

For task activation and deactivation, Rust directly defines both public
entries. The caller holds the runqueue lock. When lockdep is enabled, Rust
calls the narrow C assertion helper before it performs any other operation.
Rust owns the transition decision and calls the enqueue and dequeue functions
directly through the bindgen-generated FFI declarations. Rust also reads and
writes task runqueue membership through the kernel atomic API. C retains the
lock, the runqueue-held assertion, the writer assertion, warning delivery, and
the fallback definitions used when the Rust option is disabled.

Retained C boundary
-------------------

Rust reads and writes the task current state, saved state, blocked marker, and
scheduling delay state through volatile accesses. These accesses preserve the
single load or store behavior of the former C scalar helpers. C defines the
task layout and supplies the task pointer. Rust acquires and releases
``p->pi_lock`` for ``task_state_match()``. The caller holds the same lock for
``ttwu_state_match()``.

``WARN_ON_ONCE`` remains in C because report formatting and rate limiting
depend on kernel diagnostics. Rust decides whether the debug-preemption state
mask is invalid.

``try_to_wake_up()`` is a Rust production entry. Rust owns its preemption
lifetime, lock and retry decisions, runnable short circuit, on-CPU wait and
wake-list decision, CPU selection and migration policy, final queue decision,
success accounting decision, and lock release order. C retains the memory
barrier and conditional-load helpers, raw pi lock and preemption primitives,
wake-list and runqueue queue mechanics, task migration and I/O accounting
primitives, tracepoints, and scheduler statistics.

Rust directly defines ``ttwu_runnable()`` and owns the task runqueue lock
lifetime through the narrow C ``__task_rq_lock()`` and ``__task_rq_unlock()``
helpers. Rust owns the queued check, clock update, delayed enqueue,
proxy return, preemption, and wake publication order. C retains the lock
helpers, scheduler queue and proxy operations, scheduling-class preemption
callback implementations, tracing, and the fallback definition.

The runqueue clock algorithm remains in C. It combines lockdep validation,
scheduler feature checks, clock-source sampling, ``sched_ext`` clock update,
scheduler-class time accounting, IRQ time accounting, and paravirtual steal
time accounting. Rust calls ``update_rq_clock()`` directly while it owns the
task runqueue lock. There is no clock-update wrapper.

The per-CPU reschedule path retains CPU-to-runqueue mapping, raw runqueue lock
primitives, IRQ state save and restore, and the CPU-online and current-CPU
queries in C. Rust owns the entry point, lock and interrupt-state ordering,
eligibility decision, and the conditional ``resched_curr()`` call. Rust
queries the current CPU only when the target CPU is offline. This preserves
the original short-circuit behavior. When the current-task reschedule option
is also enabled, ``resched_curr()`` enters its Rust implementation directly.

Rust directly defines ``wakeup_preempt()`` and owns the wakeup-preemption state
machine. It reads the donor, current task, selected class, and clock-update
flags from ``struct rq``. It compares the woken task class with the selected
class, calls the selected class ``wakeup_preempt`` operation, calls
``resched_curr()``, updates the selected class, and requests a redundant
clock-update skip in their original order. The caller holds the runqueue lock.
C retains scheduling-class callback implementations, reschedule delivery, and
the fallback definition.

The task CPU selection path retains only the primitive operations that it must
share with the rest of the scheduler. ``cpumask_any()`` chooses from the
affinity mask, ``is_cpu_allowed()`` applies the CPU-hotplug and task-type
policy, ``task_cpu()`` supplies the current CPU to fallback placement, and
``select_fallback_rq()`` performs the existing fallback policy. Rust chooses
when each primitive runs and owns the returned CPU. The scheduling-class
``select_task_rq`` implementations remain in C. A narrow C helper preserves
the task pi-lock held assertion when lockdep is enabled.

The idle CPU wakeup path retains CPU-to-runqueue mapping, RCU read protection,
the RCU current-task dereference, runqueue lock pin and unpin, interrupt-state
save and restore, the runqueue-held assertion, and reschedule delivery in C.
Rust owns the direct entry, the RCU and runqueue lock ordering, both idle
checks, and the conditional ``resched_curr()`` call.

The runqueue-operation path retains the runqueue lock and its assertion,
scheduling-class queue implementations, utilization-clamp accounting,
pressure accounting, scheduler-statistics accounting, core-scheduling
operations, and the fallback C definitions. Rust directly defines
``enqueue_task()`` and ``dequeue_task()``. Rust reads the operation flags and
core-scheduling state, selects each conditional operation, invokes each
primitive in the original order, calls the selected scheduling-class callback
directly, and returns the class result.

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
static-key state, the retry sentinel, generated field offsets, and the fatal
``BUG()`` delivery helper in C. Rust directly defines ``pick_task()`` and owns
the deferred-server clear, active-class iteration, callback lookup and
invocation, result precedence, and the decision to report class exhaustion. A
non-null result, including ``RETRY_TASK``, returns immediately. A null result
advances to the next active class.

Rust directly defines ``ttwu_do_activate()``. The caller holds the runqueue
lock. Rust owns wake-flag decoding, the runqueue ``nr_uninterruptible`` and
``nr_iowait`` updates, the conditions and order for delay-accounting
completion and task activation, enqueue-flag construction, preemption, wake
publication, callback lookup, and callback invocation. Rust keeps the same
unpin-callback-repin order as the C path. C retains the lock caller,
scheduling-class queue operations reached through ``activate_task()``,
scheduling-class callback implementations, tracing, delay accounting, lock pin
primitives, and the fallback definition.

Rust directly defines ``block_task()``. The caller holds the runqueue lock.
Rust owns the load and special-state masks, the contributes-to-load bitfield
write, the special dequeue decision, and the order of dequeue and accounting.
The accounting helper runs only after the scheduling class accepts the
dequeue. C retains runqueue lock ownership, scheduler queue operations,
accounting primitives, tracing, and the fallback definition.

The transition path retains the runqueue lock and its assertion, the KCSAN
writer assertion, warning delivery, queue operations, tracing, and the
fallback C definitions. Rust directly defines ``activate_task()`` and
``deactivate_task()``. Rust owns task runqueue membership reads and writes,
migrated-state detection, the condition that adds ``ENQUEUE_MIGRATED``, the
condition that warns about ``DEQUEUE_SLEEP``, and the order of membership
publication and dequeue. Rust composes the enqueue flags and calls
``enqueue_task()`` and ``dequeue_task()`` directly. There is no runtime C
wrapper for a queue operation or a runqueue membership state transition. C
supplies the ``ASSERT_EXCLUSIVE_WRITER`` mechanism and compile-time checked
constants. The C scheduler asserts that those constants match its private
scheduler definitions.

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

Bindgen presents ``struct rq_flags`` as an opaque zero-length type. Rust must
not allocate that type directly because the C runqueue lock helpers write the
complete structure. C exports ``RQ_FLAGS_SIZE`` from
``sizeof(struct rq_flags)``. Rust rounds that size up to ``usize`` storage,
keeps the storage aligned for ``usize``, and passes its address as
``struct rq_flags *``. This applies to both ``ttwu_runnable()`` and
``wake_up_if_idle()``. A change to the C structure size is absorbed at build
time without a hand-copied layout.

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
next active class. Reaching the lowest class without a result reports the
original fatal idle-class invariant through the narrow C ``BUG()`` helper.

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
current CPU to execute the Rust ``wake_up_if_idle()`` entry. The same real
traffic enters the Rust ``try_to_wake_up()`` entry, including its
current-task, runnable, and queued-wake paths, when that option is enabled.
The same real traffic enters the Rust runqueue enqueue and dequeue entries.
Every real context switch and scheduler task-selection transition enters the
Rust scheduler-class transition entries. Scheduling attempts that take the
general class scan also enter the Rust previous-task balance pass. Real
scheduling traffic enters the Rust next-task selection entry for every
non-core scheduling pick.
On an SMT machine, the core-scheduling path case creates a core-scheduling
cookie and schedules real tasks while the Rust core-pick entry is active.

The scheduler direct-entry unit must pass native four-vCPU and one-vCPU runs,
fallback and C-only runs with the unit options disabled, PREEMPT_RT, KASAN,
lockdep, hrtimer regressions, and a no-KUnit production build. Static checks
include Rustfmt, Clippy, Rustdoc, Sphinx, strict checkpatch, and
``git diff --check``.

Validation evidence
-------------------

The task wakeup direct-entry unit was validated on 2026-11-25 with x86_64
``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive native four-vCPU runs each passed all 37 scheduler cases.
  The native one-vCPU run passed 35 cases and skipped 2 cases that require
  multiple CPUs. The native hrtimer regression passed all 25 cases.
* The fallback run disabled only ``CONFIG_RUST_SCHED_TRY_WAKEUP`` while
  retaining the other Rust wake paths. Its four-vCPU scheduler run passed all
  37 cases, its one-vCPU run passed 35 cases with 2 skips, and its hrtimer
  regression passed all 25 cases.
* The C-only run disabled all ``CONFIG_RUST_SCHED_*`` options. Its four-vCPU
  scheduler run passed all 3 applicable cases, its one-vCPU run passed 2 cases
  with 1 skip, and its hrtimer regression passed all 25 cases.
* KASAN, lockdep, and PREEMPT_RT four-vCPU runs each passed all 37 cases. Their
  one-vCPU runs each passed 35 cases and skipped 2 cases. Their hrtimer
  regressions each passed all 25 cases.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, unexpected panic, or atomic-scheduling violation. The only panic was
  the expected missing-root-filesystem panic after KUnit completion.

Native ``rust/kernel.o`` defines ``try_to_wake_up()`` and the other six
scheduler wake entries. Native ``kernel/sched/core.o`` has only an undefined
reference to ``try_to_wake_up()``. In the fallback build,
``kernel/sched/core.o`` defines ``try_to_wake_up()`` and the Rust object does
not. In the C-only build, neither the Rust scheduled-entry set nor the other
Rust scheduler entries are present. Every linked image has one strong
``try_to_wake_up()`` definition, and it has no export metadata.

The no-KUnit production configuration enabled the new entry and its four
dependencies. It built with ``CLIPPY=1`` and booted to the expected
missing-root-filesystem panic. Rustfmt, Clippy, Rustdoc, strict checkpatch,
and ``git diff --check`` passed. Focused Sphinx completed with existing
cross-directory reference warnings that do not name a changed page.

The direct scheduler wake and blocking entry unit was validated on 2026-11-25
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* The native four-vCPU run passed all 37 cases. The one-vCPU run passed 35
  cases and skipped the 2 cases that require multiple CPUs.
* The native hrtimer regression passed all 25 cases.
* The four-vCPU fallback run that disabled all five unit options passed 21
  cases. Its one-vCPU run passed 19 cases and skipped 2 cases. Its hrtimer
  regression passed all 25 cases.
* The four-vCPU C-only run passed all 3 applicable cases. Its one-vCPU run
  passed 2 cases and skipped 1 case. Its hrtimer regression passed all 5
  applicable cases.
* The KASAN, lockdep, and PREEMPT_RT four-vCPU runs each passed all 37 cases.
  Their one-vCPU runs each passed 35 cases and skipped 2 cases. Their hrtimer
  regressions each passed all 25 cases.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, panic, or atomic-scheduling violation.

Native ``rust/kernel.o`` defines all six entries covered by that unit. Native
``kernel/sched/core.o`` has no definition and only unresolved references to
them. The KASAN, lockdep, and PREEMPT_RT objects have the same ownership. The
native linked image has one strong definition of each entry, and none of the
six appears in ``Module.symvers``.

Fallback and C-only ``rust/kernel.o`` objects do not define any of the six
entries. Their C fallback bodies are selected at compile time. The compiler
keeps local ``ttwu_do_activate()`` and global ``wakeup_preempt()`` out of line
in those configurations. The other fallback bodies are inlined. Fallback and
C-only export tables contain none of the six entries.

The no-KUnit production configuration enabled all five unit options and built
with ``CLIPPY=1``. The resulting four-vCPU image booted to the expected missing
root filesystem panic. A complete serial log contained no scheduler ``BUG``,
warning, KASAN report, lock dependency diagnosis, Oops, or other unexpected
panic.

The task runqueue transition direct-entry unit was validated on 2026-11-25
with x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU native runs each passed all 37 cases.
* The one-vCPU native run passed 35 cases and skipped 2 cases that require
  multiple CPUs.
* The four-vCPU fallback and C-only runs passed all 36 and 3 applicable
  cases. Their one-vCPU runs passed 34 and 2 cases and skipped 2 and 1 cases.
* The four-vCPU transition-only run passed all 4 applicable cases. Its
  one-vCPU run passed 3 cases and skipped 1 case.
* The four-vCPU KASAN, lockdep, and PREEMPT_RT runs each passed all 37 cases.
* The four-vCPU transition-only lockdep run passed all 4 applicable cases.
* Native and fallback four-vCPU hrtimer regression runs each passed all 25
  cases.
* All passing runs completed without a ``BUG``, warning, KASAN report, lock
  dependency diagnosis, Oops, panic, or atomic-scheduling violation.

Rust directly defines ``activate_task()`` and ``deactivate_task()``. Native,
transition-only, debug, and RT ``kernel/sched/core.o`` objects have only
undefined references to both entries. Their ``rust/kernel.o`` objects define
both entries. Fallback and C-only ``kernel/sched/core.o`` objects define both
entries and do not use the old Rust transition symbols. The linked images have
one definition of each entry, and neither entry is exported.

The direct entries run the runqueue-held assertion first when lockdep is
enabled. Activation selects ``ENQUEUE_MIGRATED`` when required, calls
``enqueue_task()``, then publishes the queued membership state. Deactivation
warns about sleeping dequeue, publishes the migrating state, then calls
``dequeue_task()``. The KCSAN writer assertion still follows each membership
store.

The no-KUnit production configuration enabled all fourteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains both Rust
entries and the narrow C helpers used by the transition path. It contains no
RQ-transition KUnit selector and no unresolved RQ-transition symbol. The
production image booted to the expected missing-root-filesystem panic without
a scheduler ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops,
or unexpected panic. Rustfmt, Clippy, Rustdoc, strict checkpatch, and
``git diff --check`` passed.

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

The scheduler-class transition entry unit was validated on 2026-11-25 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. The native configuration passed three
consecutive four-vCPU runs. One four-vCPU run passed for each remaining
configuration:

.. list-table::
   :header-rows: 1
   :widths: 55 45

   * - Configuration
     - Scheduler cases
   * - Full Rust
     - 37
   * - Class transition fallback
     - 36
   * - Class transition only
     - 4
   * - C only
     - 3
   * - PREEMPT_RT
     - 37
   * - KASAN
     - 37
   * - lockdep
     - 37
   * - Class transition only with lockdep
     - 4

Native and fallback hrtimer regressions passed all 25 cases. Rustfmt, Clippy,
Rustdoc, strict checkpatch, and ``git diff --check`` passed. The no-KUnit
production image built with Clippy. It contained the C warning helper and no
transition KUnit selector symbols.

The Rust object defined ``put_prev_task()``, ``set_next_task()``, and
``put_prev_set_next_task()``. The scheduler C object referenced each canonical
name as undefined, and the linked image had no unresolved symbols. The old
``rust_sched_class_put_prev_task()``, ``rust_sched_class_set_next_task()``, and
``rust_sched_class_put_prev_set_next_task()`` production names were absent.
The canonical names were not added to the automatic Rust GPL export list.

Disassembly showed that the production entries compare ``rq->donor``, call the
warning helper only on a mismatch, move the deferred server pointer, and take
the same-task return only after the server move. The complete transition loads
the put-prev callback before the set-next callback. It calls put-prev first
with the next task, then calls set-next with the first-task argument set to
true. The standalone entries pass a null next task to put-prev and false to
set-next.

No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
Oops, atomic scheduling diagnostic, or unexpected panic.

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
``activate_task()`` directly, and ``activate_task()`` is itself a Rust public
entry in the runqueue-transition unit.

Rust updates ``struct rq.nr_uninterruptible`` as an ``unsigned long`` at the
offset generated from the C layout. The KUnit case sets the count to two and
checks the Rust decrement result of one. C statically checks the field size and
all exported enqueue flag values.

Rust removes ``rust_helper_sched_ttwu_dec_uninterruptible()`` and
``rust_helper_sched_ttwu_activate()``. C retains the runqueue lock, the
scheduling-class queue operation reached through the Rust
``activate_task()`` entry, I/O wait delay accounting, preemption, tracing, and
the class callback contract.

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

The direct reschedule entry-point migration was validated on 2026-11-24 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Fourteen scheduler boots passed:

* The one-vCPU native, fallback, and C-only runs passed 35, 31, and 2 cases.
  They skipped 2, 2, and 1 cases that require multiple CPUs.
* Three consecutive four-vCPU native runs each passed all 37 cases.
* The four-vCPU current-task-only and per-CPU-only runs passed all 36 and 34
  applicable cases.
* The four-vCPU fallback and C-only runs passed all 33 and 3 applicable cases.
* The four-vCPU KASAN, lockdep, and PREEMPT_RT runs each passed all 37 cases.
* Four-vCPU native and fallback hrtimer regression runs each passed all 25
  cases.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, panic, or atomic-scheduling violation.

Native ``rust/kernel.o`` defines ``resched_curr()``, ``resched_curr_lazy()``,
and ``resched_cpu()``. Native ``kernel/sched/core.o`` defines none of them.
The current-task-only image defines the first two in Rust and ``resched_cpu()``
in C. The per-CPU-only image defines ``resched_cpu()`` in Rust and the first
two entries in C. Fallback and C-only objects define all three entries in C.
The PREEMPT_RT ``vmlinux`` has one definition of each entry. No old
``rust_sched_resched_curr`` or ``rust_sched_resched_cpu`` symbol remains, and
``vmlinux.symvers`` does not export the three unexported entries.

Rust owns the idle-task lazy-request promotion, duplicate-request check,
local or remote selection, runqueue lock and interrupt-state ordering,
online-or-current decision, and the conditional ``resched_curr()`` call. C
retains the lazy-preemption mode query, CPU-to-runqueue mapping, raw runqueue
lock primitives, tracepoints, thread-flag operations, preemption count, IPI
delivery, and idle wake tracing.

The full configuration passed Rustfmt, Clippy, strict checkpatch, and
``git diff --check``. The no-KUnit production image built with Clippy and
contained the three Rust entries, the narrow C helpers, no KUnit selector
symbol, and no unresolved symbol.

The idle CPU wakeup direct-entry unit was validated on 2026-11-24 with x86_64
``LLVM=1`` builds and QEMU 11.1.3. Twelve primary scheduler boots, one KASAN
confirmation boot, and two hrtimer regression boots passed:

* The one-vCPU native, fallback, and C-only runs passed 35, 33, and 2 cases.
  They skipped 2, 2, and 1 cases that require multiple CPUs.
* Three consecutive four-vCPU native runs each passed all 37 cases.
* The four-vCPU idle-wakeup-only run passed all 5 applicable cases.
* The four-vCPU fallback and C-only runs passed all 35 and 3 applicable cases.
* The four-vCPU KASAN run passed all 37 cases twice. The lockdep and
  PREEMPT_RT runs each passed all 37 cases.
* Four-vCPU native and fallback hrtimer regression runs each passed all 25
  cases.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, panic, or atomic-scheduling violation.

Native ``rust/kernel.o`` defines ``wake_up_if_idle()``. Native
``kernel/sched/core.o`` no longer defines the entry. The fallback and C-only
``kernel/sched/core.o`` objects define it, and their Rust objects do not. No
old ``rust_sched_wake_if_idle_needs_lock`` or
``rust_sched_wake_if_idle_locked`` symbol remains.

Rust owns the entry, RCU read-lock lifetime, CPU-to-runqueue lookup, first idle
check, conditional runqueue lock, locked recheck, conditional
``resched_curr()`` call, and lock release order. C retains the RCU and
runqueue lock primitives, current-task dereference, lockdep assertion, and
reschedule delivery.

The action table covers a non-idle first check, a successful first check with
a non-idle locked recheck, and a successful first check with an idle locked
recheck. A real four-vCPU case calls the Rust ``wake_up_if_idle()`` entry for
the current CPU. The idle-wakeup-only build confirms that this path does not
depend on another Rust scheduler option.

One initial KASAN run emitted a post-test clocksource watchdog timeout. An
immediate rerun completed without that timeout or any KASAN diagnostic.

The task CPU selection direct-entry unit was validated on 2026-11-24 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Twelve scheduler boots and two
hrtimer regression boots passed:

* The one-vCPU native, fallback, and C-only runs passed 35, 32, and 2 cases.
  They skipped 2, 1, and 1 cases that require multiple CPUs.
* Three consecutive four-vCPU native runs each passed all 37 cases.
* The four-vCPU task-selection-only run passed all 7 applicable cases.
* The four-vCPU fallback and C-only runs passed all 33 and 3 applicable cases.
* The four-vCPU KASAN, lockdep, and PREEMPT_RT runs each passed all 37 cases.
* Four-vCPU native and fallback hrtimer regression runs each passed all 25
  cases.
* No run reported a ``BUG``, warning, KASAN report, lock dependency diagnosis,
  Oops, panic, or atomic-scheduling violation.

Rust directly defines ``select_task_rq()``. Rust reads the allowed-CPU count
and migration-disabled state before it loads the scheduling-class callback.
The callback is loaded only on the class path and receives the original wake
flags. Rust sets ``WF_RQ_SELECTED`` only after that callback returns.

The single-CPU and migration-disabled path calls ``cpumask_any()`` and keeps
the wake flags unchanged. Both paths check ``is_cpu_allowed()``. An ineligible
CPU enters ``select_fallback_rq()`` with the task's current CPU. KUnit covers
the routing table, class dispatch, flag publication, both fast-path
conditions, and a valid but disallowed callback result. The full Rust
configuration also wakes a real ``SCHED_FIFO`` kthread, which exercises the
production callback load and RT scheduling-class selection.

C retains the four primitive adapters ``cpumask_any()``,
``is_cpu_allowed()``, ``task_cpu()``, and ``select_fallback_rq()``. It also
retains scheduling-class callback implementations and a narrow lockdep helper
for the task pi-lock assertion. Rust owns the entry point, path selection,
callback dispatch, flag ordering, validity checking, fallback decision, and
the returned CPU.

Native ``rust/kernel.o`` defines ``select_task_rq()``. Native
``kernel/sched/core.o`` has only an undefined reference. The linked production
image has one definition, and the fallback and C-only Rust objects define no
task-selection entry. No old ``rust_sched_select_task_rq`` symbol remains, and
the entry is absent from ``Module.symvers``.

The no-KUnit production configuration enabled all fourteen scheduler Rust
options and built the image with ``CLIPPY=1``. Its symbol table contains the
Rust entry point and the four narrow primitive helpers. It contains no
task-selection KUnit selector symbol and no unresolved Rust scheduler symbol.

The runqueue operation direct-entry unit was validated on 2026-11-25 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3:

* Three consecutive four-vCPU native runs each passed all 37 cases.
* The four-vCPU runqueue-operations-only run passed all 5 applicable cases.
* The four-vCPU fallback and C-only runs passed all 35 and 3 applicable cases.
* The four-vCPU KASAN, lockdep, and PREEMPT_RT runs each passed all 37 cases.
* Native and fallback four-vCPU hrtimer regression runs each passed all 25
  cases.
* The one-vCPU native image normally passed 35 cases and skipped 2 cases that
  require multiple CPUs. The existing uninterruptible-timeout case was
  intermittent under concurrent host build load. It failed once in ten runs
  for both the current image and the unchanged image from the previous unit.
* All passing runs completed without a ``BUG``, warning, KASAN report, lock
  dependency diagnosis, Oops, panic, or atomic-scheduling violation.

Rust directly defines ``enqueue_task()`` and ``dequeue_task()``. The C object
has only undefined references to both entries. The Rust entries call the
runqueue-held assertion first when lockdep is enabled, then execute the
original helper order. Enqueue updates the clock, utilization clamp, class
callback, pressure, statistics, and core scheduling. Dequeue updates core
scheduling, clock, statistics, pressure, utilization clamp, and the class
callback. Each class callback is loaded once from the selected
scheduling-class pointer.

The isolated RQ-operations build initially exposed a missing bindgen
declaration for ``update_rq_clock()``. The scheduler Rust header now declares
that boundary whenever either the runnable option or the runqueue-operation
option is enabled.

The no-KUnit production configuration enabled all fourteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains both Rust
entries, all ten narrow RQ-operation C helpers, and the runqueue-held
assertion helper under lockdep. It contains no RQ-operation KUnit selector and
no unresolved Rust scheduler symbol. The production image booted to the
expected missing-root-filesystem panic without a scheduler ``BUG``, warning,
KASAN report, lock dependency diagnosis, Oops, or unexpected panic. Rustfmt,
strict checkpatch, and ``git diff --check`` passed.

The previous-task balance ownership unit was validated on 2026-11-25 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. The full-Rust configuration passed
three consecutive four-vCPU scheduler boots. The remaining focused
configurations passed one applicable scheduler boot. Native and C balance
fallback hrtimer regressions passed all 25 cases.

.. list-table::
   :header-rows: 1
   :widths: 70 30

   * - Configuration
     - Scheduler cases
   * - Full Rust
     - 37
   * - C balance fallback
     - 36
   * - Previous-balance only
     - 5
   * - C only
     - 3
   * - PREEMPT_RT
     - 37
   * - KASAN
     - 37
   * - lockdep
     - 37
   * - Previous-balance only with lockdep
     - 5

The previous-task balance action table covers absent, non-stopping, and
stopping callbacks. The active-class action table covers a regular transition,
the switched-all fair-class skip, the disabled sched_ext-class skip, and an
enabled sched_ext transition.

The no-KUnit production configuration enabled all fourteen scheduler Rust
options and built with ``CLIPPY=1``. Its symbol table contains
``prev_balance()``, the fair and idle pick helpers, the retry sentinel, the
transition helper, both class anchors, and the class-size, sched_ext-class,
and sched_ext-state helpers. It contains no previous-balance KUnit selectors
and no unresolved symbol. The automatic Rust GPL export table omits
``prev_balance``. The C scheduler-core object contains neither a definition
nor a reference for the balance entry because the Rust next-task path calls
the internal Rust state machine directly.

Disassembly shows that ``prev_balance()`` enters the shared Rust state
machine. The state machine obtains the class anchors and byte stride, loads
the donor class, checks the idle-class termination condition, queries the
sched_ext state, loads each optional balance callback, performs the indirect
call, and stops on a non-zero result. The production image booted to the
expected missing-root-filesystem panic without another scheduler diagnostic.

An additional production image enabled ``SCHED_CLASS_EXT`` with BPF JIT and
BTF. Its symbol table contains the real ``ext_sched_class`` anchor and no
unresolved symbol. The image booted to the expected panic without another
scheduler diagnostic. The sched_ext static key is disabled during this boot,
so active-class traversal must skip the real sched_ext slot and continue to
the idle anchor.

The earlier next-task selection ownership unit was validated on 2026-11-13 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Each configuration passed three
consecutive four-vCPU runs for the applicable scheduler and hrtimer suites.
The current direct balance restart path is covered by the previous-task
balance validation above.

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
sentinel, the direct previous-balance state machine, the transition helper,
the highest and lowest class anchors, and the class-size, sched_ext-class, and
sched_ext-state helpers. It contains none of the three next-task KUnit
selectors and no unresolved symbol.

Disassembly shows the deferred-server clear, sched_ext gate, donor-class and
fair-class comparison, runnable-count check, fair and idle fallback, previous
balance call, indirect class callback scan, retry loop, final transition, and
the null return used by C for the fatal idle-class invariant. The production
image booted to the expected missing-root-filesystem panic without another
scheduler ``BUG``, warning, KASAN report, lock dependency diagnosis, Oops, or
unexpected panic. Rustfmt, strict checkpatch, and ``git diff --check`` passed.

The core-scheduling task-pick entry unit was validated on 2026-11-26 with
x86_64 ``LLVM=1`` builds and QEMU 11.1.3. Four-vCPU runs used a real SMT
topology with one socket, two cores, and two threads per core. One-vCPU runs
covered the single-CPU path.

.. list-table::
   :header-rows: 1
   :widths: 40 20 20 20

   * - Configuration
     - SMP4 scheduler
     - SMP1 scheduler
     - hrtimer
   * - Full Rust
     - 39 pass
     - 36 pass, 3 skip
     - 25 pass
   * - C core-pick fallback
     - 38 pass
     - 35 pass, 3 skip
     - 25 pass
   * - C-only
     - 4 pass
     - 2 pass, 2 skip
     - 25 pass
   * - PREEMPT_RT
     - 39 pass
     - 36 pass, 3 skip
     - 25 pass
   * - KASAN
     - 39 pass
     - 36 pass, 3 skip
     - 25 pass
   * - lockdep
     - 39 pass
     - 36 pass, 3 skip
     - 25 pass

The real-path case checks that SMT is active, creates a core-scheduling cookie
with ``PR_SCHED_CORE_CREATE``, and schedules a one-millisecond timeout. The
full Rust, C fallback, and C-only images execute this case. The native,
C-fallback, and C-only CPU-priority suites passed all six cases.

The no-KUnit production configuration enabled all eighteen scheduler Rust
options and built with ``CLIPPY=1``. Native ``rust/kernel.o`` defines
``pick_task()``. Native ``kernel/sched/core.o`` has an undefined reference to
that entry and no production definition. The linked image has one strong
definition, no old adapter symbol, no export metadata, and no unresolved
scheduler symbol. The production image booted on the same SMT topology to the
expected missing-root-filesystem panic without another scheduler ``BUG``,
warning, KASAN report, lock dependency diagnosis, Oops, or unexpected panic.
Rustdoc, rustfmt, strict checkpatch, and ``git diff --check`` passed.

The deferred wake-queue and direct wake-entry unit was validated on 2026-11-26
with x86_64 builds for native, fallback, C-only, and PREEMPT_RT
configurations. KASAN used ``LLVM=1``. QEMU 11.1.3 ran the tests. Four-vCPU
runs used one socket, two cores, and two threads per core. One-vCPU runs
covered the single-CPU path.

.. list-table::
   :header-rows: 1
   :widths: 30 20 25 20 15

   * - Configuration
     - SMP4 wake
     - SMP1 wake
     - hrtimer
     - cpupri
   * - Full Rust
     - 44 pass
     - 41 pass, 3 skip
     - 25 pass
     - 6 pass
   * - Rust wake / C ttwu
     - 44 pass
     - not run
     - 25 pass
     - 6 pass
   * - Direct-wake fallback
     - 43 pass
     - 40 pass, 3 skip
     - 25 pass
     - 6 pass
   * - C-only
     - 6 pass
     - 4 pass, 2 skip
     - 25 pass
     - 6 pass
   * - PREEMPT_RT
     - 44 pass
     - 41 pass, 3 skip
     - 25 pass
     - not run
   * - KASAN
     - 44 pass
     - 41 pass, 3 skip
     - 25 pass
     - not run
   * - lockdep
     - 44 pass
     - 41 pass, 3 skip
     - 25 pass
     - 6 pass

The wake-queue cases verify queue membership, duplicate insertion, reference
counts, safe-reference transfer, failed safe insertion, node clearing, and
wake delivery with a sleeping kernel thread. The direct entry cases verify a
mismatched state mask, a matching state mask, the default wait-queue callback,
and the supported and rejected callback flag combinations. Every focused boot
reported no failed case, unexpected ``BUG`` or warning, KASAN report, lock
dependency diagnosis, Oops, or panic.
A lockdep diagnostic configuration that used the Rust direct wake entries with
the C ``try_to_wake_up()`` fallback also passed 44 four-vCPU scheduler cases,
all 25 hrtimer cases, and all 6 CPU-priority cases.

The no-KUnit production configuration enabled all eighteen scheduler Rust
options and built with ``CLIPPY=1``. Native ``rust/kernel.o`` defines all
three wake-queue entries and all three direct wake entries. Native
``kernel/sched/core.o`` contains no production definition for the queue
entries, ``wake_up_process()``, or ``default_wake_function()``. It retains
the C fallback for ``wake_up_state()`` only in configurations that disable
the direct wake option. The linked native image has one strong definition for
each enabled entry, no Rust adapter name, no unresolved reference, an ordinary
export for ``wake_up_process()`` and ``default_wake_function()``, and no export
for ``wake_up_state()``.
Rustdoc, rustfmt, strict checkpatch, and ``git diff --check`` passed. Focused
Sphinx completed with only pre-existing unrelated reference warnings.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The wake-queue head, node, or task layout changes.
* The wake-queue tail sentinel, compare-exchange ordering, task-reference
  ownership, or direct wake-entry contract changes.
* The normal task-state mask, supported default wait-queue flags, warning
  formatting, wait-queue layout, or ordinary export metadata changes.
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
* The direct reschedule entry signatures, lazy-preemption mode query, raw
  runqueue lock helpers, or interrupt-state restoration contract changes.
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
* The task wakeup entry changes its preemption lifetime, prelude warning or
  blocked-state clear, pi-lock scope, state-match retry, runqueue-membership
  barrier, waking-state publication, on-CPU acquire load, wake-list attempt,
  conditional wait, CPU selection, migration and I/O wait policy, wake-CPU
  repair, final queue order, success accounting, or lock release order.
* The task wakeup helper contract changes for preemption, pi locking, memory
  barriers, conditional loads, tracing, PSI or delay accounting, task CPU
  reads, migration, wake-list queueing, runqueue queueing, or statistics.
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
