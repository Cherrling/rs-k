.. SPDX-License-Identifier: GPL-2.0

Native Rust Interval Timer State Machine
========================================

Status and scope
----------------

The ``CONFIG_RUST_ITIMER`` option selects a native Rust implementation of the
process interval timer state machine. The existing task, signal, hrtimer, and
system call layouts remain unchanged.

The Rust code owns these production decisions:

* Real timer cancellation retry and replacement.
* Real timer remaining-time normalization.
* Real timer interval and expiry state transitions.
* Virtual and profiling CPU timer replacement.
* CPU timer relative-to-absolute conversion and tick adjustment.
* Periodic timer rearm selection after SIGALRM delivery.
* Legacy alarm remaining-seconds rounding.

User copies, ``siglock`` acquisition, hrtimer operations, CPU-time samples,
tracepoints, and SIGALRM delivery remain in C.

Entry points and ownership
--------------------------

``kernel/time/itimer.c`` keeps the system call adapters and
``it_real_fn()``. With the Rust configuration enabled, timer lookup and update
flow through:

* ``rust_itimer_get()``.
* ``rust_itimer_set()``.
* ``rust_itimer_rearm()``.
* ``rust_itimer_alarm()``.

The Rust-to-C helpers perform these operations:

* Signal lock and interrupt-state transitions.
* Real timer start, cancel-wait, queue test, forward, and restart.
* Thread-group CPU-time sampling.
* Process CPU timer installation.
* Interval timer tracepoint delivery.

Retained C boundary
-------------------

The signal lock remains in C because it protects signal delivery, CPU timer
expiry, and real timer replacement as one scheduling contract. Rust uses the
existing lock and does not introduce another lock.

Hrtimer primitives remain in C because timer queue locking, callback running
state, and clock bases depend on timer internals. Rust owns the cancellation
retry and state transition decisions.

CPU-time accounting remains in C because thread-group sample and absolute
expiry conversion depend on scheduler and clock internals.

User access and signal delivery remain in C. ``getitimer()`` and
``setitimer()`` copy legacy user structures in C. ``it_real_fn()`` traces the
expiry and sends SIGALRM in C.

Important invariants
--------------------

An inactive real timer reports zero. An active real timer that has expired but
not yet been observed reports at least one microsecond. The stored period is
reported independently of the remaining value.

Real timer replacement cancels the hrtimer while holding the signal lock. If
the callback already owns the timer lock, Rust drops the signal lock, waits for
the running callback, and retries. A zero expiry disarms the timer and clears
the period.

CPU timer replacement converts the relative value to nanoseconds, adds one
tick to nonzero values, and lets ``set_process_cpu_timer()`` convert the value
to an absolute CPU-time expiry and derive the old relative value. Rust stores
the resulting expiry and interval under the signal lock.

The periodic real timer is restarted only in the signal delivery path. The
rearm action occurs only when the hrtimer is not queued and the period is
nonzero.

The legacy ``alarm()`` result rounds an old nonzero subsecond remainder up to
one second.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled, C fallback, KASAN, lockdep, and PREEMPT_RT boots.
* Real and CPU timer set, get, disarm, and period checks.
* Invalid clock identifier checks.
* Repeated real and CPU timer replacement.
* Alarm rounding and periodic rearm path inspection.
* Adapter disassembly and unresolved-symbol inspection.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The final source tree was validated on 2026-11-11 with x86_64 LLVM builds
and QEMU/KVM using four vCPUs and 1 GiB of RAM.

* Rust production configuration: 6/6 KUnit tests passed.
* C fallback configuration: 6/6 KUnit tests passed.
* KASAN generic and inline configuration: 6/6 KUnit tests passed.
* Lockdep configuration: 6/6 KUnit tests passed.
* PREEMPT_RT configuration: 6/6 KUnit tests passed.

All five configurations completed the build and booted. The test logs
contained no BUG, WARNING, KASAN report, lockdep complaint, panic, or
atomic-scheduling report.

The Rust helper object also built with ``CONFIG_POSIX_TIMERS`` and
``CONFIG_RUST_ITIMER`` disabled.

The Rust image had no unresolved symbols. Disassembly showed calls from
the syscall and signal paths to ``rust_itimer_get()``,
``rust_itimer_set()``, ``rust_itimer_alarm()``, and
``rust_itimer_rearm()``. The C fallback image contained no
``rust_itimer_*`` production symbols.

Rustfmt, Clippy, strict checkpatch, and ``git diff --check`` passed.

Retained C boundary review conditions
-------------------------------------

Reopen the C boundary if any of these conditions changes:

* Rust gains stable signal-lock and hrtimer abstractions.
* Interval timer state moves out of ``signal_struct``.
* CPU-time sampling gains a native Rust interface.
* Signal delivery changes ownership of real timer rearming.
