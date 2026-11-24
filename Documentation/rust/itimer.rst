.. SPDX-License-Identifier: GPL-2.0

Native Rust Interval Timer Entry Points
=======================================

Status and scope
----------------

The ``CONFIG_RUST_ITIMER`` option selects direct Rust definitions of the
production process interval timer entry points. The existing task, signal,
hrtimer, and system call layouts remain unchanged. The native build excludes
the complete C implementations. C fallback and C-only configurations retain
them.

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
``it_real_fn()``. When ``CONFIG_RUST_ITIMER`` is enabled, the adapters route
timer lookup and update through these Rust definitions:

* ``itimer_get()``.
* ``itimer_set()``.
* ``posixtimer_rearm_itimer()``.
* ``alarm_setitimer()``.

The native C object does not define these entry points. The C definitions of
``do_getitimer()``, ``do_setitimer()``, the periodic rearm body, and the alarm
helper are compiled out. ``alarm_setitimer()`` obtains the current task in
Rust.

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

* Native, C fallback, and C-only x86_64 builds.
* Native, C fallback, C-only, KASAN, lockdep, PREEMPT_RT, and debug-object
  boots.
* SMP1 and SMP4 runs for native, C fallback, and C-only configurations.
* Real and CPU timer set, get, disarm, and period checks.
* Invalid clock identifier checks.
* Repeated real and CPU timer replacement.
* Alarm rounding and periodic rearm path inspection.
* Object-level entry-point ownership and export inspection.
* Rustfmt, Clippy, Rustdoc, Sphinx, checkpatch, and ``git diff --check``.

Validation evidence
-------------------

The final source tree was validated on 2026-11-24 with x86_64 LLVM 21.1.8
and Rust 1.97.1. The configurations used QEMU/KVM, ``CONFIG_SMP``,
``NR_CPUS=64``, KUnit, and ``CONFIG_ITIMER_KUNIT_TEST``.

Seven configurations completed ten boot runs. Every run passed all 6 timer
tests:

* Native Rust: SMP1 and SMP4.
* C fallback: SMP1 and SMP4.
* C-only: SMP1 and SMP4.
* KASAN generic: SMP4.
* Lockdep: SMP4.
* PREEMPT_RT: SMP4.
* Debug objects with ``CONFIG_DEBUG_OBJECTS_TIMERS``: SMP4.

The KASAN logs contain ``KernelAddressSanitizer initialized``. The lockdep
logs contain ``Lock dependency validator`` initialization. The PREEMPT_RT
boot identifies the kernel as ``SMP PREEMPT_RT``. No validation log contains
a KASAN report, BUG, WARNING, Oops, panic, RCU stall, lock inversion, or
atomic-scheduling report.

``nm`` showed ``itimer_get()``, ``itimer_set()``,
``posixtimer_rearm_itimer()``, and ``alarm_setitimer()`` as defined text
symbols in native ``rust/kernel.o``. The native ``kernel/time/itimer.o``
referenced all four as undefined symbols and did not define the corresponding
C helpers. Disassembly showed direct calls or jumps from the native system
call and test adapters to the Rust definitions.

The C fallback Rust object contained none of the four definitions. Its C
object contained ``do_getitimer()``, ``do_setitimer()``, and the complete
periodic rearm body. The C-only build used the same complete C path. Neither
the fallback nor the C-only object referenced the four Rust production
entries.

The linked native ``vmlinux`` contained the four production entries and no
``rust_itimer_*`` production symbols. ``Module.symvers`` contained none of the
four entries. The only matching exported symbol was the GPL helper
``rust_helper_itimer_set_process_cpu_timer()``.

Rustfmt, Clippy, Rustdoc, a Rust Sphinx build, strict checkpatch, and
``git diff --check`` passed.

Retained C boundary review conditions
-------------------------------------

Reopen the C boundary if any of these conditions changes:

* Rust gains stable signal-lock and hrtimer abstractions.
* Interval timer state moves out of ``signal_struct``.
* CPU-time sampling gains a native Rust interface.
* Signal delivery changes ownership of real timer rearming.
