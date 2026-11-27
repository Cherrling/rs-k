.. SPDX-License-Identifier: GPL-2.0

Native Rust inode lifecycle
===========================

Status and scope
----------------

The ``CONFIG_RUST_INODE_LIFECYCLE`` option selects native Rust for inode
allocation and rollback, reference acquisition and release, LRU insertion
and isolation, writeback triggering, eviction, and final destruction.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``alloc_inode()``

* ``new_inode()``

* ``ihold()``

* ``iput()``

* ``iput_not_last()``

* ``inode_lru_list_add()``

* ``prune_icache_sb()``

Rust also defines the private callbacks ``rust_inode_destroy()``,
``rust_inode_dispose_list()``, ``rust_inode_lru_isolate()``, and
``rust_inode_lru_list_del()``.

Rust owns:

* Filesystem inode allocation selection and generic slab allocation.

* Initialization-failure cleanup, callback selection, immediate free, and
  allocation failure propagation.

* Shared and final reference transitions.

* The final-reference drop decision, including dontcache, inactive-superblock,
  and writeback paths.

* LRU eligibility, insertion, removal, rotation, and unused-inode accounting
  decisions.

* LRU isolation, referenced-inode rotation, page-cache reclaim retry, and
  private shrink-list movement.

* Eviction callback selection, writeback waits, inode unhash, state validation,
  and final destruction.

* RCU versus immediate final-free scheduling after all filesystem callbacks
  complete.

The four ordinary exports ``new_inode()``, ``ihold()``, ``iput()``, and
``iput_not_last()`` retain their export class. ``alloc_inode()``,
``inode_lru_list_add()``, and ``prune_icache_sb()`` remain unexported. The
four private callbacks remain hidden.

Retained C boundaries
---------------------

The shared inode, superblock, address-space, list, and callback layouts remain
in C. Native ``fs/inode.o`` retains 62 narrow helpers for:

* Inode, superblock, and address-space state reads.

* Reference-count and inode-state primitives.

* ``i_lock`` acquisition, trylock, release, and lockdep checks.

* Slab allocation and ``inode_init_always()`` invocation.

* Filesystem allocation, drop, eviction, destroy, and free callbacks.

* Page-cache, writeback, hash, superblock-list, and character-device
  primitives.

* LRU insertion, removal, traversal, movement, bit waiting, and accounting.

* RCU callback scheduling and immediate callback invocation.

* Intrusive-list operations and scheduling points.

All 62 helpers have Rust references. The helpers expose no inode retention,
drop, LRU isolation, eviction-order, allocation-rollback, or final-free
decision.

``inode_init_always_gfp()``, ``clear_inode()``, and ``__destroy_inode()``
remain in C because they initialize or release shared VFS state, allocate
objects, run security and fsnotify callbacks, and manipulate page-cache or
filesystem-owned state.

``evict_inodes()`` remains in C. It walks the superblock ``s_inodes`` list
while holding ``s_inode_list_lock``, takes each ``i_lock``, and may drop the
superblock list lock to reschedule and dispose a private list. The traversal
depends on holding the current list position across that scheduling point.
Rust owns final reference handling, LRU removal, eviction, and destruction for
each inode reached by the traversal.

Important invariants
--------------------

An inode initialized by a filesystem allocation callback must run
``inode_init_always()``. If initialization fails, the filesystem destroy
callback and ``free_inode`` callback retain their original ownership rules.
An inode that was never published is freed without an RCU grace period.

``iput()`` first tries to decrement the reference without taking ``i_lock``.
The locked path handles lazy-time writeback, dirties, and the final reference.
The final path rechecks the reference count after the filesystem drop callback.

The final drop decision retains a clean, unused inode in the LRU when the
filesystem superblock is active. A dontcache inode or an inode on an inactive
superblock enters the writeback or eviction path. A special writeback path
uses ``I_WILL_FREE``, drops ``i_lock``, calls ``write_inode_now()``, and then
converts the state to ``I_FREEING``.

The inode is removed from the LRU after the final state transition. Eviction
waits for an active LRU isolation and running writeback, runs the filesystem
eviction callback or the generic page-cache eviction, forgets a character
device, removes the inode hash, wakes ``I_NEW`` waiters, and checks the final
``I_FREEING | I_CLEAR`` state.

``prune_icache_sb()`` isolates inodes under the LRU and inode locks. A
referenced inode returns with ``LRU_ROTATE``. An inode with a nonempty mapping
marks ``I_LRU_ISOLATING``, reclaims pages with both locks dropped, and requests
a walk retry. An isolated inode moves to a private list and is evicted only
after the LRU walk completes.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Null and final ``iput()``.

* Shared references and ``iput_not_last()``.

* LRU insertion and pruning.

* Allocation failure and initialization rollback.

* Concurrent reference acquisition and release.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``bb58bb44a6ce``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 9 of 9
Native Rust       SMP4                        KUnit 9 of 9
KASAN Rust        SMP1                        KUnit 9 of 9
KASAN Rust        SMP4                        KUnit 9 of 9
lockdep           SMP1                        KUnit 9 of 9
lockdep           SMP4                        KUnit 9 of 9
PREEMPT_RT        SMP1                        KUnit 9 of 9
PREEMPT_RT        SMP4                        KUnit 9 of 9
Fallback          SMP1                        KUnit 7 of 7
Fallback          SMP4                        KUnit 7 of 7
C-only            SMP1                        KUnit 7 of 7
C-only            SMP4                        KUnit 7 of 7
DEBUG_VFS         SMP1                        KUnit 9 of 9
DEBUG_VFS         SMP4                        KUnit 9 of 9
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, warning, or kernel halt.

The KASAN build uses Clang because Rust and KASAN require
``CONFIG_CC_IS_CLANG``. Its configuration enables ``CONFIG_RUST``,
``CONFIG_RUST_INODE_LIFECYCLE``, ``CONFIG_KASAN``,
``CONFIG_KASAN_INLINE``, ``CONFIG_PROVE_LOCKING``, ``CONFIG_KUNIT``, and
``CONFIG_INODE_LIFECYCLE_KUNIT_TEST``. The Native Rust object contains
``__asan_*`` references, so the KASAN run exercises instrumented Rust code.

The suite covers null and final drops, shared references,
``iput_not_last()``, LRU insertion, LRU pruning, four concurrent reference
workers, allocation failure, and initialization rollback. Each lifecycle case
obtains and releases a real rootfs path through ``kern_path()`` and
``path_put()``, so it crosses path, mount, superblock, dentry, and inode
lifetime code.

Native ``rust/kernel.o`` defines all seven production entries and all four
private callbacks. Native ``fs/inode.o`` defines none of the seven entries.
Fallback and C-only ``fs/inode.o`` each define all seven entries.

Native ``Module.symvers`` contains ordinary exports for ``new_inode()``,
``ihold()``, ``iput()``, and ``iput_not_last()``. It contains no entries for
``alloc_inode()``, ``inode_lru_list_add()``, or ``prune_icache_sb()``.
External GPL and non-GPL modules link all four ordinary exports. Modpost
rejects both license classes when a module references any of the three
unexported entries.

Disassembly shows complete Rust implementations of ``iput()``,
``new_inode()``, and ``prune_icache_sb()``. Native ``fs/inode.o`` contains C
callers and no duplicate public definition or public tail-call adapter.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` pass. Focused Sphinx retains only the existing unresolved
``kernel-docs`` and ``getting_llvm`` reference warnings. Strict checkpatch has
no findings.

The behavior and constraints are covered by the inode lifecycle KUnit suite
and this page.

Review conditions
-----------------

Reopen a retained C boundary decision if any of these conditions changes:

* The inode, superblock, address-space, list, or callback layout changes.

* Inode-state, reference-count, page-cache, writeback, hash, or superblock-list
  synchronization changes.

* The ``i_lock``, LRU isolation, or writeback wait protocol changes.

* Filesystem drop, eviction, destroy, or free callback ownership changes.

* Kernel Rust gains an interface that safely owns the ``evict_inodes()``
  superblock traversal and scheduling handoff.

* Measurements show an unacceptable inode lookup, drop, or reclaim
  regression.
