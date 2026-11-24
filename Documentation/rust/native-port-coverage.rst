.. SPDX-License-Identifier: GPL-2.0

Native Rust port coverage ledger
================================

Purpose
-------

This page records the authoritative production ownership of the native Rust
port. It distinguishes complete native components from earlier migrations
that still use a C entry-point adapter. It also records justified C and
assembly boundaries and the work that remains.

Status definitions
------------------

``migrated``
  Rust owns the complete production component or closed subsystem boundary.
  Rust defines the public production entry points. The native build excludes
  the corresponding C implementation.

``boundary-retained``
  C or assembly owns a named type layout, ABI contract, architecture
  primitive, boot entry, or other shared boundary. The entry includes the
  reason and review condition.

``pending``
  The scope is not yet native Rust, or an earlier Rust algorithm still uses a
  C ABI adapter. A pending item does not count as complete.

Migrated components
-------------------

Lock-less lists
  Status: ``migrated``.

  Evidence: :doc:`native-llist`.

  Rust defines ``llist_del_first()``, ``llist_del_first_this()``, and
  ``llist_reverse_order()``. The native build contains no C implementation or
  C ABI adapter for those operations.

Klist entry operations
  Status: ``migrated``.

  Evidence: :doc:`native-klist`.

  Rust defines the eleven public GPL symbols for insertion, deletion,
  attachment state, and iteration. The native C file retains list
  initialization, blocking removal, and the shared wakeup callback.

Reference counting
  Status: ``migrated``.

  Evidence: :doc:`refcount`.

  Rust directly defines the eleven public increment, decrement, saturation,
  limited-acquire, and conditional-locking entry points. Rust owns every
  transition, saturation, use-after-free, overflow, limit, and held-lock
  decision. C retains the shared layout and saturation encoding, architecture
  atomics and barriers, mutex and spinlock primitives, warning sites, and the
  original ordinary export classes. C fallback configurations retain the
  complete implementation.

Per-CPU reference lifecycle
  Status: ``migrated``.

  Evidence: :doc:`native-percpu-refcount`.

  Rust directly defines the nine public initialization, mode-switch, kill,
  exit, zero-test, reinitialization, and resurrection entry points. Rust owns
  every mode transition, initial count, RCU confirmation, release, and
  resurrection decision. C retains the shared layout and flags, inline
  get/put fast paths, allocation, locking, RCU, per-CPU, atomic, and
  diagnostic helpers. C fallback configurations retain the complete
  lifecycle.

Lockref operations
  Status: ``migrated``.

  Evidence: :doc:`native-lockref`.

  Rust defines the six public ``lockref_*`` symbols. The native C object
  retains only their non-GPL export metadata. C also retains the packed
  layout, initialization, architecture compare-exchange, and spinlock
  helpers.

Kref lifecycle
  Status: ``migrated``.

  Evidence: :doc:`native-kref`.

  Rust defines the seven public ``kref_*`` functions. Rust owns
  initialization, reference reads and increments, conditional increments,
  final-reference detection, and release callback dispatch. The native C
  object retains only their ordinary export metadata. C also retains the
  embedded refcount layout, compile-time initializer, mutex and spinlock
  operations, and callback ABI. C fallback configurations retain the complete
  inline implementation. All 14 kref KUnit boots passed 4 of 4 cases, the
  native SMP4 adjacent suites passed 22 of 22 cases, and native, fallback,
  and C-only module builds passed modpost.

Decrement-and-lock protocols
  Status: ``migrated``.

  Evidence: :doc:`native-dec-and-lock`.

  Rust directly defines the five public spinlock, IRQ-save, raw-lock, and
  mutex variants. Rust owns the fast decrement, lock-backed slow path, second
  decrement, and held-lock result. C retains architecture atomics, lock and
  IRQ primitives, and ordinary export metadata. C fallback configurations
  retain the complete implementations. All 16 direct-entry KUnit runs and the
  adjacent mutex, rwlock, and lockref regressions passed.

Completion state machine
  Status: ``migrated``.

  Evidence: :doc:`completion`.

  Rust directly defines the fourteen public completion, timeout,
  signal-aware, and non-blocking entry points. Rust owns count transitions,
  waiter loops, timeout and signal results, broadcast saturation, and
  non-blocking consumption. C retains task, scheduler, signal, raw-lock,
  simple-wait, and ordinary export boundaries. C fallback configurations
  retain the complete implementations.

Counting semaphore state machine
  Status: ``migrated``.

  Evidence: :doc:`semaphore`.

  Rust directly defines the six public semaphore acquisition, trylock,
  timeout, and release entry points. Rust owns every count transition,
  positive-count fast path, FIFO waiter insertion and removal, signal and
  timeout result, direct handoff, and wake selection decision. C retains the
  shared layout and initialization, raw locks, task state, signal predicates,
  scheduling, intrusive list and wake queue primitives, hung-task and trace
  accounting, sleep-context assertion, and ordinary export metadata. C
  fallback configurations retain the complete implementation. All twelve
  KUnit boots passed 6 of 6 cases, the adjacent lock and wait suites passed
  49 of 49 cases, and the non-GPL module ABI test linked all six ordinary
  exports.

Mutex protocol and public entries
  Status: ``migrated``.

  Evidence: :doc:`mutex`.

  Rust directly defines thirteen non-debug initialization, query, lock,
  trylock, I/O wait, unlock, and wait/wound entry points. With
  ``CONFIG_DEBUG_LOCK_ALLOC`` it defines fourteen nested and nest-lock
  entries. Rust owns owner encoding, fast acquisition and release,
  contention trylock, wait/wound context checks, handoff requests, direct
  pickup, slow-path selection, and slow-unlock state preparation. C retains
  the shared layouts, atomics and memory ordering, lockdep and lock-stat
  delivery, tracing, debug validation, optimistic spinning, intrusive
  waiters, task state, scheduling, wakeups, scheduler proxy policy,
  wait/wound ordering, and ordinary export metadata. C fallback
  configurations retain the complete entry implementations.

Read-write semaphore state machine and public entries
  Status: ``migrated``.

  Evidence: :doc:`rwsem`.

  Rust directly defines the eleven ordinary initialization, lock, trylock,
  unlock, and downgrade entry points. With ``CONFIG_DEBUG_LOCK_ALLOC`` it also
  defines the seven nested and anonymous-owner entries. Rust owns fast and
  slow path selection, reader and writer count transitions, reader stealing,
  writer handoff requests, owner encoding, error results, and release wake
  decisions. C retains the shared layout, atomics and memory ordering,
  preemption control, intrusive waiters, optimistic spinning, scheduling,
  wake queues, lockdep and lock-stat delivery, diagnostics, and ordinary
  export metadata. C fallback configurations retain the complete entry
  implementations.

Queued read-write spinlock state machine and public entries
  Status: ``migrated``.

  Evidence: :doc:`rwlock`.

  Rust directly defines the six entry points used by the inline queued-lock
  API and the two public slow-path entries. Rust owns reader and writer fast
  acquisition, non-blocking acquisition, provisional count handling, writer
  waiting and handoff, slow-path count transitions, and unlock transitions.
  C retains the shared layout, atomics and memory ordering, architecture wait
  queue lock, interrupt-context detection, tracing, preemption and lockdep
  wrappers, and ordinary non-GPL export metadata. The Rust slow paths remain
  in ``.spinlock.text``. C fallback and C-only configurations retain the
  complete slow-path implementations.

Simple wait queue state machine
  Status: ``migrated``.

  Evidence: :doc:`swait`.

  Rust directly defines the ten public initialization, wake, preparation,
  signal, and completion entry points. Rust owns queue membership, FIFO wake
  selection, bounded wake-all traversal, task-state ordering, signal
  rejection, cleanup, and internal locked wake-all decisions. C retains the
  shared layouts and construction macros, raw lock and list helpers, task,
  signal, and scheduler primitives, and the original export classes. C
  fallback configurations retain the complete implementations. All 12 KUnit
  boots passed 5 of 5 cases, the adjacent SMP4 suites passed 20 of 20 cases,
  and the module ABI test linked all seven ordinary exports.

Wait queue state machine
  Status: ``migrated``.

  Evidence: :doc:`waitqueue`.

  Rust directly defines all 24 public and internal wake, entry-operation,
  preparation, signal, completion, woken, and locked-wait entry points. Rust
  owns entry ordering, callback selection, autoremove and woken transitions,
  signal rejection, task-state ordering, finish cleanup, callback traversal,
  exclusive accounting, wake variants, and POLLFREE traversal. C retains the
  shared layouts, locking, intrusive list, scheduler, signal, barrier, poll,
  task, and timeout primitives, plus ordinary export metadata. C fallback
  configurations retain the complete implementation. All 12 focused KUnit
  boots passed 18 of 18 cases, the native SMP4 adjacent suites passed 69 of
  69 cases, and the module ABI test linked all 22 exports.

Bit and variable wait state machine
  Status: ``migrated``.

  Evidence: :doc:`wait-bit`.

  Rust directly defines all 15 exported bit, variable, queue-selection,
  callback, and scheduler wait entries. Rust owns queue selection, key
  matching, wait construction, normal and locked wait loops, action results,
  deadlines, variable initialization, wake submission, and scheduler action
  decisions. C retains the shared layouts and table, hashing, atomic bit
  operations, task and jiffies reads, scheduler and signal primitives, and
  ordinary export metadata. C fallback configurations retain the complete
  implementation.

RCU wait lifecycle
  Status: ``migrated``.

  Evidence: :doc:`native-rcuwait`.

  Rust directly defines the five public initialization, activity,
  publication, completion, and wake entry points. Rust also owns the
  condition, signal, timeout, scheduling, and cleanup decisions behind the
  three internal wait helpers. C retains the shared layout, caller condition
  expansion, low-level RCU, task-state, signal, scheduler, and wake helpers,
  and the original export classes. C fallback configurations retain the
  complete inline and out-of-line implementations. All 20 final KUnit boots
  passed 8 of 8 cases, the adjacent SMP4 suites passed 23 of 23 cases, and
  the module ABI test linked all eight symbols.

Task work queues
  Status: ``migrated``.

  Evidence: :doc:`native-task-work`.

  Rust directly defines the five public enqueue, cancellation, and drain
  entries. Rust owns LIFO publication, cancellation matching, exit-sentinel
  sealing, queue detachment, callback dispatch, and reentrant draining. C
  retains task fields, the task PI lock, compare-exchange primitives,
  notification flags, diagnostics, IRQ work, and scheduler progress. All five
  entries remain unexported. C fallback configurations retain the complete
  implementations.

IRQ work execution
  Status: ``migrated``.

  Evidence: :doc:`native-irq-work`.

  Rust directly defines the seven public queue, tick, single-item, drain,
  needs-CPU, and synchronization entries. Rust owns claim and duplicate
  suppression, local and remote queue policy, PENDING and BUSY transitions,
  lockless-list draining, reentrant callbacks, timer-tick processing, and fast
  and slow synchronization decisions. C retains per-CPU storage, delivery and
  worker plumbing, context diagnostics, low-level primitives, and export
  metadata. The three historical GPL exports remain unchanged. The other four
  public entries and the PREEMPT_RT lazy-worker bridge remain unexported. C
  fallback configurations retain the complete implementations.

IRQ event dispatch
  Status: ``migrated``.

  Evidence: :doc:`native-irq-event`.

  Rust directly defines the four internal per-CPU dispatch, descriptor
  lifecycle, and threaded-wake entry points. Rust owns action traversal,
  handler result aggregation, exact wake-result matching, duplicate wake
  suppression, oneshot and active-count updates, and pending/in-progress
  transitions. C retains descriptor and action layouts, handler invocation
  with tracing and duration diagnostics, lock and interrupt transitions,
  thread flags, scheduler wakeup, and randomness. All four entries remain
  unexported. C fallback configurations retain the complete implementations.

IRQ resend state machine
  Status: ``migrated``.

  Evidence: :doc:`native-irq-resend`.

  Rust directly defines the four internal check, clear, initialization, and
  tasklet-drain entry points. Rust owns level, replay, and pending decisions,
  hardware retrigger fallback, nested parent selection, software queue
  insertion, duplicate suppression, tasklet drain order, and replay-state
  transitions. C retains descriptor fields, the raw resend lock, hlist
  operations, tasklet storage and scheduling, irqchip callbacks, IRQ-number
  lookup, and flow-handler invocation. All four entries remain unexported. C
  fallback configurations retain the complete implementations.

IRQ spurious detection
  Status: ``migrated``.

  Evidence: :doc:`native-irq-spurious`.

  Rust directly defines the internal ``note_interrupt()`` entry. Rust owns
  polling exclusions, bad-result classification, deferred threaded
  accounting, unhandled windows, misroute policy, stuck-IRQ thresholds, and
  final state transitions. C retains descriptor fields, jiffies arithmetic,
  diagnostics, full descriptor scans, IRQ disabling, and timer rearming. The
  entry remains unexported. C fallback configurations retain the complete
  implementation.

Kobject lifecycle
  Status: ``migrated``.

  Evidence: :doc:`native-kobject`.

  Rust defines the four public ``kobject_*`` symbols for typed
  initialization, reference acquisition, conditional acquisition, and final
  reference dispatch. The native C object retains only their non-GPL export
  metadata. C also retains bitfield accessors, diagnostics, the release
  callback bridge, and final cleanup.

Interval tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-interval-tree`.

  Rust defines the eight public GPL symbols for insertion, removal, subtree
  search, overlap iteration, and span iteration. The native C object retains
  only the intrusive link and cached augmented erase helpers. C fallback
  configurations retain the complete interval tree algorithms.

Red-black tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-rbtree`.

  Rust defines the eleven public GPL and non-GPL symbols for insertion,
  removal, augmented callbacks, traversal, replacement, and linked-tree
  maintenance. The native C object retains only non-GPL export metadata and
  uses one internal Rust augmented erase bridge. C fallback configurations
  retain the complete algorithms.

Priority lists
  Status: ``migrated``.

  Evidence: :doc:`native-plist`.

  Rust directly defines ``plist_add()``, ``plist_del()``, and
  ``plist_requeue()``. It owns priority placement, equal-priority FIFO
  ordering, bucket promotion, requeue decisions, and the validation sequence.
  C retains intrusive layouts, raw list primitives, and diagnostic helpers.
  The functions remain unexported.

List sort
  Status: ``migrated``.

  Evidence: :doc:`native-list-sort`.

  Rust directly defines ``list_sort()``. It owns stable bottom-up merging,
  pending-list state, comparator invocation, and restoration of the circular
  doubly-linked list. The native C object retains only the non-GPL export
  metadata. C fallback configurations retain the complete algorithm.

List LRU
  Status: ``migrated``.

  Evidence: :doc:`native-list-lru`.

  Rust directly defines the fourteen node-local add, delete, isolate, count,
  and walk entry points. Nine functions retain GPL exports, three public
  helpers remain unexported, and two caller-locked helpers are internal. Rust
  owns item presence, list placement, slice and node count transitions,
  shrinker notification, scan budgets, retry and stop handling, and isolate
  accounting. The native C object retains lock accessors, initialization,
  destruction, memory-cgroup slice allocation, and reparenting. C fallback
  configurations retain the complete algorithms.

Generic bit searches
  Status: ``migrated``.

  Evidence: :doc:`native-find-bit`.

  Rust directly defines the sixteen public out-of-line searches and owns word
  traversal, range masks, partial-word filtering, population counts, nth-bit
  selection, clump extraction, and random-bit resolution. The native C object
  retains ordinary non-GPL export metadata. C also retains the shared bitmap
  storage layout and the random-number primitive. C fallback configurations
  retain the complete algorithms.

Generic bitmap operations
  Status: ``migrated``.

  Evidence: :doc:`native-bitmap-ops`.

  Rust directly defines the 31 public out-of-line bitmap operations and owns
  logical operations, shifts, population counts, range updates, mapping,
  scatter and gather selection, and array-layout conversion. Twenty-nine
  functions retain ordinary non-GPL exports, and ``bitmap_onto()`` and
  ``bitmap_fold()`` remain unexported. The native C object retains only export
  metadata. C also retains allocation, parsing, formatting, user-copy paths,
  storage operations, and constant-foldable inline accessors. C fallback
  configurations retain the complete algorithms. All nine bitmap KUnit runs
  passed, and native symbol inspection showed all 31 Rust-defined entries.

Maple Tree loads
  Status: ``migrated``.

  Evidence: :doc:`native-maple-tree-load`.

  Rust directly defines ``mtree_load()`` and owns root selection, pivot
  descent, dead-node restart, and the RCU lookup lifetime. C retains node
  encoding and layout, protected slot access, lockdep diagnostics, tracepoint
  emission, and ordinary export metadata. C fallback configurations retain
  the complete load traversal. All 16 load KUnit runs and complete Maple Tree
  self-tests passed.

Maple Tree stateful walks
  Status: ``migrated``.

  Evidence: :doc:`native-maple-tree-walk`.

  Rust directly defines ``mas_walk()`` and owns state normalization, pivot
  descent, range metadata transitions, dead-node restart, and single-entry
  results. C retains node encoding and layout, protected slot access,
  traversal metadata, and lockdep helpers. C fallback configurations retain
  the complete stateful walk. All 16 walk KUnit runs and complete Maple Tree
  self-tests passed.

Generic radix trees
  Status: ``migrated``.

  Evidence: :doc:`native-genradix`.

  Rust directly defines the six public non-GPL entry points for sparse
  lookup, allocation, forward and reverse iteration, preallocation, and
  release. It owns root growth, tagged-root publication retries, traversal,
  iterator advancement, and recursive release. The native C object retains
  only export metadata. C also retains node allocation, tagged-root and node
  layouts, atomic root and child publication, root exchange, and the inline
  type-aware API. C fallback configurations retain the complete algorithms.

B+Tree operations
  Status: ``migrated``.

  Evidence: :doc:`native-btree-entry`.

  Rust directly defines the nine public GPL entry points for lookup, update,
  last-entry selection, predecessor traversal, insertion, removal, merge,
  and visitor traversal. Rust owns every traversal, update, structural,
  merge, callback, and reclamation decision. The native C object retains node
  and geometry layouts, typed visitor callback adapters, mempool allocation
  and release, lifecycle initialization, and geometry objects. C fallback
  configurations retain the complete algorithms.

Kernel FIFO operations
  Status: ``migrated``.

  Evidence: :doc:`native-kfifo`.

  Rust directly defines the twenty public out-of-line FIFO entry points and
  owns ring copying, wrap-around, record framing, user-copy accounting, DMA
  segment preparation, and index commitment. The native C object retains
  allocation, initialization, user-access and barrier helpers, scatterlist
  mutation, and ordinary non-GPL export metadata. C fallback configurations
  retain the complete algorithms.

Timer queue operations
  Status: ``migrated``.

  Evidence: :doc:`timerqueue`.

  Rust directly defines the four public GPL insertion, deletion, successor,
  and linked-tree insertion entry points. Rust owns ordered search, intrusive
  node linking, cached-leftmost maintenance, deletion, successor lookup, and
  linked-chain insertion. C retains the intrusive timer queue layout, inline
  accessors, red-black tree primitives, and the complete fallback.

High-resolution timer state machine and public entries
  Status: ``migrated``.

  Evidence: :doc:`hrtimer`.

  Rust directly defines thirteen ordinary GPL entry points for setup, callback
  update, active and remaining queries, clock sampling, startup, sleeper
  lifecycle, cancellation, and forwarding. It also defines the unexported
  next-event, interrupt, queue-run, and deferred-rearm entries. With debug
  objects Rust defines ``destroy_hrtimer_on_stack()``, and on PREEMPT_RT it
  defines ``hrtimer_cancel_wait_running()``. Rust owns every startup, enqueue,
  expiry, interrupt, rearm, migration, cancellation, forwarding, and sleeper
  decision. The native C object does not define these entry points. C retains
  the shared timer layouts, raw locks, debug objects, tracepoints, time
  sampling, opaque clock event fields, task state, callback invocation, and
  timekeeping bridges. C fallback configurations retain the complete
  implementation.

Interval timer state machine and entry points
  Status: ``migrated``.

  Evidence: :doc:`itimer`.

  Rust directly defines ``itimer_get()``, ``itimer_set()``,
  ``posixtimer_rearm_itimer()``, and ``alarm_setitimer()``. Rust owns interval
  validation, real and CPU timer replacement, real timer cancellation retry,
  remaining-time normalization, period updates, periodic rearm selection,
  and alarm rounding. The native C object compiles out its complete get, set,
  rearm, and alarm implementations. C retains user copies, the signal lock,
  hrtimer operations, CPU-time sampling, tracepoints, and SIGALRM delivery.
  C fallback and C-only configurations retain the complete C state machine.
  Seven native, fallback, C-only, KASAN, lockdep, PREEMPT_RT, and debug-object
  configurations passed ten boot runs at 6 of 6 cases.

CPU-priority scheduler state machine and entry points
  Status: ``migrated``.

  Evidence: :doc:`cpupri`.

  Rust directly defines ``cpupri_find()``, ``cpupri_find_fitness()``, and
  ``cpupri_set()``. Rust owns scheduler priority conversion, vector search,
  affinity and active-CPU filtering, capacity fitness selection,
  priority-first fallback, and ordered transitions between vectors. C retains
  the shared layout, cpumask storage and primitives, atomics and barriers,
  run-queue locking, initialization, cleanup, and invalid-priority
  diagnostics. C fallback and C-only configurations retain the complete C
  state machine. Twelve native, fallback, C-only, KASAN, lockdep, PREEMPT_RT,
  and off-stack cpumask boots passed all applicable cases, including three
  consecutive native four-vCPU runs.

Boundary-retained inventory
---------------------------

Shared type layouts
  Status: ``boundary-retained``.

  Owner: ``include/linux/llist.h``.

  Reason: Both languages access the same intrusive structures.

  Test: Native, fallback, and C-only KUnit runs exercise the same layouts.

  Risk: A layout change can invalidate one language's field access.

  Exit condition: All consumers use a Rust-owned type and the C layout is no
  longer part of the shared interface.

Public ABI
  Status: ``boundary-retained``.

  Owner: The public symbols defined in ``include/linux/llist.h``,
  ``include/linux/klist.h``, ``include/linux/kref.h``,
  ``include/linux/lockref.h``,
  ``include/linux/kobject.h``, ``include/linux/rcuwait.h``,
  ``include/linux/interval_tree.h``,
  ``include/linux/rbtree.h``, ``include/linux/rbtree_augmented.h``,
  ``include/linux/plist.h``, ``include/linux/list_sort.h``,
  ``include/linux/list_lru.h``, ``include/linux/generic-radix-tree.h``,
  ``include/linux/btree.h``, ``include/linux/find.h``,
  ``include/linux/bitmap.h``, ``include/linux/maple_tree.h``,
  ``include/linux/kfifo.h``, ``include/linux/completion.h``,
  ``include/linux/task_work.h``, ``include/linux/irq_work.h``,
  ``include/linux/timerqueue.h``, ``include/linux/hrtimer.h``,
  ``include/linux/itimer-rust.h``, ``include/linux/cpupri.h``,
  ``include/linux/mutex.h``,
  ``include/linux/ww_mutex.h``, and ``include/linux/rwsem.h``.

  Reason: Existing callers and loadable modules use the stable C ABI.

  Test: KUnit calls the public symbols and module post-processing validates
  their exports.

  Risk: A signature or export-class change can break binary compatibility.

  Exit condition: A coordinated kernel-wide API change removes or replaces
  the public interface.

Architecture atomics
  Status: ``boundary-retained``.

  Owner: ``rust/kernel/sync/atomic`` and the architecture atomic layer.

  Reason: LKMM operations depend on architecture primitives.

  Test: SMP1 and SMP4 runs include native, KASAN, lockdep, and PREEMPT_RT.

  Risk: Incorrect ordering can cause lost nodes or stale successor reads.

  Exit condition: A replacement atomic API preserves the complete LKMM
  contract and all architecture implementations.

Timer queue layout and inline operations
  Status: ``boundary-retained``.

  Owner: ``include/linux/timerqueue.h`` and
  ``include/linux/timerqueue_types.h``.

  Reason: C and Rust access the same intrusive red-black node, cached root,
  linked-node chain, expiration field, and inline access operations.

  Test: Native, fallback, and C-only KUnit runs exercise signed ordering,
  duplicate expirations, cached-leftmost maintenance, successor traversal,
  and linked-chain insertion and removal.

  Risk: A layout or inline-contract change can make Rust update the wrong
  cached root or expose a node outside the timer queue ordering rules.

  Exit condition: Rust-owned timer queue types and inline interfaces replace
  the shared C representation.

Timer queue tree primitives
  Status: ``boundary-retained``.

  Owner: ``include/linux/rbtree.h``, ``lib/rbtree.c``, and the Rust bindings.

  Reason: Intrusive linking, rotations, color repair, erasure, and successor
  lookup retain the shared red-black tree layout and inlining contracts.

  Test: Native and fallback runs execute timer queues with both the Rust and C
  red-black tree implementations.

  Risk: A primitive contract change can corrupt ordering or detach a timer
  from the cached leftmost path.

  Exit condition: Rust owns compatible timer queue tree primitives without a
  shared C call boundary.

High-resolution timer shared entry boundary
  Status: ``boundary-retained``.

  Owner: ``include/linux/hrtimer.h``, ``include/linux/hrtimer_types.h``,
  ``include/linux/hrtimer_bases.h``, ``include/linux/hrtimer-rust.h``,
  ``kernel/time/hrtimer.c``, and ``rust/helpers``.

  Reason: C and Rust share timer layouts, raw CPU and timer base locks,
  debug-object state, tracepoints, time sampling, task state, callback
  invocation, opaque clock event device fields, and timekeeping, sleep,
  resume, and CPU hotplug bridges. The ``hrtimer_resolution`` data export also
  remains in C.

  Test: Native, fallback, C-only, KASAN, lockdep, debug-object, and
  PREEMPT_RT builds cover all entry-point configurations. SMP1 and SMP4 KUnit
  runs exercise setup, startup, expiration, interrupt, rearm, migration,
  cancellation, forwarding, and sleeper state. Native, fallback, and C-only
  external GPL modules link all ordinary exports.

  Risk: A layout, lock, tracepoint, clock sample, task-state, or opaque device
  mismatch can corrupt queue state or deadlock a timer interrupt.

  Exit condition: Rust owns compatible timer types, locks, diagnostics, clock
  event access, task integration, and timekeeping bridges without shared C
  entry trampolines.

Interval timer shared entry boundary
  Status: ``boundary-retained``.

  Owner: ``include/linux/itimer-rust.h``, ``kernel/time/itimer.c``,
  ``rust/kernel/itimer_core.rs``, and ``rust/helpers``.

  Reason: C and Rust share task and signal layouts, the signal lock and
  interrupt-state transition, hrtimer timers, CPU-time sampling, tracepoint
  delivery, user copies, and SIGALRM delivery. The system call adapters and
  ``it_real_fn()`` remain in C.

  Test: Native, fallback, C-only, KASAN, lockdep, PREEMPT_RT, and debug-object
  configurations passed ten boot runs at 6 of 6 cases. The runs cover invalid
  clocks, real and CPU timer lifecycles, repeated replacement, periodic
  rearm, and alarm rounding.

  Risk: A task, signal, lock, hrtimer, CPU-time, or tracepoint mismatch can
  corrupt timer state, report the wrong remaining time, or deadlock against
  signal delivery.

  Exit condition: Rust owns compatible signal locking, task and signal
  storage, hrtimer operations, CPU-time sampling, tracepoints, user copies,
  and signal delivery without shared C entry helpers.

CPU-priority scheduler shared entry boundary
  Status: ``boundary-retained``.

  Owner: ``include/linux/cpupri.h``, ``include/linux/cpupri-rust.h``,
  ``kernel/sched/cpupri.c``, ``rust/kernel/cpupri_core.rs``, and
  ``rust/helpers``.

  Reason: C and Rust share the CPU-priority vector layout, task affinity and
  active-CPU masks, atomic counts, memory barriers, run-queue locking,
  initialization and cleanup, and invalid-priority diagnostics.

  Test: The native, fallback, and C-only configurations exercise the full
  priority search matrix, affinity and active-CPU filtering, fitness fallback,
  and concurrent vector transitions. Native KASAN, lockdep, PREEMPT_RT, and
  off-stack cpumask configurations cover the retained primitives.

  Risk: A layout, mask, count, barrier, or lock mismatch can miss a runnable
  CPU during migration or publish a vector that the scheduler cannot trust.

  Exit condition: Rust owns compatible cpumasks, atomic and barrier
  primitives, scheduler locking, initialization, diagnostics, and lifecycle
  operations without shared C helper entries.

Mutex scheduler, tracing, and locking-debug boundary
  Status: ``boundary-retained``.

  Owner: ``include/linux/mutex.h``, ``include/linux/mutex-rust.h``,
  ``include/linux/mutex_types.h``, ``include/linux/ww_mutex.h``,
  ``kernel/locking/mutex.c``, ``kernel/locking/ww_mutex.h``, and
  ``rust/kernel/mutex_core.rs``.

  Reason: Native Rust entries select lock state, but intrusive waiters,
  optimistic spinning, scheduler proxy execution, task state, scheduling,
  wake queues, lockdep and lock-stat delivery, tracing, debug validation,
  wait/wound ordering, and PREEMPT_RT use locking and scheduler internals
  that remain in C.

  Test: Native, fallback, and C-only KUnit runs exercise fast paths, signal
  cancellation, handoff, FIFO ownership, wait/wound blocking and trylock,
  nested classes, nest locks, I/O waits, and concurrent exclusion.

  Risk: An owner transition or debug-delivery mismatch can lose a waiter,
  violate wait/wound ordering, or make lockdep and lock-stat report a
  different acquisition or release site.

  Exit condition: Rust owns equivalent atomics, preemption, wake queue,
  scheduler proxy, lockdep, lock-stat, tracing, wait/wound, and
  context-analysis interfaces.

Read-write semaphore scheduler and locking-debug boundary
  Status: ``boundary-retained``.

  Owner: ``include/linux/rwsem.h``, ``include/linux/rwsem-rust.h``,
  ``kernel/locking/rwsem.c``, and ``rust/kernel/rwsem_core.rs``.

  Reason: Native Rust entries select lock state, but intrusive waiters,
  optimistic spinning, scheduling, wake queues, task state, hung-task
  accounting, lockdep and lock-stat delivery, and ``DEBUG_RWSEMS`` validation
  use locking and scheduler internals that remain in C.

  Test: Native, fallback, and C-only KUnit runs exercise reader and writer
  acquisition, stealing, handoff, signal cancellation, downgrade, nested
  classes, anonymous ownership, and concurrent exclusion.

  Risk: An ordering or debug-delivery mismatch can lose a waiter, break
  ownership tracking, or make lockdep and lock-stat report a different
  acquisition site.

  Exit condition: Rust owns equivalent preemption, wake queue, scheduler,
  lockdep, lock-stat, and context-analysis interfaces.

Queued read-write spinlock architecture and tracing boundary
  Status: ``boundary-retained``.

  Owner: ``include/asm-generic/qrwlock.h``, ``include/linux/rwlock-rust.h``,
  ``kernel/locking/qrwlock.c``, and ``rust/kernel/rwlock_core.rs``.

  Reason: Native Rust entries select and update lock state, but architecture
  atomics and memory ordering, the architecture wait queue lock,
  interrupt-context detection, contention tracepoints, preemption and lockdep
  wrappers, and ordinary exports use inline locking, tracing, and
  architecture interfaces that remain in C.

  Test: Native, fallback, and C-only KUnit runs exercise fast paths, trylock,
  reader and writer slow paths, writer preference, handoff, and concurrent
  reader/writer exclusion. Native KASAN, lockdep, and adjacent lock and wait
  suites pass.

  Risk: A count transition or memory-ordering mismatch can lose a waiting
  writer, admit a reader behind a waiting writer, or expose critical-section
  accesses before the corresponding acquire ordering.

  Exit condition: Rust owns equivalent architecture atomics, wait queue
  operations, interrupt-context checks, tracing, preemption, lockdep, and
  context-analysis interfaces.

Generic bitmap storage and inline interface
  Status: ``boundary-retained``.

  Owner: ``include/linux/bitmap.h`` and ``include/linux/find.h``.

  Reason: C callers and Rust share the ``unsigned long`` word layout, inline
  public names, constant-foldable accessors, and range contracts.

  Test: Native, fallback, and C-only runs exercise out-of-line bitmap
  operations and inline search accessors on partial and multiword bitmaps.

  Risk: A storage or range-contract change can make Rust read the wrong word
  or expose tail bits outside the logical size.

  Exit condition: Rust-owned bitmap accessors and inline interfaces replace
  the shared C representation.

Bitmap allocation and device-managed storage
  Status: ``boundary-retained``.

  Owner: ``lib/bitmap.c``.

  Reason: Bitmap allocation, NUMA placement, release, and device-managed
  storage depend on allocator and devres contracts.

  Test: Native, fallback, and C-only bitmap tests exercise allocated,
  zero-initialized, node-selected, and device-managed lifecycles.

  Risk: A lifetime or node-selection mismatch can leak storage or return
  memory for the wrong NUMA node.

  Exit condition: Rust owns bitmap allocation and device-managed storage with
  equivalent allocator and devres lifetime rules.

Bitmap parsing and formatting
  Status: ``boundary-retained``.

  Owner: ``lib/bitmap.c``.

  Reason: String parsing, list parsing, sequence-file formatting, and
  user-copy parsing depend on parser, printk, seq_file, and user-access
  interfaces.

  Test: Native, fallback, and C-only tests cover valid and invalid input,
  formatting, list parsing, and user-copy paths.

  Risk: An ownership or interface mismatch can accept malformed input, report
  the wrong range, or cross the user-kernel boundary incorrectly.

  Exit condition: Rust owns complete parsers and formatters with equivalent
  kernel interfaces and error contracts.

Generic bit-search export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/find_bit.c`` and ``rust/Makefile``.

  Reason: The sixteen Rust-defined searches retain ordinary non-GPL exports
  while generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records sixteen ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all sixteen names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Generic bitmap export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/bitmap.c`` and ``rust/Makefile``.

  Reason: Twenty-nine Rust-defined operations retain ordinary non-GPL
  exports, while ``bitmap_onto()`` and ``bitmap_fold()`` remain unexported.

  Test: ``vmlinux.symvers`` records twenty-nine ``EXPORT_SYMBOL`` entries, no
  entry for either unexported function, and the generated Rust GPL export
  header excludes the twenty-nine exported names.

  Risk: Duplicate exports or a class change can break module loading and
  binary compatibility.

  Exit condition: Rust export metadata supports the original non-GPL and
  unexported classes without C export entries.

Maple Tree node layout and protected access
  Status: ``boundary-retained``.

  Owner: ``include/linux/maple_tree.h``, ``lib/maple_tree.c``, and
  ``rust/kernel/maple_tree_core.rs``.

  Reason: Rust reads encoded roots, node types, pivots, and protected slots
  through C helpers that preserve Maple node layout and RCU access rules.

  Test: Native and fallback Maple Tree load KUnit runs exercise empty,
  direct-root, deep-node, zero-entry, maximum-index, and concurrent RCU
  lookup paths.

  Risk: A layout or protection change can make Rust select the wrong pivot,
  read an unstable slot, or miss a dead node.

  Exit condition: Rust owns the Maple node representation and equivalent
  protected-access contract.

Maple Tree traversal metadata and lockdep
  Status: ``boundary-retained``.

  Owner: ``lib/maple_tree.c``, ``include/linux/maple_tree_rust.h``, and
  ``rust/kernel/maple_tree_core.rs``.

  Reason: The node data end, protected slot access, lockdep initialization,
  and RCU bookkeeping depend on the shared Maple metadata layout.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  empty, direct-root, range metadata, zero-entry, maximum-index, and concurrent
  RCU paths.

  Risk: A metadata or initialization mismatch can report the wrong range or
  hide an invalid lock or RCU access.

  Exit condition: Rust owns equivalent Maple metadata decoding and lockdep
  initialization.

Maple Tree load diagnostics and metadata
  Status: ``boundary-retained``.

  Owner: ``lib/maple_tree.c`` and ``rust/Makefile``.

  Reason: Lockdep checks, ``ma_read`` tracing, and the ordinary
  ``EXPORT_SYMBOL`` class remain C boundaries.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs execute
  the load suite. ``Module.symvers`` records one ordinary export, and the
  generated Rust GPL export header excludes ``mtree_load``.

  Risk: A diagnostic or export-class mismatch can hide an RCU violation or
  break module compatibility.

  Exit condition: Rust diagnostics and export generation preserve the
  existing trace, lockdep, and non-GPL contracts.

Random set-bit primitive
  Status: ``boundary-retained``.

  Owner: ``include/linux/random.h`` and the Rust bindings.

  Reason: Rust resolves the selected set-bit ordinal, while
  ``__get_random_u32_below()`` remains the kernel random-number primitive.

  Test: Empty, singleton, random-membership, concurrent, KASAN, lockdep, and
  PREEMPT_RT runs exercise the random path.

  Risk: A primitive range or return-contract change can select an out-of-range
  ordinal.

  Exit condition: A Rust random abstraction preserves the primitive range
  contract and kernel entropy policy.

Lock-less list inline paths
  Status: ``boundary-retained``.

  Owner: ``include/linux/llist.h``.

  Reason: Header-only producer operations are part of the C container
  interface.

  Test: The concurrent producer and consumer KUnit case exercises these
  paths.

  Risk: Mixed atomic and non-atomic field access can introduce data races.

  Exit condition: All producers use a Rust-owned intrusive container API and
  the inline C interface is removed.

Klist initialization
  Status: ``boundary-retained``.

  Owner: ``lib/klist.c``.

  Reason: Spinlock initialization depends on architecture and lock debugging
  support.

  Test: Every klist KUnit case uses a C-initialized list, and lockdep runs the
  suite with expanded lock metadata.

  Risk: Incorrect initialization can leave lockdep state or architecture lock
  state invalid.

  Exit condition: A Rust abstraction provides equivalent lock initialization
  and debug behavior.

Blocking klist removal
  Status: ``boundary-retained``.

  Owner: ``lib/klist.c``.

  Reason: The operation registers a waiter and uses task state, scheduling,
  and wakeup primitives.

  Test: The blocking ``klist_remove()`` KUnit case runs in native, fallback,
  and C-only configurations.

  Risk: Incorrect publication or wake ordering can lose a wakeup or return
  before final release.

  Exit condition: Rust owns the complete waiter and task-wakeup lifecycle.

Klist primitive helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/klist.c``.

  Reason: The helpers preserve list debugging, refcount, spinlock, and
  interrupt-state semantics shared with C.

  Test: Native KUnit, KASAN, lockdep, and PREEMPT_RT runs exercise the helpers.

  Risk: A mismatched lock or reference primitive can break list lifetime or
  interrupt-state restoration.

  Exit condition: Approved Rust list, lock, and refcount abstractions cover the
  complete helper contract.

Refcount layout and saturation encoding
  Status: ``boundary-retained``.

  Owner: ``include/linux/refcount_types.h`` and
  ``include/linux/refcount.h``.

  Reason: C and Rust share the embedded atomic word, signed count domain,
  saturation constants, and saturation-event enumeration.

  Test: Six configurations passed SMP1 and SMP4 with seven of seven cases.
  The cases exercise ordinary transitions, limits, exact-zero changes,
  special decrements, and every saturation event.

  Risk: A layout or constant mismatch can corrupt the counter or misclassify
  a use-after-free, overflow, underflow, or leak.

  Exit condition: Rust owns an equivalent reference-count type and saturation
  encoding.

Refcount atomic, barrier, lock, and warning helpers
  Status: ``boundary-retained``.

  Owner: ``lib/refcount.c`` and the Rust bindings.

  Reason: Atomic reads and updates, acquire and release ordering, the
  control-dependency acquire barrier, mutex and spinlock operations, IRQ
  restoration, and warning sites retain architecture and scheduler contracts.

  Test: Native, KASAN, lockdep, and PREEMPT_RT passed SMP1 and SMP4 with
  seven of seven cases. The native SMP4 image also passed 25 adjacent cases.

  Risk: Incorrect ordering or primitive selection can lose a final reference,
  expose release-before-acquire, or return with the wrong lock state.

  Exit condition: Approved Rust atomics, barriers, locks, and diagnostics
  preserve the complete helper contracts.

Refcount export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/refcount.c`` and ``rust/Makefile``.

  Reason: All eleven native Rust public symbols use ordinary non-GPL exports
  while generated Rust exports use the GPL class.

  Test: Native exports eleven ``EXPORT_SYMBOL`` entries. Fallback and C-only
  retain six. Generated Rust export headers exclude all eleven names, and
  external modules linked against every exported entry.

  Risk: A duplicate export or class change can break non-GPL module
  compatibility.

  Exit condition: Rust export metadata supports the ordinary class without C
  export records.

Per-CPU reference layout and flags
  Status: ``boundary-retained``.

  Owner: ``include/linux/percpu-refcount.h``.

  Reason: C and Rust share the encoded per-CPU pointer, mode and dead flags,
  backing data object, count bias, callback ABI, and embedded RCU head.

  Test: Six configurations pass the seven-case lifecycle suite on SMP1 and
  SMP4, including concurrent mode switching.

  Risk: A flag, bias, layout, or callback mismatch can leak references, free
  live storage, or expose a partially switched counter.

  Exit condition: Rust owns an equivalent reference type, flag encoding, and
  callback layout.

Per-CPU reference fast paths and lifecycle helpers
  Status: ``boundary-retained``.

  Owner: ``include/linux/percpu-refcount.h`` and ``lib/percpu-refcount.c``.

  Reason: Lockless get, put, and live-tryget paths remain inline C. C also
  retains per-CPU and backing allocation, switch locking and waiting, RCU
  queueing, release-store publication, counter collection, and diagnostics.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT exercise
  both mode-switch directions, RCU confirmation, reinitialization,
  resurrection, and concurrent switching.

  Risk: A missing barrier, lock, or reference can lose the final release or
  retain a stale per-CPU mode.

  Exit condition: Approved Rust abstractions cover the complete per-CPU,
  atomic, RCU, locking, and diagnostic contract.

Lockref layout and initialization
  Status: ``boundary-retained``.

  Owner: ``include/linux/lockref.h``.

  Reason: The packed spinlock and signed count share an architecture-specific
  word. ``lockref_init()`` initializes architecture lock debugging state.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  the layout and initialization.

  Risk: A layout or initialization mismatch can make a packed update race with
  the embedded lock.

  Exit condition: A Rust abstraction owns the packed layout and equivalent
  lock-debugging initialization.

Lockref architecture helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/lockref.c``.

  Reason: Relaxed compare-exchange, packed-word decoding, and spinlock
  operations depend on architecture and lock implementation details.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs execute the
  fast and serialized paths.

  Risk: Incorrect ordering or packed-word handling can lose a reference or
  corrupt the embedded lock state.

  Exit condition: Approved Rust atomics and spinlock abstractions preserve the
  complete packed operation contract.

Lockref export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/lockref.c``.

  Reason: The existing public API is exported as non-GPL ABI, while Rust GPL
  exports are generated automatically.

  Test: ``Module.symvers`` records all six public symbols as
  ``EXPORT_SYMBOL`` and the native object defines no lockref function body.

  Risk: Duplicate exports or a class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Kref layout and initializer
  Status: ``boundary-retained``.

  Owner: ``include/linux/kref.h``.

  Reason: The public API wraps an embedded ``refcount_t``, and
  ``KREF_INIT()`` is a compile-time initializer used by shared C objects.

  Test: Native, fallback, and C-only KUnit runs use the same object layout and
  initializer.

  Risk: A layout or initializer mismatch can expose an invalid initial count
  or invalidate the embedded refcount.

  Exit condition: Rust owns the shared object layout and equivalent static
  initialization.

Kref lock and callback helpers
  Status: ``boundary-retained``.

  Owner: The lock, refcount, and release callback interfaces.

  Reason: Mutex and spinlock acquisition, architecture atomics, and subsystem
  release callbacks retain their existing kernel contracts.

  Test: Native KUnit exercises plain, mutex, and spinlock final-reference
  release, plus concurrent reference traffic.

  Risk: A lock or callback mismatch can release with the wrong lock state or
  invoke a release function more than once.

  Exit condition: Approved Rust lock, atomic, and callback abstractions
  preserve the complete contracts.

Kref export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/kref.c``.

  Reason: The public lifecycle functions need ordinary non-GPL module
  visibility, while Rust entry points do not generate ordinary exports.

  Test: ``Module.symvers`` records all seven public symbols as
  ``EXPORT_SYMBOL`` and the native object defines no kref function body.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the ordinary class without C
  export entries.

Simple wait queue layout and wait macros
  Status: ``boundary-retained``.

  Owner: ``include/linux/swait.h``.

  Reason: C and Rust share the raw lock, queue, wait, task pointer, and
  intrusive list layouts. The public macros also expand the caller-provided C
  condition and scheduling command after Rust prepares the wait.

  Test: Native, fallback, and C-only KUnit boots exercise queue
  initialization, stack wait entries, signal rejection, and FIFO wakeup.

  Risk: A layout or macro expansion mismatch can miss a wakeup, corrupt queue
  membership, or evaluate the condition with the wrong task state.

  Exit condition: Rust owns an equivalent queue and wait layout and a
  condition-callback interface.

Simple wait raw lock, task, and list helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/swait.c`` and the Rust bindings.

  Reason: Raw lock acquisition, lock dependency classes, interrupt state
  restoration, current task access, task-state stores, signal queries, task
  wakeup, and intrusive list mutation retain scheduler, lock, and
  architecture contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT boots exercise queue
  lifecycle, bounded wake-all, FIFO wake, signal, and locked wake-all paths.

  Risk: Incorrect lock, barrier, task-state, or list ordering can lose a
  wakeup, leave a stale waiter, or restore the wrong interrupt state.

  Exit condition: Approved Rust lock, task, signal, scheduler, and list
  abstractions preserve the complete helper contracts.

Simple wait export metadata
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/swait.c`` and ``rust/Makefile``.

  Reason: Seven public symbols retain ordinary non-GPL exports, while
  ``swake_up_all_locked()``, ``__prepare_to_swait()``, and ``__finish_swait()``
  remain unexported and generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records seven ``EXPORT_SYMBOL`` entries, no entry
  for the three internal names, and the generated Rust export lists exclude
  all ten names.

  Risk: A duplicate export or class change can expose an internal interface
  or break module compatibility.

  Exit condition: Rust export metadata supports the ordinary and unexported
  classes without C export records.

Wait queue layout and construction macros
  Status: ``boundary-retained``.

  Owner: ``include/linux/wait.h``.

  Reason: C and Rust share the wait queue head lock and list, wait entry,
  task pointer, callback, and flag layouts. C construction and initialization
  macros embed those layouts and lock classes in callers.

  Test: Six configurations exercise entry insertion, priority ordering,
  stack wait entries, task wakeup, timeout, signal, and finish paths.

  Risk: A layout or initializer mismatch can corrupt queue membership, select
  the wrong callback, or miss a wakeup.

  Exit condition: Rust owns equivalent queue and entry layouts and construction
  interfaces.

Wait queue locking, scheduler, and signal helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/wait.c``, ``include/linux/wait-rust.h``, and
  ``include/linux/sched.h``.

  Reason: Raw lock and interrupt restoration, lockdep assertions, intrusive
  list mutation, task-state stores, SMP barriers, signal predicates,
  scheduling with a released queue lock, task wakeup, scheduler wake flags,
  poll key construction, and timeout operations retain scheduler, signal,
  poll, architecture, and lock contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT boots exercise normal,
  exclusive, priority, keyed, synchronized, current-CPU, locked, signal,
  timeout, autoremove, woken, and POLLFREE paths.

  Risk: An incorrect lock, barrier, signal, task-state, or list operation can
  lose a wakeup, wake an ineligible task, or corrupt a wait entry.

  Exit condition: Approved Rust lock, scheduler, signal, poll, and intrusive
  list abstractions preserve the complete helper contracts.

Wait queue export metadata
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/wait.c`` and ``rust/Makefile``.

  Reason: Fifteen production symbols retain ordinary non-GPL exports, seven
  retain GPL exports, and two remain unexported. C emits the ordinary records
  while Rust automatic generation emits only the GPL class.

  Test: ``vmlinux.symvers`` records exactly 15 ordinary entries, seven GPL
  entries, and no entry for the two internal names. Generated Rust export
  lists omit every ordinary and unexported name.

  Risk: A duplicate export or class change can expose an internal interface
  or break module compatibility.

  Exit condition: Rust export metadata supports the ordinary and unexported
  classes without C export records.

Bit wait layout and shared table
  Status: ``boundary-retained``.

  Owner: ``include/linux/wait_bit.h`` and ``kernel/sched/wait_bit.c``.

  Reason: C and Rust share the key, queue entry, wait entry, hashed table, and
  initialization layout. Callers also construct stack entries with C macros.

  Test: Six configurations exercise bit queue selection, variable queue
  selection, stack entries, timeouts, signals, and scheduler-backed wakeups.

  Risk: A layout, hash, or initialization mismatch can select the wrong queue,
  miss a waiter, or corrupt stack queue links.

  Exit condition: Rust owns an equivalent key, entry, table, and construction
  interface.

Bit wait architecture and scheduler helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/wait_bit.c``, ``include/linux/wait-bit-rust.h``, and
  scheduler, signal, and time interfaces.

  Reason: Shared table storage, architecture-sized hashing, ordered and
  unordered bit tests, test-and-set, current-task and jiffies reads, schedule,
  I/O schedule, timeout schedule, signal state, and wrapping time comparison
  retain architecture and scheduler contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT boots exercise normal,
  locked, timed, signal, bit callback, variable callback, and scheduler paths.

  Risk: Incorrect bit ordering, time comparison, signal handling, or scheduler
  selection can lose a wakeup, return a wrong action result, or violate bit
  lock ownership.

  Exit condition: Approved Rust architecture, scheduler, signal, and time
  abstractions preserve the complete helper contracts.

Bit wait export metadata
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/wait_bit.c`` and ``rust/Makefile``.

  Reason: Thirteen production symbols retain ordinary non-GPL exports, two
  retain GPL exports, and two internal names remain unexported. C emits the
  ordinary records while Rust automatic generation emits only the GPL class.

  Test: ``Module.symvers`` records exactly 13 ordinary entries, two GPL
  entries, and no entry for either internal name. Generated Rust export lists
  omit every ordinary and unexported name.

  Risk: A duplicate export or class change can expose an internal callback or
  break module compatibility.

  Exit condition: Rust export metadata supports the ordinary and unexported
  classes without C export records.

RCU wait layout and wait macros
  Status: ``boundary-retained``.

  Owner: ``include/linux/types.h`` and ``include/linux/rcuwait.h``.

  Reason: The shared object contains an RCU-protected task pointer, and the
  public macros must expand the caller-provided C condition after Rust sets
  the task state.

  Test: Native, fallback, and C-only KUnit boots exercise initialization,
  short waits, finite timeouts, signal interruption, and repeated handoffs.

  Risk: A layout or macro expansion mismatch can miss a wakeup, evaluate the
  condition in the wrong task state, or leave a waiter published.

  Exit condition: Rust owns an equivalent condition-callback interface and
  the shared object layout.

RCU wait scheduler and RCU helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/rcuwait.c`` and the RCU, task, scheduler, and signal
  interfaces.

  Reason: Pointer publication, RCU read locking, task-state stores, signal
  queries, scheduling, timeout scheduling, and task wakeup retain their
  kernel and architecture contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT boots exercise wake,
  timeout, signal, and handoff paths.

  Risk: Incorrect ordering can expose a stale waiter or lose a wakeup.

  Exit condition: Approved Rust abstractions preserve the complete RCU,
  scheduler, signal, and wakeup contracts.

RCU wait export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/rcuwait.c``, ``kernel/exit.c``,
  ``kernel/rcu/update.c``, and ``rust/Makefile``.

  Reason: Initialization, activity, and publication retain ordinary exports,
  while completion, wake, and macro helpers retain GPL exports.

  Test: ``Module.symvers`` records the three ordinary and five GPL entries,
  and a temporary external module resolves all eight symbols.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports both original classes without
  C export entries.

Decrement-and-lock primitive helpers
  Status: ``boundary-retained``.

  Owner: ``lib/dec_and_lock.c`` and ``rust/helpers`` bindings.

  Reason: Architecture atomics, spinlocks, raw spinlocks, mutexes, and IRQ
  state operations retain their existing architecture and lock-debug
  contracts.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  every protocol variant and concurrent final-transition selection.

  Risk: A primitive mismatch can lose the unique zero transition, leave a
  lock held, or restore the wrong interrupt state.

  Exit condition: Rust-owned atomic, lock, mutex, and interrupt abstractions
  preserve the complete primitive contracts.

Decrement-and-lock export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/dec_and_lock.c``, ``kernel/locking/mutex.c``, and
  ``rust/Makefile``.

  Reason: All five public symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``Module.symvers`` records five ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all five names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Completion layout and initialization
  Status: ``boundary-retained``.

  Owner: ``include/linux/completion.h``, ``include/linux/swait.h``, and
  ``rust/kernel/completion_core.rs``.

  Reason: C and Rust access the same completion count, embedded simple wait
  queue, wait entry, and inline construction interface.

  Test: Native, fallback, and C-only KUnit runs exercise initialization,
  reinitialization, stack wait entries, and queued waiter membership.

  Risk: A layout or initialization mismatch can corrupt the count, wait list,
  or lock state shared by both languages.

  Exit condition: Rust-owned completion and simple wait types replace the
  shared C layouts and inline construction interface.

Completion primitive helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/completion.c`` and the Rust bindings.

  Reason: Raw spinlock operations, task state, signal queries, scheduler
  timeout calls, completion memory-order hooks, simple-wait queue operations,
  and the PREEMPT_RT lock assertion retain scheduler and architecture
  contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs exercise every
  wait result, FIFO wake, broadcast wake, and concurrent producer path.

  Risk: A primitive mismatch can lose a wakeup, select the wrong timeout or
  signal result, or leave task state or locks inconsistent.

  Exit condition: Approved Rust task, signal, scheduler, lock, and simple-wait
  abstractions preserve the complete helper contracts.

Completion export metadata
  Status: ``boundary-retained``.

  Owner: ``kernel/sched/completion.c`` and ``rust/Makefile``.

  Reason: Thirteen public symbols retain ordinary non-GPL exports, while
  ``complete_on_current_cpu()`` remains unexported and generated Rust exports
  use the GPL class.

  Test: ``Module.symvers`` records thirteen ``EXPORT_SYMBOL`` entries, no
  entry for ``complete_on_current_cpu``, and the generated Rust GPL export
  header excludes all fourteen names.

  Risk: A duplicate export or class change can expose an internal interface
  or break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL and
  unexported classes without C export records.

Semaphore layout and initialization
  Status: ``boundary-retained``.

  Owner: ``include/linux/semaphore.h``.

  Reason: C callers and Rust share the raw lock, count, first-waiter pointer,
  optional last-holder field, and lockdep initialization interface.

  Test: Native, fallback, and C-only KUnit runs initialize semaphores on the
  stack and exercise count, waiter, and holder fields directly.

  Risk: A layout or initialization mismatch can corrupt the waiter list or
  leave the raw lock without its lockdep state.

  Exit condition: Rust-owned semaphore and waiter representations replace the
  shared C layout and initialization interface.

Semaphore locking, scheduling, and diagnostics
  Status: ``boundary-retained``.

  Owner: ``kernel/locking/semaphore.c``, the Rust bindings, and the scheduler
  and tracing interfaces.

  Reason: Raw spinlocks, interrupt restoration, task state, signal queries,
  scheduling, intrusive list operations, wake queues, hung-task accounting,
  tracepoints, and the sleep-context assertion retain scheduler,
  architecture, and tracing contracts.

  Test: Native, KASAN, lockdep, and PREEMPT_RT runs cover fast acquisition,
  blocked waits, signals, timeouts, FIFO handoff, and concurrent exclusion.

  Risk: A primitive or ordering mismatch can lose a wakeup, select the wrong
  result, leave a waiter queued, or restore the wrong interrupt state.

  Exit condition: Approved Rust lock, task, signal, scheduler, tracing, and
  wake-queue abstractions preserve the complete helper contracts.

Semaphore export metadata
  Status: ``boundary-retained``.

  Owner: ``kernel/locking/semaphore.c`` and ``rust/Makefile``.

  Reason: All six public symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``Module.symvers`` records six ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all six names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Task work layout and callback ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/task_work.h``, ``include/linux/sched.h``, and
  ``rust/kernel/task_work.rs``.

  Reason: C and Rust share the task work-list head, callback links and
  function, notification modes, exit sentinel, and callback calling
  convention.

  Test: Native, fallback, and C-only KUnit runs exercise enqueue,
  cancellation, exit sealing, reentrant draining, and callback invocation.

  Risk: A layout or callback mismatch can corrupt the task work list, invoke
  a callback with the wrong ABI, or publish the exit sentinel incorrectly.

  Exit condition: Rust-owned task and callback types replace the shared
  task-work representation and callback interface.

Task work primitive helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/task_work.c`` and the Rust bindings.

  Reason: Task field access, the task PI lock, one-copy atomic reads,
  compare-exchange, notification flags, diagnostics, IRQ work, and scheduler
  progress retain task and architecture contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs exercise
  concurrent enqueue and cancellation, all notification modes, and
  reentrant callback draining.

  Risk: A primitive mismatch can lose a callback, expose it before
  notification, or race cancellation with detachment.

  Exit condition: Approved Rust task, lock, atomic, notification, and
  scheduler abstractions preserve the complete helper contracts.

IRQ work layout and callback ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/irq_work.h``,
  ``include/linux/irq_work_types.h``, and ``rust/kernel/irq_work.rs``.

  Reason: C and Rust share the IRQ work flags, intrusive lockless-list node,
  callback pointer, wait object, initialization helpers, and callback calling
  convention.

  Test: Native, fallback, and C-only KUnit runs exercise claim, duplicate
  suppression, reentrant callbacks, and state transitions on the shared
  layout.

  Risk: A layout or callback mismatch can corrupt queue links, lose a
  callback, or clear the wrong state bit.

  Exit condition: Rust-owned IRQ work and callback types replace the shared
  representation and callback interface.

IRQ work delivery and worker boundaries
  Status: ``boundary-retained``.

  Owner: ``kernel/irq_work.c``, architecture IRQ entry code, and the Rust
  bindings.

  Reason: Per-CPU raised and lazy lists, preemption and current-CPU access,
  IPI and call-single delivery, hotplug, the PREEMPT_RT worker lifecycle,
  KASAN, lockdep, synchronization context checks, barriers, RCU, ``rcuwait``,
  and CPU relax operations retain architecture and scheduler contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs exercise local,
  remote, lazy, reentrant, contended-sync, and concurrent-producer paths.

  Risk: A delivery or lifetime mismatch can execute work on the wrong CPU,
  miss a wakeup, or access an item after release.

  Exit condition: Approved Rust per-CPU, interrupt-delivery, worker, atomic,
  diagnostic, and wait abstractions preserve the complete contracts.

IRQ work export policy
  Status: ``boundary-retained``.

  Owner: ``rust/Makefile`` and ``kernel/irq_work.c``.

  Reason: ``irq_work_queue()``, ``irq_work_run()``, and ``irq_work_sync()``
  retain their historical GPL exports. The other four public entries and the
  internal ``rust_irq_work_run_lazy()`` bridge have no module export.

  Test: ``Module.symvers`` records only the three GPL entries in native
  builds, and the generated Rust GPL export header excludes the five
  unexported names.

  Risk: An automatic export can expand the module ABI or expose an internal
  PREEMPT_RT bridge.

  Exit condition: Rust export generation supports the original GPL and
  unexported classes without C export records or the skip list.

IRQ descriptor, action, and handler ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/irq.h``, ``include/linux/irqdesc.h``,
  ``kernel/irq/internals.h``, and ``rust/kernel/irq_event.rs``.

  Reason: C and Rust share descriptor state, intrusive action lists, action
  flags, handler and thread callbacks, return values, and thread-state fields.

  Test: Native, fallback, and C-only IRQ KUnit runs exercise action traversal,
  result aggregation, threaded wakeup, duplicate suppression, and descriptor
  lifecycle transitions on the shared layouts.

  Risk: A layout, callback, or return-value mismatch can dispatch the wrong
  action, corrupt descriptor state, or lose a threaded wakeup.

  Exit condition: Rust-owned descriptor and action types replace the shared
  IRQ subsystem representation and callback interface.

IRQ event primitive helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/irq/handle.c``, tracing and diagnostics infrastructure, and
  the Rust bindings.

  Reason: Handler tracing, duration diagnostics, descriptor locking,
  interrupt-state changes, thread flags, scheduler wakeup, randomness, and
  descriptor state access retain scheduler, tracing, and architecture
  contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs exercise
  hardware callbacks, threaded wakeup, oneshot accounting, unknown results,
  and spurious-interrupt paths.

  Risk: A helper mismatch can invoke a handler in the wrong context, miss a
  trace or diagnostic, or leave descriptor and thread state inconsistent.

  Exit condition: Approved Rust tracing, locking, interrupt, thread, wake,
  randomness, and descriptor state abstractions preserve the helper contracts.

IRQ event unexported entry policy
  Status: ``boundary-retained``.

  Owner: ``rust/Makefile`` and ``kernel/irq/internals.h``.

  Reason: The four production entries are internal kernel interfaces with no
  module export. Automatic Rust exports are GPL by default.

  Test: All six ``Module.symvers`` files omit the four names, and the generated
  Rust export header excludes them.

  Risk: An automatic export can unintentionally expose internal IRQ dispatch
  entries to modules.

  Exit condition: Rust export generation supports internal unexported entry
  points without the skip list.

IRQ resend descriptor and queue ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/irq.h``, ``kernel/irq/internals.h``, and
  ``rust/kernel/irq_resend.rs``.

  Reason: C and Rust share descriptor state, the embedded resend hlist node,
  the global resend queue, the raw queue lock, tasklet storage, and flow
  handler callback ABI.

  Test: Native, fallback, and C-only IRQ KUnit runs cover level retirement,
  replay exclusion, hardware and hierarchy retrigger, nested-parent
  deduplication, software drain order, injection, and rejected resend paths.

  Risk: A layout, lock, queue, or callback mismatch can lose a resend, enqueue
  one descriptor twice, or dispatch the wrong flow handler.

  Exit condition: Rust-owned descriptor, queue, tasklet, and callback
  representations replace the shared IRQ subsystem interfaces.

IRQ resend primitive helpers
  Status: ``boundary-retained``.

  Owner: ``kernel/irq/resend.c``, irqchip callbacks, and the Rust bindings.

  Reason: Descriptor field access, raw lock transitions, hlist operations,
  tasklet scheduling, irqchip retrigger callbacks, IRQ-number lookup, and
  flow-handler invocation retain architecture, interrupt, and scheduler
  contracts.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs exercise
  interrupt-disabled drain, hardware retrigger, software fallback, and
  nested-parent paths.

  Risk: A helper mismatch can call a flow handler in the wrong context, break
  queue serialization, or misreport hardware retrigger success.

  Exit condition: Approved Rust interrupt, lock, hlist, tasklet, irqchip, and
  callback abstractions preserve the helper contracts.

IRQ resend unexported entry policy
  Status: ``boundary-retained``.

  Owner: ``rust/Makefile`` and ``kernel/irq/internals.h``.

  Reason: The four production entries are internal kernel interfaces with no
  module export. Automatic Rust exports are GPL by default.

  Test: All six ``Module.symvers`` files omit the four names, and the generated
  Rust export headers exclude them.

  Risk: An automatic export can unintentionally expose internal resend
  entries to modules.

  Exit condition: Rust export generation supports internal unexported entry
  points without the skip list.

Kobject layout and state
  Status: ``boundary-retained``.

  Owner: ``include/linux/kobject.h``.

  Reason: C callers and retained sysfs and kset paths share the intrusive
  layout, registration bitfields, delayed-work member, and type pointer.

  Test: Native, fallback, C-only, KASAN, lockdep, and PREEMPT_RT runs exercise
  initialization, reinitialization, and final cleanup.

  Risk: A layout or state mismatch can corrupt registration state or release
  a live object.

  Exit condition: A Rust-owned kobject type and safe state interface replace
  direct C field access.

Kobject diagnostics
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c``.

  Reason: Invalid-pointer, missing-type, reinitialization, and uninitialized
  reference diagnostics use kernel log and stack tracing facilities shared
  with C callers.

  Test: The invalid-initialization KUnit case runs in native, fallback, and
  C-only configurations.

  Risk: A diagnostic mismatch can hide a recoverable initialization error or
  change historical behavior.

  Exit condition: Rust diagnostics preserve the formatting, stack trace, and
  recoverability contracts.

Kobject release bridge
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c`` and ``include/linux/kobject-rust.h``.

  Reason: Rust dispatches the final reference through
  ``rust_kobject_release()``. C then owns delayed work, ktype release,
  parent-reference removal, name cleanup, and sysfs cleanup.

  Test: Final-reference KUnit cases pass under native SMP1/SMP4, KASAN,
  lockdep, and PREEMPT_RT.

  Risk: A callback mismatch can skip cleanup, double release an object, or
  break the delayed-release path.

  Exit condition: Rust owns the complete cleanup and delayed-release
  lifecycle with equivalent workqueue and callback primitives.

Kobject export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/kobject.c``.

  Reason: The four public lifecycle operations retain a non-GPL export class
  while generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records all four public symbols as
  ``EXPORT_SYMBOL``, and the generated Rust GPL export header excludes them.

  Risk: Duplicate exports or a class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

Interval tree layout and iterator state
  Status: ``boundary-retained``.

  Owner: ``include/linux/interval_tree.h``.

  Reason: C callers share the intrusive interval node, cached red-black root,
  augmented subtree endpoint, and used or hole span iterator layout.

  Test: Native, fallback, and C-only KUnit runs exercise ordering, subtree
  maxima, overlap traversal, and the complete span state machine.

  Risk: A layout or endpoint-type change can invalidate direct Rust access or
  alter inclusive range behavior.

  Exit condition: A Rust-owned interval tree type and iterator API replace the
  shared C layouts.

Augmented red-black tree helpers
  Status: ``boundary-retained``.

  Owner: ``lib/interval_tree.c`` and ``include/linux/interval-tree-rust.h``.

  Reason: Intrusive node linking, cached-leftmost insertion, and cached
  augmented erase depend on shared red-black tree layouts and callbacks.

  Test: Interval tree KUnit runs cover every public entry, and the adjacent
  Rust red-black tree suite passes four of four cases.

  Risk: A callback or rotation mismatch can corrupt subtree maxima or cached
  tree state during insertion or removal.

  Exit condition: Rust owns complete intrusive interval tree integration
  without C link or erase callbacks.

Red-black tree layout and color encoding
  Status: ``boundary-retained``.

  Owner: ``include/linux/rbtree_types.h`` and ``include/linux/rbtree.h``.

  Reason: C and Rust access the same intrusive node, cached root, linked-node,
  parent pointer, and color fields.

  Test: Native, fallback, and C-only KUnit runs check color invariants,
  black-height, traversal order, linked order, augmentation, and replacement.

  Risk: A layout or parent-color encoding change can corrupt both the tree and
  lockless lookup assumptions.

  Exit condition: A Rust-owned tree and node representation replaces the
  shared C layouts.

Red-black tree export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/rbtree.c`` and ``rust/Makefile``.

  Reason: Ten historical symbols use non-GPL exports while
  ``rb_erase_linked()`` and the internal augmented erase bridge use GPL
  exports.

  Test: ``vmlinux.symvers`` records the ten non-GPL exports and two GPL
  exports, and the generated Rust GPL header excludes the ten historical
  non-GPL names.

  Risk: A duplicate export or class change can break module loading and binary
  compatibility.

  Exit condition: Rust export metadata supports all original export classes
  without C metadata.

Augmented red-black erase dispatch
  Status: ``boundary-retained``.

  Owner: ``include/linux/rbtree_augmented.h`` and
  ``rust/kernel/rbtree/native.rs``.

  Reason: The inline augmented erase interface coordinates callback
  propagation, node replacement, and rebalancing as one operation.

  Test: Native, fallback, and C-only KUnit runs exercise augmented insertion,
  removal, and rotations.

  Risk: A callback or rebalance mismatch can corrupt subtree aggregates or
  detach the wrong child.

  Exit condition: Rust owns the complete augmented erase interface and no
  internal C-to-Rust bridge remains.

Priority-list layout and list primitives
  Status: ``boundary-retained``.

  Owner: ``include/linux/plist.h``, ``include/linux/plist_types.h``, and
  ``lib/plist.c``.

  Reason: C callers share the intrusive head, node, priority bucket, and
  common ``list_head`` representation.

  Test: Native, fallback, and C-only KUnit runs check priority order, FIFO
  order, bucket promotion, requeue, and debug validation.

  Risk: A layout or list primitive change can corrupt bucket links or break
  list hardening and barriers.

  Exit condition: Rust owns the complete intrusive representation and list
  primitive contract.

Priority-list diagnostics
  Status: ``boundary-retained``.

  Owner: ``lib/plist.c`` and ``include/linux/plist-rust.h``.

  Reason: ``WARN_ON()``, ``BUG_ON()``, and the optional full-list validation
  require kernel diagnostic infrastructure.

  Test: Every native and fallback run enables ``CONFIG_DEBUG_PLIST`` and runs
  its boot self-test together with the KUnit suite.

  Risk: A changed diagnostic can mask a corrupt bucket or alter the fatal
  requeue contract.

  Exit condition: Rust diagnostics preserve the warning, fatal, and optional
  validation behavior with no C diagnostic bridge.

Unexported Rust public symbols
  Status: ``boundary-retained``.

  Owner: ``rust/Makefile``.

  Reason: Automatic Rust exports are GPL, while the selected symbols
  intentionally have no module export.

  Test: ``vmlinux.symvers`` contains none of the names in the
  unexported-symbol list, and the generated Rust export header excludes every
  listed name.

  Risk: An automatic export can unintentionally expand the module ABI.

  Exit condition: Rust export generation supports per-symbol unexported
  status without the skip list.

List sort layout and comparator ABI
  Status: ``boundary-retained``.

  Owner: ``include/linux/list.h`` and ``include/linux/list_sort.h``.

  Reason: C callers share the intrusive ``list_head`` layout and comparator
  callback ABI with Rust.

  Test: Native, fallback, and C-only KUnit runs check ordering, stability,
  edge cases, poisoning, and restored list links.

  Risk: A layout or callback change can corrupt list links or invoke the
  comparator with an invalid calling convention.

  Exit condition: Rust owns the complete intrusive list representation and
  comparator interface.

List sort export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/list_sort.c`` and ``rust/Makefile``.

  Reason: ``list_sort()`` retains its historical non-GPL export class while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records ``EXPORT_SYMBOL(list_sort)`` and the
  generated Rust GPL export header excludes the symbol.

  Risk: A duplicate export or class change can break module loading and
  binary compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

List LRU layout
  Status: ``boundary-retained``.

  Owner: ``include/linux/list_lru.h``.

  Reason: C and Rust access the same intrusive ``list_head``, node, slice,
  memory-cgroup slice, and memory-cgroup-aware xarray layouts.

  Test: Native, fallback, and C-only KUnit runs exercise add, delete,
  isolate, count, object-backed operations, every walk status, and concurrent
  workers on the same shared layouts.

  Risk: A layout change can invalidate direct Rust field access or make a C
  helper operate on the wrong slice or counter.

  Exit condition: Rust-owned types and accessors replace the shared C layouts.

List LRU locking and memory-cgroup resolution
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c`` and ``include/linux/list_lru-rust.h``.

  Reason: Spinlock and interrupt modes, RCU protection, object page-to-memcg
  lookup, dying-memcg traversal, and xarray slice iteration depend on MM and
  locking internals shared with C.

  Test: Native SMP1/SMP4, KASAN, lockdep, and PREEMPT_RT runs execute locked,
  interrupt, RCU, retry, stop, and concurrent paths.

  Risk: A lock-mode or lifetime mismatch can leave a slice locked, use a dead
  memcg, or expose a reparented slice.

  Exit condition: Rust owns the complete lock, RCU, memcg, and xarray lifetime
  contract.

List LRU lifecycle and memory-cgroup slices
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c``.

  Reason: Initialization, destruction, dynamic per-memcg slice allocation,
  and reparenting retain their existing C lifecycle and allocation rules.

  Test: Every configuration uses C-initialized list LRUs and runs the full
  suite in native, fallback, pure C, KASAN, lockdep, and PREEMPT_RT builds.

  Risk: Incorrect allocation or reparent ordering can corrupt slice counts or
  detach live objects from the selected memcg.

  Exit condition: Rust owns list LRU allocation, destruction, and memcg
  reparenting with equivalent lifetime rules.

List LRU export metadata
  Status: ``boundary-retained``.

  Owner: ``mm/list_lru.c`` and ``rust/Makefile``.

  Reason: Nine node-local functions retain GPL exports, while three public
  helpers and two caller-locked helpers have no module export.

  Test: ``vmlinux.symvers`` records the nine GPL exports, and the generated
  Rust GPL export header excludes all five unexported helpers.

  Risk: A duplicate export or class change can break module compatibility or
  expose an internal caller-locked interface.

  Exit condition: Rust export metadata supports per-symbol export and
  unexported status without the skip list.

Generic radix tree layouts and inline API
  Status: ``boundary-retained``.

  Owner: ``include/linux/generic-radix-tree.h``.

  Reason: C and Rust access the tagged root, interior child array, leaf data
  union, iterator, and type-aware container macros through one shared ABI.

  Test: Native, fallback, and C-only KUnit runs exercise shallow and deep
  trees, sparse entries, non-power-of-two objects, and repeated concurrent
  allocation.

  Risk: A layout, depth encoding, or offset-conversion change can make Rust
  traverse the wrong node or return a misaligned leaf entry.

  Exit condition: Rust-owned tree, node, iterator, and container interfaces
  replace the generic radix tree macros and layouts.

Generic radix tree atomic helpers
  Status: ``boundary-retained``.

  Owner: ``rust/helpers/genradix.c``.

  Reason: Root and child publication require kernel release compare-exchange,
  read-once child loads, and null exchange semantics.

  Test: Native SMP4, KASAN, lockdep, and PREEMPT_RT runs execute concurrent
  root and child allocation paths.

  Risk: A weaker or mismatched atomic primitive can publish an uninitialized
  child, lose an allocated node, or expose an incomplete tree.

  Exit condition: Rust owns equivalent layout-aware atomic publication and
  release primitives.

Generic radix tree allocation and release primitives
  Status: ``boundary-retained``.

  Owner: ``include/linux/generic-radix-tree.h`` and
  ``rust/helpers/genradix.c``.

  Reason: Zeroed page allocation, page release, and leaf data access retain
  the kernel allocator, memory-tagging, and layout contracts.

  Test: Every configuration checks zeroed pages, preallocated-node reuse,
  recursive release, and tree reset.

  Risk: An allocation or release mismatch can leak nodes, return nonzero
  entries, or free a published subtree.

  Exit condition: Rust owns allocation and release with equivalent zeroing,
  reuse, and lifetime behavior.

Generic radix tree export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/generic-radix-tree.c`` and ``rust/Makefile``.

  Reason: All six public symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records six ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all six names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

B+Tree layouts and geometry
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree.h``.

  Reason: C and Rust access the same tree head, geometry, node arrays, key
  slots, and value slots through one shared layout.

  Test: Native, fallback, and C-only traversal, structural, and visitor
  suites exercise 32-bit, 64-bit, and 128-bit keys in multi-level trees.

  Risk: A layout or geometry change can make Rust read or write the wrong key
  or value slot.

  Exit condition: Rust-owned tree, geometry, node, and key representations
  replace the shared C layout.

B+Tree allocation and lifecycle
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree-rust.h`` and ``lib/btree.c``.

  Reason: Zeroed mempool allocation, node release, geometry objects,
  initialization, destruction, allocator callbacks, and module setup retain
  kernel allocator and lifecycle contracts.

  Test: Native, fallback, and C-only suites check root growth, split rollback,
  failed allocations, merge ownership, node release, and tree reuse.

  Risk: An allocation or release mismatch can leak nodes, use a freed node,
  or attach a node from the wrong mempool.

  Exit condition: Rust owns node allocation, release, geometry setup, and tree
  lifecycle with equivalent zeroing and ownership behavior.

B+Tree typed visitor callbacks
  Status: ``boundary-retained``.

  Owner: ``include/linux/btree.h`` and ``lib/btree.c``.

  Reason: The four public typed visitor adapters retain the existing 32-bit,
  64-bit, and 128-bit callback signatures while Rust uses the shared machine
  word key representation.

  Test: Native, fallback, and C-only visitor suites check callback keys,
  descending order, stable indices, counts, grim release, and tree reuse.

  Risk: A callback-ABI change can corrupt typed keys or break visitor
  compatibility.

  Exit condition: Rust directly supplies compatible typed visitor interfaces
  for every public key width.

Kernel FIFO layout and inline API
  Status: ``boundary-retained``.

  Owner: ``include/linux/kfifo.h``.

  Reason: C and Rust access the same index, mask, element-size, storage
  pointer, scatterlist, and record-prefix layout through the inline API.

  Test: Native, fallback, and C-only KUnit runs exercise in-place and pointer
  FIFOs, wrap-around, element scaling, records, user copies, and DMA ranges.

  Risk: A layout or inline-contract change can make Rust use the wrong range
  or publish an index before the corresponding data is visible.

  Exit condition: Rust-owned FIFO and inline container interfaces replace the
  shared C representation.

Kernel FIFO allocation and initialization
  Status: ``boundary-retained``.

  Owner: ``lib/kfifo.c``.

  Reason: Buffer allocation, NUMA-node placement, devres release, and
  caller-provided storage lifetime retain kernel allocator contracts.

  Test: Every configuration checks allocated and caller-provided FIFO setup,
  invalid sizes, element sizes, release, and reuse.

  Risk: An allocation or reset mismatch can leak storage or leave stale
  indices visible to Rust.

  Exit condition: Rust owns FIFO allocation and initialization with
  equivalent device-managed storage and lifetime behavior.

Kernel FIFO copy and scatterlist helpers
  Status: ``boundary-retained``.

  Owner: The architecture user-access layer, ``smp_wmb()``, and
  ``rust/helpers/scatterlist.c``.

  Reason: User copies, publication barriers, and scatterlist mutation remain
  architecture and kernel-helper boundaries.

  Test: Native, fallback, and C-only KUnit runs cover partial user copies,
  wrap splitting, DMA segment creation, and mapped-address preservation.

  Risk: A missing barrier or helper mismatch can expose partial records or
  corrupt DMA metadata.

  Exit condition: Rust-owned equivalents preserve the architecture ordering,
  user-access, and scatterlist contracts.

Kernel FIFO export metadata
  Status: ``boundary-retained``.

  Owner: ``lib/kfifo.c`` and ``rust/Makefile``.

  Reason: All twenty out-of-line symbols retain ordinary non-GPL exports while
  generated Rust exports use the GPL class.

  Test: ``vmlinux.symvers`` records twenty ``EXPORT_SYMBOL`` entries, and the
  generated Rust GPL export header excludes all twenty names.

  Risk: A duplicate export or class change can break module compatibility.

  Exit condition: Rust export metadata supports the original non-GPL class
  without C export entries.

The lock-less list boundary is valid only while the inline producer paths,
intrusive layouts, and atomic semantics remain shared ABI. A boundary entry
does not transfer policy, retry decisions, state transitions, or exported
production entry points back to C.

The klist boundary is valid only while initialization, blocking removal,
task wakeup, and shared primitive behavior remain outside the migrated entry
scope. The retained C functions do not select insertion, reference, deletion,
attachment, or traversal policy.

The reference-counting boundary is valid only while the shared counter layout,
saturation encoding, architecture atomics and barriers, lock primitives,
warning sites, and ordinary export metadata remain named C boundaries. Rust
defines every public entry and owns every transition, limit, saturation,
conditional-lock, and held-lock decision.

The per-CPU reference boundary is valid only while the shared layout and
flags, inline get/put paths, allocation, switch locking, RCU queueing,
per-CPU collection, atomic operations, diagnostics, and callback ABI remain
named C boundaries. Rust defines every public lifecycle entry and owns every
initialization, switch, confirmation, release, reinitialization, and
resurrection decision.

The lockref boundary is valid only while initialization, the packed layout,
architecture operations, and non-GPL export metadata remain shared C
boundaries. Rust owns every reference transition and fallback decision.

The kref boundary is valid only while the shared layout, compile-time
initializer, refcount and lock primitives, release callback ABI, and ordinary
export metadata remain named C boundaries. Rust defines every public lifecycle
entry and owns every reference and release decision.

The decrement-and-lock boundary is valid only while architecture atomics,
lock and IRQ primitives, and ordinary export metadata remain named C
boundaries. Rust defines every public protocol entry and owns every decrement,
slow-path, and held-lock decision.

The completion boundary is valid only while the shared completion layout,
inline initialization, task and signal primitives, scheduler timeout calls,
simple-wait operations, PREEMPT_RT assertion, and export metadata remain named
C boundaries. Rust defines every public entry and owns every count, wait,
timeout, signal, wake, and consumption decision.

The semaphore boundary is valid only while the shared layout and
initialization, raw lock and interrupt transitions, task-state and signal
queries, scheduling, intrusive list and wake queue primitives, hung-task and
trace accounting, sleep assertion, and ordinary export metadata remain named
C boundaries. Rust defines every public entry and owns every count, fast path,
waiter, signal, timeout, handoff, wake-selection, and concurrent exclusion
decision.

The simple wait queue boundary is valid only while the shared queue and wait
layouts, construction macros, raw lock and list helpers, task-state and signal
queries, scheduler wakeup, and export metadata remain named C boundaries.
Rust defines every public entry and owns every initialization, membership,
wake-selection, wake-all, signal, cleanup, and state-order decision.

The wait queue boundary is valid only while the shared queue and entry
layouts, construction macros, raw lock and list helpers, task-state and signal
queries, scheduler actions, barrier and poll primitives, and mixed export
metadata remain named C boundaries. Rust defines every public and internal
entry and owns every insertion, callback, autoremove, woken, signal, finish,
exclusive-budget, wake-variant, and POLLFREE decision.

The bit and variable wait boundary is valid only while the shared key and
entry layouts, hashed table, callers' construction macros, atomic bit
primitives, task and jiffies reads, scheduler actions, signal queries, timeout
comparison, and mixed export metadata remain named C boundaries. Rust defines
every exported entry and the variable-key helper and owns every queue
selection, key match, wait loop, action result, deadline, and wake decision.

The RCU wait boundary is valid only while the shared wait layout, caller
condition expansion, RCU and task-state primitives, signal queries, scheduler
selection, wakeup, and ordinary and GPL export metadata remain named C
boundaries. Rust defines every public entry and owns every publication,
condition, timeout, signal, scheduling, cleanup, and wake-order decision.

The task work boundary is valid only while the shared task work-list and
callback layout, task PI lock, atomic primitives, notification flags,
diagnostics, IRQ work, scheduler progress, and unexported symbol policy remain
named C boundaries. Rust defines every public entry and owns every enqueue,
cancellation, detachment, dispatch, exit, and reentrant-drain decision.

The IRQ work boundary is valid only while the shared work and callback layout,
per-CPU lists, interrupt delivery, hotplug and PREEMPT_RT worker lifecycle,
diagnostics, low-level synchronization primitives, and export policy remain
named C boundaries. Rust defines every public entry and owns every claim,
queue, state, drain, dispatch, tick, and synchronization decision.

The IRQ event boundary is valid only while the shared descriptor and action
layouts, handler callback ABI, tracing and duration helpers, interrupt and
descriptor locking, thread flags, scheduler wakeup, randomness, and unexported
entry policy remain named C boundaries. Rust defines every production entry
and owns every traversal, result, wake, oneshot, active-count, and lifecycle
decision.

The IRQ resend boundary is valid only while the shared descriptor and resend
queue layouts, raw lock, hlist operations, tasklet storage and scheduling,
irqchip callbacks, flow-handler invocation, and unexported entry policy remain
named C boundaries. Rust defines every production entry and owns every level,
replay, pending, retrigger, nesting, deduplication, and drain-order decision.

The IRQ spurious boundary is valid only while shared descriptor fields,
jiffies arithmetic, diagnostics, full descriptor scans, IRQ disabling, timer
rearming, and unexported entry policy remain named C boundaries. Rust defines
the production entry and owns every polling, result, threaded-accounting,
window, misroute, threshold, and terminal-state decision.

The timer queue boundary is valid only while intrusive timer queue layouts,
inline accessors, red-black tree primitives, and caller serialization remain
named C boundaries. Rust defines every out-of-line public entry and owns every
ordered search, link, cached-root, deletion, successor, and linked-chain
decision.

The high-resolution timer boundary is valid only while the shared timer
layouts, raw locks, debug objects, tracepoints, time sampling, opaque clock
event fields, task state, callback invocation, and timekeeping bridges remain
named C boundaries. Rust defines every ordinary public entry and the internal
interrupt, queue, next-event, and deferred-rearm entries. Rust owns every
startup, queue, expiry, interrupt, rearm, migration, cancellation, forwarding,
and sleeper decision.

The kobject boundary is valid only while the shared layout, bitfields,
diagnostics, final cleanup callback, and non-GPL export metadata remain named
C boundaries. Rust defines every public lifecycle entry and owns every
initialization and reference decision.

The interval tree boundary is valid only while the intrusive node, cached
red-black tree layout, augmented callback interface, and two narrow C helpers
remain shared infrastructure. Rust defines every public entry and owns start
ordering, overlap policy, subtree maximum updates, and span transitions.

The red-black tree boundary is valid only while the shared node encoding,
intrusive layouts, augmented callback interface, internal erase bridge, and
historical export metadata remain named boundaries. Rust defines every public
entry and owns every tree mutation and traversal decision.

The priority-list boundary is valid only while the intrusive representation,
raw list primitives, diagnostics, and unexported ABI policy remain named C
boundaries. Rust defines every public entry and owns every placement, bucket,
and requeue decision.

The list sort boundary is valid only while the shared list layout, comparator
ABI, and historical export metadata remain named boundaries. Rust defines the
public entry and owns every merge, pending-list, stability, and final-link
decision.

The list LRU boundary is valid only while the shared layouts, lock and
interrupt modes, RCU and memory-cgroup resolution, initialization and
destruction, and dynamic slice allocation and reparenting remain named C
boundaries. Rust defines every node-local public entry and owns every item,
count, scan, and walk transition.

The generic bit-search boundary is valid only while the shared
``unsigned long`` bitmap layout, inline public interface, random-number
primitive, and ordinary export metadata remain named boundaries. Rust defines
every public out-of-line search and owns every mask, traversal, count, and
selection decision.

The generic bitmap boundary is valid only while allocation, parsing,
formatting, storage operations, constant-foldable inline accessors, and
ordinary export metadata remain named boundaries. Rust defines every public
out-of-line bitmap operation and owns every mask, traversal, count, mapping,
selection, and conversion decision.

The Maple Tree load boundary is valid only while encoded roots, node layouts,
protected slot access, lockdep diagnostics, tracing, and ordinary export
metadata remain named C boundaries. Rust defines the public load entry and
owns every root, pivot, restart, and RCU lifetime decision.

The Maple Tree walk boundary is valid only while encoded nodes, traversal
metadata, protected access, and lockdep helpers remain named C boundaries.
Rust defines the public walk entry and owns every state normalization, range,
restart, and result transition.

The generic radix tree boundary is valid only while the tagged layouts,
inline container API, atomic publication helpers, allocation primitives, and
non-GPL export metadata remain named C boundaries. Rust defines every public
out-of-line entry and owns every growth, traversal, iteration, allocation
policy, and release transition.

The B+Tree boundary is valid only while the shared tree and node layouts,
mempool allocation primitives, typed visitor callbacks, geometry objects, and
lifecycle functions remain named C boundaries. Rust defines every public
algorithm entry and owns every traversal, update, structural, merge, visitor,
and reclamation decision.

The kernel FIFO boundary is valid only while the FIFO layout, allocation and
initialization, user-access and barrier helpers, scatterlist mutation, and
ordinary export metadata remain named C boundaries. Rust defines every public
out-of-line entry and owns every copy, framing, segment, and index decision.

Pending inventory
-----------------

The following major areas remain pending. The order follows dependency and
risk. Device drivers and peripheral subsystems remain behind the core work.

Direct-entry audit
  Status: ``pending``.

  Replace C ABI adapters with Rust-defined public entry points unit by unit.

Remaining core object lifecycles
  Status: ``pending``.

  Complete the direct-entry and ownership review for object types beyond
  kobject.

Lock and wait lifecycles
  Status: ``pending``.

  Complete the direct-entry and concurrency review.

Time and IRQ core
  Status: ``pending``.

  Complete the direct-entry and interrupt-context review.

Scheduling
  Status: ``pending``.

  Complete the direct-entry and lock-order review.

Memory management
  Status: ``pending``.

  Complete the direct-entry and fault-path review.

Signals and process lifecycles
  Status: ``pending``.

  Complete the direct-entry and lifetime review.

Core filesystem
  Status: ``pending``.

  Begin after the core contracts stabilize.

Core networking
  Status: ``pending``.

  Begin after the core contracts stabilize.

Device drivers
  Status: ``pending``.

  Begin after the core subsystems.

An earlier Rust algorithm with a C adapter is not listed as migrated. It
remains available in the ``rs`` baseline, but it must pass the direct-entry
audit before this ledger records it as complete.

Promotion rules
---------------

A component moves to ``migrated`` only after all of these conditions hold:

* Rust defines every production entry point for the selected scope.
* The native build excludes the corresponding C production implementation.
* Rust owns the component state machine, lifecycle, errors, and policy.
* Every retained boundary has an owner, reason, test, risk, and exit
  condition.
* Native, fallback, and C-only builds pass.
* Boot, focused KUnit, concurrency, KASAN, lockdep, PREEMPT_RT, Rustfmt,
  Clippy, Rustdoc, Sphinx, and checkpatch pass as applicable.
* Symbol inspection proves the production ownership claim.
