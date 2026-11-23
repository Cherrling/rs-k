.. SPDX-License-Identifier: GPL-2.0

Native Rust Completion Migration
================================

Status and scope
----------------

The ``CONFIG_RUST_COMPLETION`` option selects an experimental native Rust
implementation of completions.  The migration keeps the existing
``struct completion`` layout and caller-facing API.

The Rust code owns these production decisions:

* Completion count increments and decrements.
* Saturation at ``UINT_MAX`` after broadcast completion.
* Waiter queue loops for finite and infinite waits.
* Timeout and pending-signal results.
* Single completion, current-CPU completion, and broadcast completion.
* Non-blocking count consumption and completion queries.

The original C state machine remains the fallback when the option is disabled.

Entry points and ownership
--------------------------

Rust directly defines the fourteen public entry points and retains their names
and signatures. Thirteen entry points keep their ordinary ``EXPORT_SYMBOL``
class, and ``complete_on_current_cpu()`` remains unexported.
``init_completion()``, ``reinit_completion()``, and the static initializer
remain inline construction boundaries. They initialize the embedded swait
queue and reset the count to zero.

The production state machine is in ``rust/kernel/completion_core.rs``.
The native ``kernel/sched/build_utility.o`` provides the narrow primitive
boundary and the ordinary export records. It defines no public completion
entry body. The complete C implementations remain available when the option
is disabled.

Retained C boundary
-------------------

The following operations stay in C because they use scheduler, task,
architecture, or locking internals:

* Raw spinlock acquisitions and interrupt state restoration.
* Current task state stores and pending-signal queries.
* Sleepability checks and completion memory-ordering hooks.
* Normal and I/O scheduler timeout calls.
* Simple wait queue preparation, finish, single wake, and broadcast wake.
* The scheduler flag for current-CPU wakeup preference.
* The realtime threaded-context lock assertion.

These helpers do not modify completion counts or decide when a wait succeeds,
times out, or accepts a signal.

Important invariants
--------------------

The embedded swait lock protects completion counts and waiter membership.
The wait loop drops that lock only while the current task sleeps.  It restores
the task state and removes the stack wait entry before returning.

``complete()`` increments a nonzero-bounded count and wakes one FIFO waiter.
``complete_all()`` stores ``UINT_MAX`` and wakes every queued waiter.
``try_wait_for_completion()`` never consumes a broadcast completion.

``completion_done()`` acquires and releases the wait lock after a nonzero
count observation.  This synchronizes with ``complete()`` before a caller can
free the completion.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Native Rust, Rust fallback, and C-only builds and boots with one and four
  virtual CPUs.
* Rust-enabled KASAN, lockdep, prove-locking, debug spinlocks, and
  PREEMPT_RT builds.
* Count consumption, finite timeout, and all public wait variants.
* FIFO single completion with queued kernel threads.
* Broadcast completion, saturation, reset, and future-waiter behavior.
* Interruptible signal rejection.
* Concurrent completion producers.
* Symbol checks showing Rust defines all fourteen public entry points and the
  native C object defines no public completion body.
* Export checks showing thirteen ordinary exports remain and
  ``complete_on_current_cpu`` remains unexported.
* The nine sleeping wait entry points remain in ``.sched.text``.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation record
-----------------

The final tree passed the following x86_64 checks with ``LLVM=1``:

* Native Rust, one vCPU: six tests passed, once.
* Native Rust, four vCPUs: six tests passed, three consecutive runs.
* Rust fallback, one vCPU: six tests passed, once.
* Rust fallback, four vCPUs: six tests passed, three consecutive runs.
* C-only, one and four vCPUs: six tests passed in both runs.
* Generic inline KASAN, one vCPU: six tests passed, once.
* Generic inline KASAN, four vCPUs: six tests passed, three consecutive runs.
* ``PROVE_LOCKING``, ``DEBUG_LOCK_ALLOC``, ``LOCKDEP``,
  ``DEBUG_SPINLOCK``, and ``DEBUG_LIST`` enabled, four vCPUs: six tests
  passed.
* ``PREEMPT_RT``, four vCPUs: six tests passed.
* Adjacent ``swait``, ``semaphore``, ``rcuwait``, and ``dec_and_lock``
  suites: 5, 6, 8, and 6 tests passed, respectively.

The six cases cover count consumption and timeout, public wait variants,
signal interruption, FIFO single completion, broadcast completion, and
concurrent producers.  The logs contain no unexpected ``WARNING``, ``BUG``,
``Oops``, KASAN report, failed test, lock dependency diagnosis, or kernel
panic.

The object-level ABI checks covered all six configurations. The four native
Rust configurations define all fourteen public entry points in
``rust/kernel.o``, and ``kernel/sched/build_utility.o`` defines none. The Rust
fallback and C-only configurations define all fourteen in
``kernel/sched/build_utility.o``, and the Rust object defines none.

All six ``Module.symvers`` files contain exactly the original thirteen
``EXPORT_SYMBOL`` entries. They contain no GPL export or duplicate for these
names, and no export for ``complete_on_current_cpu``. The generated Rust GPL
export headers, where present, exclude all fourteen names. The nine sleeping
wait entry points remain in ``.sched.text`` in every native Rust object.

Clippy, Rustfmt, Rustdoc, focused Sphinx, checkpatch with strict mode, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for task state, scheduling, signals, raw
  locks, or swait membership.
* Completion initialization moves out of the inline construction boundary.
* The completion count type or saturation value changes.
* A scheduler action other than normal or I/O timeout is introduced.
* Completion wakeup ordering or signal behavior changes.
* Benchmarks show that the out-of-line ABI and helper transitions are too
  costly for scheduler-core users.
