.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Wakeup State Selection
=========================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_WAKE_SELECT`` option selects native Rust for the
production state selection in the inline ``signal_wake_up()`` and
``ptrace_signal_wake_up()`` adapters. The option depends on the native Rust
wakeup state machine.

This unit covers fatal-signal job-control transitions, ptrace resume
transitions, and selection of the state passed to ``signal_wake_up_state()``.
It excludes signal queue manipulation, signal delivery, ptrace stop protocol,
and scheduler wakeup.

The Rust code owns these decisions and transitions:

1. Reading ``task->jobctl``.
2. Recognizing ``JOBCTL_PTRACE_FROZEN``.
3. Clearing stopped and traced state for a fatal signal.
4. Selecting ``TASK_WAKEKILL | __TASK_TRACED`` for fatal signals.
5. Clearing traced state for a ptrace resume.
6. Selecting ``__TASK_TRACED`` for ptrace resume.
7. Calling the native Rust wakeup state machine.

Entry points and ownership
--------------------------

The inline ``signal_wake_up()`` and ``ptrace_signal_wake_up()`` functions
remain the public C ABI adapters. When the option is enabled, they call
``rust_signal_wake_up()`` and ``rust_ptrace_signal_wake_up()`` respectively.
The original C selection logic is compiled out.

Rust owns the complete job-control state selection. It calls the native Rust
wakeup state machine directly and does not call the C state machine.

Retained C boundary
-------------------

The following operations remain in C:

* The public inline ABI adapters.
* The signal-handler lock assertion.
* Thread-flag, scheduler wakeup, and process-kick primitives.

The caller must hold ``task->sighand->siglock`` with local interrupts disabled.
Rust preserves that requirement and does not sleep.

Ordering and invariants
-----------------------

A fatal signal clears ``JOBCTL_STOPPED`` and ``JOBCTL_TRACED`` unless
``JOBCTL_PTRACE_FROZEN`` is set. A frozen ptrace task keeps its job-control
state and uses a zero wake state.

A ptrace resume clears ``JOBCTL_TRACED`` and uses ``__TASK_TRACED`` as the
selected wake state. A non-resume notification keeps the job-control state and
uses a zero wake state.

The selected state is passed to the native Rust wakeup state machine, which
sets ``TIF_SIGPENDING`` and performs the wake-or-kick transition.

Validation record
-----------------

Each configuration runs three consecutive times with four vCPUs and 1536 MiB
of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust wakeup state selection
     - 3
     - 1 of 1
     - Pass
   * - C wakeup state selection fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust wakeup state selection with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust wakeup state selection with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust wakeup state selection with lockdep
     - 3
     - 1 of 1
     - Pass

The integration case calls both production adapters five times against a real
running kernel thread. It covers fatal state clearing, the frozen skip, the
non-fatal path, ptrace resume, and ptrace notification without resume.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The complete change
passed strict ``checkpatch`` and ``git diff --check``.

The production image disabled KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` showed direct relocations from fatal and ptrace wakeup
callers to ``rust_signal_wake_up()`` and
``rust_ptrace_signal_wake_up()``. Disassembly of ``kernel/ptrace.o`` showed
the same direct ptrace relocations.

The production ``vmlinux`` contained both Rust selection entries and the
native Rust wakeup state entry, contained no wake selection test symbols, and
had no undefined symbols. The image booted to the expected missing
root-filesystem panic and reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The inline adapters become out-of-line functions.
* A helper starts to select job-control policy or operation order.
* The ``sighand->siglock`` contract changes.
* Fatal, ptrace, or frozen state rules change.
* Signal delivery no longer requires the selected state to reach the wakeup
  state machine.
