.. SPDX-License-Identifier: GPL-2.0

Native Rust Pending Signal Dequeue
==================================

Status and scope
----------------

The ``CONFIG_RUST_SIGNAL_DEQUEUE`` option selects native Rust for pending
signal selection and dequeue in ``kernel/signal.c``. The original C
implementations remain available as fallbacks.

This unit covers ``next_signal()``, ``dequeue_signal()``, and
``dequeue_synchronous_signal()``. It includes signal-number selection,
local-versus-shared queue priority, synchronous signal priority, the
stop-dequeued marker, and POSIX timer retry selection. It excludes signal
generation, signal preparation, actual delivery, and the surrounding
``get_signal()`` state machine.

The Rust code owns these decisions and transitions:

1. Selecting the lowest available signal.
2. Giving queued synchronous signals priority in the first signal word.
3. Selecting the per-thread queue before the shared queue.
4. Recording whether a dequeued signal was thread-local or process-shared.
5. Rearming a POSIX interval timer after shared SIGALRM dequeue.
6. Marking a dequeued kernel stop signal with ``JOBCTL_STOP_DEQUEUED``.
7. Retrying after a POSIX timer delivery failure.
8. Selecting a synchronous queue entry by positive ``si_code``.
9. Preserving the pending bit when another synchronous ``siginfo`` remains.
10. Clearing the pending bit and recalculating pending state.

Entry points and ownership
--------------------------

Rust directly defines ``next_signal()``, ``dequeue_signal()``, and
``dequeue_synchronous_signal()``. The native build compiles their original C
state machines out of ``kernel/signal.c``. Rust owns signal selection and
operation order. It does not call an original C state machine.

``dequeue_signal()`` keeps its original GPL export. ``next_signal()`` and
``dequeue_synchronous_signal()`` remain unexported kernel entries.

Retained C boundary
-------------------

The following operations remain in C:

* Intrusive ``sigqueue`` list traversal.
* ``collect_signal()`` queue matching, removal, siginfo copy, and release.
* Synchronous queue removal, siginfo copy, and release.
* The pending-state recalculation adapter. When
  ``CONFIG_RUST_SIGNAL_PENDING_RECALC`` is enabled, this adapter calls the
  native Rust recalculation state machine.
* POSIX timer rearm and delivery primitives.
* Signal classification helpers.
* The signal-handler lock assertion helper.

These boundaries isolate intrusive-list mutation, object lifetime, timer
internals, and architecture thread flags. No helper selects the queue class,
signal number, synchronous entry, timer retry, or stop-marker decision.

The caller must hold ``current->sighand->siglock`` with interrupts disabled.
Rust preserves that requirement for the dequeue entries. ``next_signal()``
only reads caller-provided signal sets.

Ordering and invariants
-----------------------

Rust tries the per-thread queue first. It switches to the shared queue only
when the local queue has no unblocked signal. A shared SIGALRM is rearmed
before pending state is recalculated.

Pending state is recalculated after queue collection and before returning.
The ``JOBCTL_STOP_DEQUEUED`` marker is set after a stop signal is selected.
A POSIX timer delivery failure discards the selected queue entry and restarts
the complete local-then-shared selection.

Synchronous dequeue first checks the pending and blocked first-word masks.
Rust then scans queue entries in list order for a positive-code synchronous
signal. If another ``siginfo`` for the same signal follows, the pending bit
stays set. Otherwise Rust clears the bit before removing the selected queue
entry.

Validation record
-----------------

Each boot used 1536 MiB of guest memory. Native completed three four-vCPU
runs and one single-vCPU run. Every other configuration completed one run at
each CPU count.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Boots
     - Groups
     - Result
   * - Native Rust
     - 3 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - C fallback
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - C-only
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with KASAN
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with lockdep
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass
   * - Native Rust with PREEMPT_RT
     - 1 SMP4, 1 SMP1
     - 5 of 5
     - Pass

The integration case calls the production dequeue entries on the KUnit task.
It covers synchronous selection priority, local and shared queues, blocked
signals, the stop marker, synchronous queue collection, and synchronous
precheck misses. Rust-only counters verify the selected transitions.

Every boot also ran ``complete_signal``, ``send_signal``,
``prepare_signal``, and ``signal_pending_recalc`` with the selected dequeue
configuration.

Every KUnit log reported no BUG, warning, KASAN report, lockdep complaint, or
kernel panic.

Build and production checks
---------------------------

The production build passed with ``CONFIG_WERROR=y`` and Clippy. The module
build completed modpost without an error. The Rust source passed
``rustfmtcheck`` and Rustdoc. Focused Sphinx generated this page with only the
existing ``kernel-docs`` and ``getting_llvm`` reference warnings. The complete
change passed ``git diff --check`` and strict ``checkpatch`` with zero errors,
warnings, or checks.

The production image disables KUnit, KASAN, lockdep, and PREEMPT_RT.
``rust/kernel.o`` defines ``next_signal()``, ``dequeue_signal()``, and
``dequeue_synchronous_signal()``. Native ``kernel/signal.o`` contains only
undefined references to them. Fallback and C-only ``kernel/signal.o`` files
define all three entries and contain no references to Rust dequeue entries.

The native image has one GPL export table entry for ``dequeue_signal()``.
``next_signal()`` and ``dequeue_synchronous_signal()`` have no export table
entries. The old ``rust_signal_*`` wrapper names are absent, and the production
image has no unresolved symbols.

The production ``vmlinux`` contains no signal dequeue test symbols. The image
booted to the expected missing root-filesystem panic and reported no earlier
failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for intrusive signal queues.
* A helper starts to select signal policy, queue class, or operation order.
* Signal-set layout or synchronous signal classification changes.
* The POSIX timer delivery retry contract changes.
* The ``sighand->siglock`` contract changes.
* The surrounding ``get_signal()`` delivery state machine moves into Rust.
