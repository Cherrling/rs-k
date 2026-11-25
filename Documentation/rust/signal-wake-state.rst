.. SPDX-License-Identifier: GPL-2.0

Native Rust Signal Wakeup State
===============================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_WAKE_STATE`` option selects native Rust for the
production ``signal_wake_up_state()`` function. The original C implementation
remains available as a fallback.

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

Rust directly defines ``signal_wake_up_state()``. The native build compiles
the original C implementation out of ``kernel/signal.c``. Rust owns the
operation order, the lock-contract assertion, and the wake-or-kick decision.
The C fallback and C-only configurations retain the complete implementation.

Retained C boundary
-------------------

The following operations remain in C:

* A ``lockdep_assert_held()`` helper for the signal-handler lock contract.
* ``set_tsk_thread_flag()`` for the architecture thread-flag operation.
* ``wake_up_state()`` for scheduler wakeup.
* ``kick_process()`` for the cross-CPU reschedule fallback.

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

Each boot used 1536 MiB of guest memory. Native completed three four-vCPU
runs and one single-vCPU run. Every other configuration completed one run at
each CPU count.

.. list-table::
   :widths: 34 16 14 12 24
   :header-rows: 1

   * - Configuration
     - Boots
     - Signal groups
     - Cases
     - Result
   * - Rust signal wakeup state
     - 3 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C signal wakeup state fallback
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal wakeup state with KASAN
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal wakeup state with lockdep
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass
   * - Rust signal wakeup state with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 9 of 9
     - 12 of 12
     - Pass

The sleeping-task case verifies pending-state publication, the wake-mask
composition, successful scheduler wakeup, and interruptible wait completion.
The running-task case verifies the failed-wakeup path and the process-kick
fallback.

The policy counters run only with the native Rust implementation. The real
sleeping and running task behavior runs with either the C or Rust
implementation.

Every boot also ran ``complete_signal``, ``send_signal``,
``prepare_signal``, ``signal_dequeue``, ``signal_pending_recalc``,
``signal_struct_copy``, ``signal_retarget``, and ``signal_wake_select``.
Every log reported no failed case, BUG, warning, KASAN report, lockdep
complaint, oops, or panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck`` and Rustdoc. Focused Sphinx generated this page with only the
existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The complete
change passed ``git diff --check`` and strict ``checkpatch`` with zero errors,
warnings, or checks.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``signal_wake_up_state()``. Native
``kernel/signal.o`` contains neither a definition nor a direct reference to
the entry. ``kernel/ptrace.o`` contains an undefined reference to the entry.
Disassembly shows the Rust entry calls the lock assertion, pending-state,
scheduler wakeup, and process-kick helpers.

The production image contains no ``rust_signal_wake_up_state`` wrapper. The
``signal_wake_up_state`` entry has no export table entry, which preserves the
original unexported class. The production image has no unresolved symbol and
boots to the expected missing root-filesystem panic without an earlier
failure.

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
