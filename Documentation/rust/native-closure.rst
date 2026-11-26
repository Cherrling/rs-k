.. SPDX-License-Identifier: GPL-2.0

Native Rust closure lifecycle
=============================

Status and scope
----------------

The ``CONFIG_RUST_CLOSURE`` option selects native Rust for the production
closure reference-count and wait lifecycle. The public API, object layout,
ordinary export class, and debug interfaces remain unchanged.

Rust directly defines every public closure, wake, synchronization, timeout,
and debug entry point. Rust owns the remaining-count state machine, guard-bit
validation, wait-list membership, FIFO wake ordering, asynchronous callback
rescheduling, destructor and parent release ordering, synchronous handoff,
and timeout rollback.

The complete C implementation remains available when the option is disabled.

Ownership
---------

Rust owns:

* ``closure_sub()`` and ``closure_put()`` count transitions.
* Final-reference detection and the acquire barrier after the zero check.
* The ``closure_get_happened`` lifecycle transition.
* Guard-bit and inconsistent-zero validation decisions.
* Callback rescheduling and the reset to ``CLOSURE_REMAINING_INITIALIZER``.
* Destructor selection, debug retirement, and parent reference release.
* ``closure_wait()`` membership checks, reference acquisition, and
  publication.
* ``__closure_wake_up()`` chain detachment, reversal, FIFO traversal, waiting
  bit retirement, and reference release.
* The synchronous callback handoff used by ``__closure_sync()``,
  ``closure_return_sync()``, and ``__closure_sync_timeout()``.
* Wait-state transitions, completion detection, timeout expiry, and timeout
  rollback.
* The final no-rearm result of ``closure_return_sync()``.
* The public ``closure_debug_create()`` and ``closure_debug_destroy()`` entry
  points when ``CONFIG_DEBUG_CLOSURES`` is enabled.

C boundaries retain:

* The shared ``struct closure``, ``struct closure_waitlist``, state encoding,
  and initialization macros.
* Architecture atomic reads, stores, additions, subtractions,
  compare-exchanges, and the acquire barrier after the zero check.
* Lock-less list insertion and detached-chain deletion.
* Workqueue initialization and submission.
* Current task state, scheduling, timeout scheduling, RCU read locking, and
  task wakeup.
* The debug-list spinlock, magic-value checks, list storage, and debugfs
  formatting.
* The exact historical ordinary ``EXPORT_SYMBOL()`` metadata.

The C helpers in ``include/linux/closure-rust.h`` are narrow primitives.
They do not choose a reference transition, reorder a wait list, select a
callback, decide timeout success, roll back a timed wait, or release a parent.

Native ``lib/closure.o`` contains no public closure function body. It contains
only the primitive helpers and export metadata. Rust defines the public
symbols in ``rust/kernel.o``.

State invariants
----------------

The low 26 remaining-count bits must not reach zero while a guard bit other
than ``CLOSURE_DESTRUCTOR`` remains set. Rust preserves the original warning
sites through a C diagnostic helper.

A zero remaining count with a callback and without the destructor bit rearms
the count to ``CLOSURE_REMAINING_INITIALIZER`` before submission. A zero
remaining count with the destructor bit, or without a callback, retires debug
state, invokes the callback, and releases the parent reference.

Wait-list nodes are inserted at the lock-less list head. Wakeup detaches the
whole chain, reverses it in Rust, and processes the oldest entry first.
Rust reads the next node before the callback transition can reclaim the current
closure.

Synchronous waits publish a stack syncer, transfer the running reference to a
Rust callback, and wake the waiting task from that callback. A timeout first
attempts to restore ``CLOSURE_REMAINING_INITIALIZER``. If the closure has
already completed or another running reference is present, the wait retires
without a rollback.

Export metadata
---------------

The nine public lifecycle and debug names retain ordinary non-GPL exports.
The Rust automatic exporter skips these names so that the C metadata remains
the only export record. Rust uses the ``#[export]`` macro for signature
checking and direct C ABI entry-point definition.

Testing
-------

``CONFIG_CLOSURE_KUNIT_TEST`` selects the focused suite in
``lib/tests/closure_kunit.c``. The suite has ten cases:

* Reference acquisition, release, callback rescheduling, and debug retirement.
* Wait-list membership, debug caller-address recording, and duplicate-wait
  rejection.
* FIFO wakeup and destructor order across four waiting closures.
* Synchronous retirement after an asynchronous release.
* Timeout completion before expiry.
* Timeout expiry and rollback.
* Final ``closure_return_sync()`` retirement.
* Workqueue callback execution.
* Parent reference release through a child destructor.
* Concurrent reference transitions from four workers.

The same suite runs against native Rust, the C fallback, and a C-only build.
The debug entry points are also exercised when
``CONFIG_DEBUG_CLOSURES`` is enabled.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust with one and four CPUs.
* Native Rust without the debug closure layout, and with
  ``CONFIG_DEBUG_CLOSURES`` enabled.
* Rust fallback with ``CONFIG_RUST_CLOSURE=n`` on one and four CPUs.
* C-only with ``CONFIG_RUST=n`` on one and four CPUs.
* Native Rust with KASAN.
* Native Rust with lockdep and prove-locking.
* Native Rust with PREEMPT_RT.
* Adjacent reference-count, lock, wait, list, and workqueue regressions.
* Symbol checks for native, fallback, and C-only ownership.
* Ordinary non-GPL export checks.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation used base commit ``f407ed437e11``. Each QEMU boot selected the
ten focused cases with ``kunit.filter_glob=closure`` and passed all of them:

* Native Rust with ``CONFIG_DEBUG_CLOSURES=y`` on one and four CPUs.
* Native Rust with ``CONFIG_DEBUG_CLOSURES=n`` on one and four CPUs.
* Rust fallback with ``CONFIG_RUST_CLOSURE=n`` on one and four CPUs.
* C-only with ``CONFIG_RUST=n`` on one and four CPUs.
* Native Rust with KASAN under Clang on four CPUs.
* Native Rust with lockdep and ``CONFIG_PROVE_LOCKING`` on four CPUs.
* Native Rust with PREEMPT_RT on four CPUs.

No focused boot reported a BUG, warning, Oops, KASAN report, lockdep
complaint, PREEMPT_RT warning, or RCU stall.

The native SMP4 adjacent regression boot passed 124 of 124 cases: kref 4,
lockref 4, llist 4, swait 5, rcuwait 8, completion 6, waitqueue 18, wait bit
7, list 39, hlist 18, and klist 11.

Native ``rust/kernel.o`` defines all nine public closure entries and native
``lib/closure.o`` defines none of them. The fallback C object defines all
nine entries. The C-only build defines the seven non-debug entries because
``CONFIG_DEBUG_CLOSURES`` is disabled.

Native, fallback, and vmlinux export records retain ordinary
``EXPORT_SYMBOL`` metadata rather than GPL-only metadata. An external module
linked against native and fallback kernels resolved all nine entries. Against
the C-only kernel it resolved the seven non-debug entries.

Rustfmt, Clippy under the kernel warning policy, Rustdoc, focused Sphinx
generation, and ``git diff --check`` passed. Strict checkpatch reported no
findings after excluding the generic new-file path notice, because
``MAINTAINERS`` was updated, and ``AVOID_BUG`` for the two legacy fatal
debug-list checks that this change preserves.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for atomic count operations,
  lock-less lists, task state, scheduling, RCU task publication, or
  workqueue submission.
* The closure state encoding or guard-bit mask changes.
* Wakeup no longer requires FIFO reversal.
* Synchronous completion no longer uses the callback work union.
* The debug list requires different locking or lifetime rules.
* A public closure entry changes its export class.
