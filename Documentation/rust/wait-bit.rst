.. SPDX-License-Identifier: GPL-2.0

Native Rust Bit and Variable Wait Migration
===========================================

Status and scope
----------------

The ``CONFIG_RUST_WAIT_BIT`` option selects native Rust state machines for
the bit and variable wait APIs in ``kernel/sched/wait_bit.c``. The migration
keeps the existing ``wait_bit_key``, ``wait_bit_queue_entry``, and public API
layouts.

The Rust code owns these production decisions:

* Shared queue hashing and table selection for bit and variable waits.
* Bit wait key matching, including the waited bit's current value.
* Variable wait key matching by address.
* Wait entry construction, callback selection, and list initialization.
* Normal bit wait traversal and action selection.
* Exclusive bit lock wait traversal and atomic acquisition.
* Timed wait deadline setup.
* Signal and timeout result propagation from wait actions.
* Variable wait entry initialization.
* Wake submission for explicit, bit, and variable queue selection.

Shared table storage, hash and atomic bit operations, current task and jiffies
reads, scheduler actions, and public ABI wrappers remain in C.

Entry points and ownership
--------------------------

The public wait, wake, callback, and queue-selection entry points retain their
names, signatures, and export classes. The production state machines are in
``rust/kernel/wait_bit_core.rs``. ``kernel/sched/wait_bit.c`` provides the C
ABI adapters and narrow primitive boundary.

Retained C boundary
-------------------

The following operations stay in C because they directly use architecture,
scheduler, or table internals:

* Shared wait queue table storage.
* The architecture-sized hash primitive.
* Ordered and unordered atomic bit tests.
* Atomic test-and-set for bit locks.
* Current task lookup.
* Read-once jiffies access.
* ``schedule()``, ``io_schedule()``, and timeout scheduling actions.
* Context annotation on scheduler-backed public entry points.
* Initialization of the shared wait queue table.

These helpers do not select a queue, match a bit or variable key, choose a
callback, decide when a wait loop continues, call a wait action, acquire a bit
lock, set a timeout deadline, or decide whether to send a wake.

Important invariants
--------------------

The bit queue hash combines the address and bit number. The variable queue
hash uses only the address. Both select from the same 256-entry shared table.

A normal bit wait prepares its entry, tests the bit, and invokes its action
only while the bit is set. The loop continues while the acquire-ordered test
sees the bit set and the action has not returned an error.

An exclusive bit lock wait prepares an exclusive entry. If its action returns
an error, Rust removes the entry before testing and setting the bit. A
successful test-and-set returns zero and removes any remaining queued entry
when the action did not already remove it.

The wake callback wakes a bit waiter only when the address and bit number match
and the target bit is clear. A variable waiter requires only an address match.
Both callbacks use ``autoremove_wake_function()`` for the final task wake and
queue removal decision.

The timed wait wrapper sets the key deadline to the current jiffies value plus
the requested timeout. The scheduler action owns timeout sleeping and result
selection.

Validation gate
---------------

The migration unit must pass these checks before integration:

* Rust-enabled and C fallback x86_64 builds.
* Rust-enabled generic inline KASAN build.
* One-vCPU and repeated four-vCPU Rust KUnit runs.
* Four-vCPU C fallback, KASAN, and lockdep KUnit runs.
* Bit queue stability and variable queue stability.
* Bit callback address, bit-number, set-bit, and clear-bit decisions.
* Normal wait completion for an already-clear bit and an action-cleared bit.
* Signal interruption and timeout action results.
* Clear-bit lock acquisition and action-cleared lock acquisition.
* Scheduler-backed normal and locked wait wakeups.
* Variable entry fields, callback identity, address matching, and wake cleanup.
* Export, adapter disassembly, and module unresolved-symbol checks.
* Rustfmt, Clippy, checkpatch, and ``git diff --check``.

Validation results
------------------

The final source tree passed seven x86_64 KUnit runs on 2026-11-11:

* One run on one vCPU with the Rust implementation enabled.
* Three consecutive runs on four vCPUs with the Rust implementation enabled.
* One run on four vCPUs with the C fallback implementation.
* One run on four vCPUs with generic inline KASAN enabled.
* One run on four vCPUs with lockdep, spinlock, and list debugging enabled.

Every run reported seven passed tests and no failures, skips, kernel warnings,
sanitizer reports, lockdep reports, or panic. A final normal Rust rebuild after
the Clippy run also passed the four-vCPU suite.

All thirteen public bit wait, callback, and queue-selection adapters disassemble
to tail calls into their Rust counterparts. The exported symbol names and
classes match the C fallback. The narrow C boundary helpers are not exported.
The C fallback image and the twelve loadable modules from both builds contain
no unresolved Rust bit wait symbols.

Clippy, rustfmt, strict checkpatch, and ``git diff --check`` completed without
errors or warnings.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* Kernel Rust gains stable abstractions for atomic bit operations, current
  task access, jiffies, or scheduler actions.
* The shared table size, hash primitive, or queue-selection contract changes.
* The bit or variable key matching protocol changes.
* Wait actions gain permission to retain or move entries across iterations.
* Atomic bit ordering or bit-lock ownership semantics change.
* Benchmarks show that the out-of-line ABI and boundary helpers are too costly
  for page, inode, buffer, or filesystem lock paths.
