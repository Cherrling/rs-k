.. SPDX-License-Identifier: GPL-2.0

Native Rust lockref operations
==============================

Status and scope
----------------

The ``CONFIG_RUST_LOCKREF`` option selects native Rust implementations of
lockref reference transitions. The option depends on x86_64,
``CONFIG_ARCH_USE_CMPXCHG_LOCKREF``, and SMP.

The original C algorithms remain available when the option is disabled or
when ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

The public ``lockref_*()`` interfaces keep their existing signatures and
export classes. When the option is enabled, their C entry points forward to
native Rust functions.

Rust owns:

* The bounded compare-exchange retry loop.

* Reference increments for live, zero-checked, and dead-checked operations.

* Positive decrements and the returned count.

* The decision to enter the spinlock fallback.

* The decision to return with the spinlock held from
  ``lockref_put_or_lock()``.

* The dead count and the rejection rules for zero and negative counts.

Retained C boundaries
---------------------

The ``lockref`` layout remains in C. It embeds the architecture spinlock and
the signed reference count in one naturally aligned 64-bit word.

Small C helpers retain:

* ``READ_ONCE()`` access to the packed word.

* Extraction of the lock state and count from the packed word.

* The relaxed 64-bit compare-exchange.

* Spinlock acquisition and release.

* The locked count load and store.

* The lockdep assertion for dead-state transitions.

These helpers do not select reference transitions. Rust supplies every new
count and chooses whether the operation succeeds, returns early, or enters
the serialized fallback.

The public C wrappers retain the exported ABI. The complete C implementation
remains compiled in fallback configurations.

Important invariants
--------------------

The packed spinlock must be unlocked before a compare-exchange can update the
reference count. A compare-exchange covers both the lock and count, so it has
the same visibility as an operation performed under the embedded spinlock.

The lockless path makes 100 attempts, matching the C implementation. A failed
compare-exchange reloads the expected packed word. The operation enters the
spinlock fallback when the lock is held or the retry budget is exhausted.

``lockref_get_not_zero()`` succeeds only when the old count is positive.
``lockref_get_not_dead()`` succeeds for zero and positive counts, and fails
for negative counts.

``lockref_put_return()`` decrements only a positive count. It returns ``-1``
when the count is not positive or the lockless attempt cannot complete.
It never takes the embedded spinlock.

``lockref_put_or_lock()`` decrements counts above one without leaving the
spinlock held. At count one or below, it returns false while holding the
spinlock so the caller can perform the final release.

The dead value is ``-128``. ``lockref_mark_dead()`` requires the caller to
hold the embedded spinlock.

Validation gate
---------------

The unit must pass these checks before integration:

* Rust-enabled x86_64 LLVM builds with one and four virtual CPUs.

* The same tests with native Rust lockref operations disabled.

* The same tests with ``CONFIG_RUST`` disabled.

* Live, zero-checked, and dead-checked increments.

* Positive, zero, and dead decrement behavior.

* The conditional locking contract for counts above one and counts at one.

* Concurrent references and conditional releases on SMP.

* Rust-enabled KASAN and lockdep runs.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

* Symbol checks that the native C object calls all Rust entry points and that
  fallback objects reference no Rust lockref symbol.

Validation results
------------------

Validation on 2026-11-17 used parent commit ``d4e1d5d24a92``.

================  ============================================================
Build             Configuration
================  ============================================================
Native Rust       ``CONFIG_RUST=y``, ``CONFIG_RUST_LOCKREF=y``
Rust fallback     ``CONFIG_RUST=y``, ``CONFIG_RUST_LOCKREF=n``
Pure C            ``CONFIG_RUST=n``
Native KASAN      Native Rust configuration plus ``CONFIG_KASAN=y``
Native lockdep    Native Rust configuration plus
                  ``CONFIG_PROVE_LOCKING=y``
================  ============================================================

Every configuration used x86_64, ``CONFIG_SMP=y``, ``CONFIG_NR_CPUS=4``,
``CONFIG_KUNIT=y``, and ``CONFIG_LOCKREF_KUNIT_TEST=y``.

All ten QEMU runs passed with one and four virtual CPUs. Each run reported
four of four KUnit cases passed.

The concurrent case used four kthreads. Each worker performed 10000
iterations. Every iteration completed one unconditional reference cycle and
one zero-checked reference cycle, for 80000 guarded get and conditional
release cycles.

The KASAN run initialized generic KASAN. The lockdep run initialized the lock
dependency validator. The logs contained no KASAN report, BUG, warning,
Oops, lock inversion, recursive locking report, or RCU stall.

The lockdep build expands ``spinlock_t`` to 64 bytes and disables the packed
lock and count word. C helpers then report that the lockless path is
unavailable. Rust still owns every count transition and runs the serialized
spinlock path.

In the native configuration, ``lib/lockref.o`` referenced all six
``rust_lockref_*`` entry points. Each public adapter was a 9-byte tail jump.
``rust/kernel.o`` defined all six Rust symbols. The Rust-fallback and pure C
objects referenced no Rust lockref symbol.

The build gate also exposed two existing configuration-only issues. The
interval-tree span type alias now follows the same configuration guard as its
users. A VMA-unmap KUnit entry point now documents its output-pointer safety
contract. These changes do not alter production behavior.

Rustfmt, Clippy ``bzImage``, Rustdoc, focused Sphinx generation, strict
checkpatch, and ``git diff --check`` passed.

Review conditions
-----------------

Reopen a retained boundary decision if any of these conditions changes:

* The lockref, spinlock, or packed-word layout changes.

* Spinlock, compare-exchange, or memory-order semantics change.

* The bounded retry count or fallback conditions change.

* The zero, dead, or conditional-locking contract changes.

* Rust stops controlling count transitions or fallback selection.

* A fallback object starts to use a Rust lockref symbol.

* Another architecture passes the complete validation gate.
