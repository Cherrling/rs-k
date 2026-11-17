.. SPDX-License-Identifier: GPL-2.0

Native Rust decrement-and-lock protocols
========================================

Status and scope
----------------

The ``CONFIG_RUST_DEC_AND_LOCK`` option selects native Rust implementations
for the atomic decrement-and-lock family. The public ABI includes
``atomic_dec_and_lock()``, its IRQ-save and raw-lock variants, and
``atomic_dec_and_mutex_lock()``.

This unit owns the decision to decrement without locking, enter the slow path,
decrement while holding the lock, and return with the lock held only for the
unique transition to zero. It does not migrate the architecture atomic
operations, kernel lock implementations, or the lock-specific unlock sites
used by callers.

The existing C implementations remain available when the option is disabled.

Entry points and ownership
--------------------------

The existing public functions remain C ABI entry points. When the option is
enabled, each entry point calls a native Rust implementation.

Rust owns:

* The non-final decrement fast path.

* The lock-backed slow path and its second decrement.

* The return-with-lock-held decision.

* The matching unlock when the second decrement is not final.

C helpers retain:

* ``atomic_add_unless()`` and ``atomic_dec_and_test()``.

* Spinlock, raw spinlock, and mutex lock and unlock primitives.

* IRQ state save and restore.

* Public exported ABI adapters.

The C helpers contain no reference-count transition decision and do not retain
an alternate implementation of the decrement-and-lock sequence.

Important invariants
--------------------

The first atomic operation must decrement without the lock while the count
cannot reach zero. When the count is one, the caller must acquire the selected
lock before the second atomic operation.

The function returns true only when the second operation changes the count from
one to zero. The caller then owns the held lock. Every other path returns false
without leaving the lock held or disabling interrupts.

The IRQ-save variants save interrupt state only in the slow path. They return
with interrupts disabled on the true path, and the caller restores the saved
state when it unlocks.

Validation gate
---------------

The unit must pass these checks before integration:

* Non-final and final transitions for spinlock, IRQ-save, raw spinlock, raw
  IRQ-save, and mutex variants.

* Lock ownership, final-transition uniqueness, and concurrent decrements.

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_DEC_AND_LOCK=n``.

* C-only builds with ``CONFIG_RUST=n``.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Symbol checks for all five Rust entry points.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Ten final SMP boot tests passed with six cases and no failures. The matrix
covered native Rust, ``CONFIG_RUST_DEC_AND_LOCK=n``, and ``CONFIG_RUST=n``
with one and four CPUs. Native Rust also passed with KASAN and with lockdep,
prove-locking, debug spinlocks, and debug mutexes.

Both KASAN logs report that the generic KernelAddressSanitizer initialized.
The lockdep logs contain no lock inversion or inconsistent lock state report.
A scan of all ten boot logs found no BUG, warning, KASAN report, lock
inversion, panic, or RCU stall.

The native ``lib/dec_and_lock.o`` and ``kernel/locking/mutex.o`` together
reference all five Rust entry points. The fallback and C-only objects
reference none of them. Disassembly shows each public decrement-and-lock entry
point jumps directly to its Rust implementation.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* An architecture changes the ordering contract of the atomic primitives.

* The lock-family API gains a conditional acquisition contract that Rust
  cannot preserve.

* PREEMPT_RT changes the relationship between spinlock, raw spinlock, and
  mutex entry points.

* Measurements show that crossing the C ABI in this protocol causes an
  unacceptable hot-path regression.
