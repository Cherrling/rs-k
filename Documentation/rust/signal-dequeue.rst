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

The local ``next_signal()``, ``dequeue_signal()``, and
``dequeue_synchronous_signal()`` functions remain in ``kernel/signal.c`` as
ABI adapters. When the option is enabled, their original state machines are
compiled out and each adapter calls the corresponding Rust entry.

Rust owns signal selection and operation order. It does not call an original
C state machine. The C adapters contain one direct Rust call on the enabled
path.

Retained C boundary
-------------------

The following operations remain in C:

* Intrusive ``sigqueue`` list traversal.
* ``collect_signal()`` queue matching, removal, siginfo copy, and release.
* Synchronous queue removal, siginfo copy, and release.
* Pending-state recalculation.
* POSIX timer rearm and delivery primitives.
* Signal classification helpers.
* The signal-handler lock assertion and public ABI adapters.

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

Each KUnit configuration runs three consecutive times with four vCPUs and
1536 MiB of guest memory.

.. list-table::
   :widths: 44 12 14 30
   :header-rows: 1

   * - Configuration
     - Runs
     - Cases
     - Result
   * - Rust pending signal dequeue
     - 3
     - 1 of 1
     - Pass
   * - C pending signal dequeue fallback
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal dequeue with KASAN
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal dequeue with PREEMPT_RT
     - 3
     - 1 of 1
     - Pass
   * - Rust pending signal dequeue with lockdep
     - 3
     - 1 of 1
     - Pass

The integration case calls the production dequeue entries on the KUnit task.
It covers synchronous selection priority, local and shared queues, blocked
signals, the stop marker, synchronous queue collection, and synchronous
precheck misses. Rust-only counters verify the selected transitions.

The existing signal preparation, completion, and queue insertion tests also
run with ``CONFIG_RUST_SIGNAL_DEQUEUE=y``. A combined
``kunit.filter_glob=*signal*`` run passed ``complete_signal``,
``send_signal``, ``prepare_signal``, and ``signal_dequeue``.

Every KUnit log reported no BUG, warning, KASAN report, lockdep complaint, or
kernel panic.

Build and production checks
---------------------------

The final Rust source passed Clippy and ``rustfmtcheck``. The Rust
documentation build completed and generated this page. Its only warnings were
existing unresolved references in other Rust documentation and a missing
``dot`` program. The complete change passed ``git diff --check`` and strict
``checkpatch`` with the generic new-file warning ignored after ``MAINTAINERS``
was updated.

The production image disabled KUnit, KASAN, lockdep, and PREEMPT_RT.
``System.map`` contained ``rust_signal_next_pending``,
``rust_signal_dequeue``, and ``rust_signal_dequeue_synchronous``.
Disassembly showed a direct jump from ``next_signal()`` to
``rust_signal_next_pending``, a direct jump from ``dequeue_signal()`` to
``rust_signal_dequeue``, and direct calls from ``get_signal()`` to both Rust
dequeue entries.

The production ``vmlinux`` contained no signal dequeue test symbols and had no
undefined symbols. The image booted to the expected missing root-filesystem
panic and reported no earlier failure.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* Kernel Rust gains safe abstractions for intrusive signal queues.
* A helper starts to select signal policy, queue class, or operation order.
* Signal-set layout or synchronous signal classification changes.
* The POSIX timer delivery retry contract changes.
* The ``sighand->siglock`` contract changes.
* The surrounding ``get_signal()`` delivery state machine moves into Rust.
