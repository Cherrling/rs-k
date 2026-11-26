.. SPDX-License-Identifier: GPL-2.0

Native Rust object pools
========================

Status and scope
----------------

The ``CONFIG_RUST_OBJPOOL`` option selects native Rust implementations of
the object-pool initialization, forced release, outstanding-object drop, and
normal finalization entry points.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when the Rust option is disabled or ``CONFIG_RUST`` is disabled.

The lock-free pop and push paths remain inline C code. This unit covers the
slower lifecycle operations around those paths.

Entry points and ownership
--------------------------

Rust directly defines these public GPL interfaces:

* ``objpool_init()``

* ``objpool_free()``

* ``objpool_drop()``

* ``objpool_fini()``

Rust owns:

* Input limits and alignment.

* Ring capacity calculation.

* CPU topology traversal and object distribution.

* Slot allocation sizing and zeroing.

* Object initialization callback dispatch.

* Partial-initialization cleanup.

* Initial reference-count conversion.

* Outstanding-object reference drops.

* Drain and forced-release selection.

* Final cleanup callback selection.

The native C object retains no definition of these four entry points. The
``#[export]`` attributes preserve their historical ``EXPORT_SYMBOL_GPL``
class.

Retained C boundaries
---------------------

The packed ``objpool_slot`` and ``objpool_head`` layouts remain in C in
``include/linux/objpool.h``. C and Rust access the same per-CPU pointer array,
ring sequence fields, reference count, callbacks, and allocation flags.

C helpers in ``lib/objpool.c`` retain:

* CPU count and possible-CPU queries.

* CPU-local slot allocation and release.

* ``vmalloc`` and ``kmalloc`` selection.

* ``kvfree`` release.

* ``refcount_t`` transitions.

* The inline ``objpool_pop()`` implementation used during draining.

Their declarations are in ``include/linux/objpool-rust.h``. These helpers do
not select lifecycle policy, object distribution, callback order, or the
release decision.

Important invariants
--------------------

A request must contain one or more objects and must not exceed
``OBJPOOL_NR_OBJECT_MAX``. The object size must be positive and must not
exceed ``OBJPOOL_OBJECT_SIZE_MAX``. A rejected request leaves the pool
metadata unchanged.

The object size is aligned to the size of an unsigned long. The ring capacity
is the next power of two. The capacity and all allocation-size arithmetic
must fit the destination types.

Objects are assigned to possible CPUs in CPU-ID order. Each slot contains the
packed header, a pointer array with one entry for each capacity unit, and the
object storage for that CPU. The complete allocation is zeroed before
publication.

The optional object initialization callback runs once for each object before
the object becomes visible. A callback failure stops initialization. Rust
then frees the current slot and all earlier slots. The cleanup callback does
not run for a partially initialized pool.

The initial reference count is the number of allocated objects plus one
reference for the pool itself. Each ``objpool_drop()`` consumes one
outstanding-object reference. The final drop releases the pool.

``objpool_fini()`` counts the pool reference, pops every free object, and
subtracts the complete count. The pool is released only when no outstanding
objects remain. ``objpool_free()`` releases the slots directly and then runs
the cleanup callback.

The pool clears ``__GFP_ZERO`` before allocation because slot zeroing is part
of the lifecycle contract. ``GFP_ATOMIC`` allocation uses the slot allocator
without a direct ``vmalloc`` attempt.

Validation gate
---------------

The unit must pass these checks before integration:

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_OBJPOOL=n``.

* C-only builds with ``CONFIG_RUST=n``.

* Native Rust builds with KASAN, lockdep, and PREEMPT_RT.

* Parameter, layout, callback, drain, drop, forced-release, partial-failure,
  and atomic-allocation tests.

* Pop and push integration through a real objpool consumer.

* Symbol placement and GPL export checks in every build class.

* External module linkage for all four public symbols.

* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation on 2026-11-26 used base commit
``2e9b63201e842238df3b30f127c5f8be7c75b585``. All runs used x86_64 QEMU,
``CONFIG_SMP=y``, ``CONFIG_KUNIT=y``, and
``CONFIG_OBJPOOL_KUNIT_TEST=y``.

================  ============================================================
Build             Result
================  ============================================================
Native SMP1       KUnit 7 of 7, stress module passed 10 cases
Native SMP4       KUnit 7 of 7, stress module passed 10 cases
Fallback SMP1     KUnit 7 of 7, stress module passed 10 cases
Fallback SMP4     KUnit 7 of 7, stress module passed 10 cases
C-only SMP1       KUnit 7 of 7, stress module passed 10 cases
C-only SMP4       KUnit 7 of 7, stress module passed 10 cases
KASAN SMP4        KUnit 7 of 7
Lockdep SMP4      KUnit 7 of 7
PREEMPT_RT SMP4   KUnit 7 of 7
================  ============================================================

The KUnit suite covered invalid parameters, aligned object size, capacity and
reference count, initialization callbacks, pop and push, synchronous drain,
outstanding-object drop, forced release, partial-initialization failure, and
atomic allocation.

The existing ``test_objpool.ko`` stress module covered ten synchronous,
interrupt, asynchronous, overrun, slab, and ``vmalloc`` cases. The overrun
cases intentionally recorded expected misses. Every case reported a zero
kmalloc difference and a zero vmalloc difference. The stress ranges contained
no BUG, warning, Oops, KASAN report, kernel panic, or RCU stall.

The stress module returns ``-EAGAIN`` after it completes. Initramfs
``insmod`` therefore reports status 11 by design.

The native SMP1 and SMP4 runs with ``CONFIG_KRETPROBE_ON_RETHOOK=y`` passed
the adjacent ``kprobes_test`` suite with 7 of 7 cases. This exercised the
rethook objpool consumer.

The native ``rust/kernel.o`` defined all four public entry points. The native
``lib/objpool.o`` defined only the ten ``rust_objpool_*`` helpers. The fallback
and C-only ``lib/objpool.o`` objects defined the four public entry points.

Every Native, Fallback, and C-only ``Module.symvers`` file recorded all four
symbols with ``EXPORT_SYMBOL_GPL``. An external GPL module built against each
configuration and referenced exactly ``objpool_init``, ``objpool_free``,
``objpool_drop``, and ``objpool_fini``. Module post-processing resolved all
four symbols without a warning.

The KASAN run used LLVM. Its build reported an existing Maple Tree objtool
``NORETURN`` warning that does not reference objpool. No objpool report
appeared in the boot log.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
``git diff --check`` passed.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``objpool_head`` or ``objpool_slot`` layout changes.

* The packed ring encoding or inline pop and push contract changes.

* The allocation fallback, CPU topology, or callback ABI changes.

* ``refcount_t`` changes its lifecycle or memory-ordering contract.

* Kernel Rust gains safe direct interfaces for the retained primitives.

* Measurements show that the helper calls cause an unacceptable lifecycle
  regression.
