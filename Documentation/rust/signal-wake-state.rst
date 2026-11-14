.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Wakeup State
===============================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_WAKE_STATE`` option selects native Rust for the
production ``signal_wake_up_state()`` function in ``kernel/signal.c``. The
original C implementation remains available as a fallback.

This unit covers the ordered wakeup sequence for a task that must process a
signal. Fatal-signal state selection in ``signal_wake_up()`` and ptrace state
selection are handled by :doc:`signal-wake-select`. This unit excludes signal
queue manipulation and signal delivery.

The Rust code owns these decisions and transitions:

1. Setting the target pending state.
2. Adding ``TASK_INTERRUPTIBLE`` to the caller-selected wake state.
3. Attempting the scheduler wakeup.
4. Selecting the process-kick fallback when the wakeup does not match.

Entry points and ownership
--------------------------

The production ``signal_wake_up_state()`` function remains the public C entry.
When the option is enabled, its body calls
``rust_signal_wake_up_state()`` and the original state machine is compiled out.

Rust owns the operation order and the wake-or-kick decision. It does not call
the original C state machine.

Retained C boundary
-------------------

The following operations remain in C:

* ``lockdep_assert_held()`` for the signal-handler lock contract.
* ``set_tsk_thread_flag()`` for the architecture thread-flag operation.
* ``wake_up_state()`` for scheduler wakeup.
* ``kick_process()`` for the cross-CPU reschedule fallback.
* The public ``signal_wake_up_state()`` ABI adapter.

The caller must hold ``task->sighand->siglock`` with local interrupts disabled.
Rust preserves that requirement and does not sleep.

Ordering and invariants
-----------------------

Rust sets ``TIF_SIGPENDING`` before it attempts to wake the target. The wake
mask always contains ``TASK_INTERRUPTIBLE``. If the scheduler wakeup reports
failure, Rust invokes ``kick_process()`` so a task on another CPU can enter the
signal delivery path.

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
   * - Rust signal wakeup state
     - 3
     - 2 of 2
     - Pass
   * - C signal wakeup state fallback
     - 3
     - 2 of 2
     - Pass
   * - Rust signal wakeup state with KASAN
     - 3
     - 2 of 2
     - Pass
   * - Rust signal wakeup state with PREEMPT_RT
     - 3
     - 2 of 2
     - Pass
   * - Rust signal wakeup state with lockdep
     - 3
     - 2 of 2
     - Pass

The sleeping-task case verifies pending-state publication, the wake-mask
composition, successful scheduler wakeup, and interruptible wait completion.
The running-task case verifies the failed-wakeup path and the process-kick
fallback.

The policy counters run only with the native Rust implementation. The real
sleeping and running task behavior runs with either the C or Rust
implementation.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The complete change
passed strict ``checkpatch`` and ``git diff --check``.

The production image disabled KUnit, KASAN, and lockdep. Disassembly of
``kernel/signal.o`` showed ``signal_wake_up_state()`` as a direct adapter to
``rust_signal_wake_up_state()``. Production signal callers also contained
direct relocations to the Rust entry.

The production ``vmlinux`` contained the Rust wakeup entry and its three C
helpers, contained no signal wakeup test symbols, and had no undefined
symbols. The image booted to the expected missing root-filesystem panic and
reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for thread flags or scheduler wakeup.
* A helper starts to select wake policy or operation order.
* The ``sighand->siglock`` contract changes.
* The interface from :doc:`signal-wake-select` to this wakeup state machine
  changes.
* Signal delivery no longer requires ``TIF_SIGPENDING`` publication before
  wakeup.
