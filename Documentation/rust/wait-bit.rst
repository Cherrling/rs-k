.. SPDX-License-Identifier: GPL-2.0

Native Rust Bit and Variable Wait Migration
===========================================

Status and scope
----------------

The ``CONFIG_RUST_WAIT_BIT`` option selects native Rust state machines for
the bit and variable wait APIs in ``kernel/sched/wait_bit.c``. The migration
keeps the existing ``wait_bit_key``, ``wait_bit_queue_entry``, and public API
layouts.

Rust directly defines all 15 exported production entry points and the
unexported variable-key helper. The complete C implementation remains under
``#ifndef CONFIG_RUST_WAIT_BIT``.

Rust owns these production decisions:

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
* Normal, I/O, and timed scheduler wait actions.

Shared table storage, hash and atomic bit operations, current task and jiffies
reads, scheduler primitives, signal queries, timeout comparison, and ordinary
export metadata remain in C.

Entry points and ownership
--------------------------

Rust directly defines these ordinary-export entry points:

* ``bit_waitqueue()``
* ``wake_bit_function()``
* ``__wait_on_bit()``
* ``out_of_line_wait_on_bit()``
* ``__wait_on_bit_lock()``
* ``out_of_line_wait_on_bit_lock()``
* ``__wake_up_bit()``
* ``wake_up_bit()``
* ``__var_waitqueue()``
* ``init_wait_var_entry()``
* ``wake_up_var()``
* ``bit_wait()``
* ``bit_wait_io()``

Rust directly defines these GPL-export entry points:

* ``out_of_line_wait_on_bit_timeout()``
* ``bit_wait_timeout()``

Rust also directly defines the unexported ``__var_wake_key()`` helper and the
internal ``var_wake_function()`` callback. The ordinary symbols retain their
``EXPORT_SYMBOL`` class through metadata in ``kernel/sched/wait_bit.c``. Rust
automatic export generation omits the 13 ordinary names and both unexported
names, and emits the two GPL exports.

Scheduler-backed Rust entries retain the ``.sched.text`` section placement.

The production state machines are in ``rust/kernel/wait_bit_core.rs``. The
native C scheduler object contains only the narrow primitive helpers and
ordinary export metadata.

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
* ``schedule()``, ``io_schedule()``, and timeout scheduling primitives.
* Current-task signal queries and wrapping timeout comparison.
* Initialization of the shared wait queue table.
* Ordinary non-GPL export metadata for Rust-defined entry points.

These helpers do not select a queue, match a bit or variable key, choose a
callback, decide when a wait loop continues, select a wait action result,
acquire a bit lock, set a timeout deadline, or decide whether to send a wake.

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

* Native Rust, Rust fallback, and C-only x86_64 builds.
* One-vCPU and four-vCPU KUnit runs for all six configurations.
* Generic inline KASAN, lockdep, and PREEMPT_RT builds and boots.
* Bit queue stability and variable queue stability.
* Bit callback address, bit-number, set-bit, and clear-bit decisions.
* Normal wait completion for an already-clear bit and an action-cleared bit.
* Signal interruption and timeout action results.
* Clear-bit lock acquisition and action-cleared lock acquisition.
* Scheduler-backed normal and locked wait wakeups.
* Variable entry fields, callback identity, address matching, and wake cleanup.
* Symbol ownership, mixed export class, duplicate-export, and
  unresolved-symbol checks.
* Module builds and an external module ABI test for both export classes.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

The final source tree passed twelve x86_64 KUnit boots on 2026-11-24:

* Native Rust, Rust fallback, and C-only configurations on one and four vCPUs.
* Generic inline KASAN, lockdep, and PREEMPT_RT configurations on one and
  four vCPUs.

Every boot reported seven passed tests and no failures, skips, kernel warnings,
sanitizer reports, lockdep reports, or panic. The twelve boots passed 84 of
84 cases.

The native four-vCPU adjacent suites passed 80 of 80 cases: wait queue 18,
scheduler wake 37, completion 6, simple wait queue 5, RCU wait 8, and task
work 6.

The native Rust object defines all 17 entry points, while the native C
scheduler archive defines none. The fallback and C-only Rust objects define
none, while their C scheduler archives define all 17. The same ownership split
holds for the KASAN, lockdep, and PREEMPT_RT configurations.

All six ``Module.symvers`` files contain exactly 13 ordinary exports and two
GPL exports. Neither internal name is exported, no duplicate symbol record
exists, and every kernel image has no unresolved symbols.

Native, fallback, and C-only module builds passed modpost, and their twelve
loadable modules contain no unresolved Rust bit wait helper. An external
module that references all 15 exports linked against all three configurations.

Clippy, Rustdoc, rustfmt, strict checkpatch, and ``git diff --check``
completed without errors or warnings. Sphinx generated both modified pages
without errors or local warnings.

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
