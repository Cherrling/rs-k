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

Rust directly defines the five public functions and their ordinary
``EXPORT_SYMBOL`` class. The C fallback retains the complete implementations
when the option is disabled.

Rust owns:

* The non-final decrement fast path.

* The lock-backed slow path and its second decrement.

* The return-with-lock-held decision.

* The matching unlock when the second decrement is not final.

C helpers retain:

* ``atomic_add_unless()`` and ``atomic_dec_and_test()``.

* Spinlock, raw spinlock, and mutex lock and unlock primitives.

* IRQ state save and restore.

* Ordinary export metadata for the five public symbols.

The C helpers contain no reference-count transition decision. The native
``lib/dec_and_lock.o`` and ``kernel/locking/mutex.o`` objects define no public
decrement-and-lock body and do not retain an alternate implementation of the
sequence.

Important invariants
--------------------

The first atomic operation must decrement without the lock while the count
cannot reach zero. When the count is one, the caller must acquire the selected
lock before the second atomic operation.

The function returns true only when the second operation changes the count from
one to zero. The caller then owns the held lock. Every other path returns false
without leaving the lock held or changing the required interrupt state.

The IRQ-save variants save interrupt state only in the slow path. On non-RT
builds they return with interrupts disabled on the true path, and the caller
restores the saved state when it unlocks. PREEMPT_RT regular spinlocks sleep
and preserve their architecture-defined IRQ behavior, while raw spinlocks
retain hard IRQ disabling.

Validation gate
---------------

The unit must pass these checks before integration:

* Non-final and final transitions for spinlock, IRQ-save, raw spinlock, raw
  IRQ-save, and mutex variants.

* Lock ownership, final-transition uniqueness, and concurrent decrements.

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_DEC_AND_LOCK=n`` and one and four
  CPUs.

* C-only builds with ``CONFIG_RUST=n`` and one and four CPUs.

* Native Rust with KASAN.

* Native Rust with lockdep, prove-locking, and debug spinlocks.

* Native Rust with PREEMPT_RT.

* Symbol checks showing Rust defines all five public entry points, the native
  C objects define no public bodies, and fallback and C-only objects define the
  complete implementations.

* Export checks showing ``Module.symvers`` retains the ordinary export class
  and the generated Rust GPL export header excludes all five names.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-24 used parent commit ``ee3cc404bff1``, x86_64 LLVM
builds, and QEMU.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y`` and ``CONFIG_RUST_DEC_AND_LOCK=y``, one
                  SMP1 run and three SMP4 runs
Rust fallback     ``CONFIG_RUST=y`` and ``CONFIG_RUST_DEC_AND_LOCK=n``, one
                  SMP1 run and three SMP4 runs
Pure C            ``CONFIG_RUST=n``, one and four virtual CPUs
Native KASAN      Native Rust plus ``CONFIG_KASAN=y``, one SMP1 run and three
                  SMP4 runs
Native lockdep    Native Rust plus ``CONFIG_PROVE_LOCKING=y``,
                  ``CONFIG_DEBUG_SPINLOCK=y``, and
                  ``CONFIG_DEBUG_MUTEXES=y``, one SMP4 run
Native RT         Native Rust plus ``CONFIG_PREEMPT_RT=y``, one SMP4 run
================  ============================================================

Every configuration used ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, and ``CONFIG_DEC_AND_LOCK_KUNIT_TEST=y``.

All six kernel configurations compiled successfully. All 16 QEMU runs passed
all six ``dec_and_lock`` cases. The concurrent case used four kthreads that
performed 10,000 decrements each and selected one final-transition winner.

Three native SMP4 runs also passed the adjacent ``mutex``, ``rwlock``, and
``lockref`` KUnit suites with seven, five, and four passing cases.

The KASAN build enabled generic KASAN. The lockdep build initialized lock
dependency tracking. The RT kernel reported an ``SMP PREEMPT_RT`` build. The
logs contained no BUG, warning, KASAN report, suspicious RCU usage, lock
inversion, recursive locking, Oops, panic, or RCU stall.

The native ``rust/kernel.o`` defines all five public entry points. The native
``lib/dec_and_lock.o`` and ``kernel/locking/mutex.o`` define no public
decrement-and-lock body and retain their ordinary export records. The
fallback and C-only objects define the complete C implementations.
``Module.symvers`` records all five names as ``EXPORT_SYMBOL``, and the
generated Rust GPL export header excludes all five.

The old ``rust_dec_and_lock()``, ``rust_dec_and_raw_lock()``, and
``rust_dec_and_mutex_lock()`` public adapter names are absent. The retained
``rust_dec_and_lock_*`` helper names provide the narrow atomic and lock
primitives.

Rustfmt, Clippy, Rustdoc, strict checkpatch, and ``git diff --check`` passed.
Focused Sphinx generation completed with one unrelated cross-reference warning
in ``rust/index.rst``.

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
