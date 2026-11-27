.. SPDX-License-Identifier: GPL-2.0

Native Rust dentry lifecycle
============================

Status and scope
----------------

The ``CONFIG_RUST_DENTRY_LIFECYCLE`` option selects native Rust for dentry
reference drops, retention decisions, eviction, private shrink-list
processing, LRU isolation, and final release scheduling.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``dput()``

* ``d_make_discardable()``

* ``__move_to_shrink_list()``

* ``shrink_dentry_list()``

* ``dput_to_list()``

Rust also defines the private callbacks ``rust_dentry_lru_isolate()``,
``rust_dentry_lru_isolate_shrink()``, and ``rust_dentry_shrink_kill()``.

Rust owns:

* Fast reference-drop selection through the lockref return value.

* Lockless retention checks and the transition to the locked slow path.

* Hashed, disconnected, dontcache, and filesystem delete-callback retention
  decisions.

* LRU insertion, removal, isolation, rotation, and movement to a private
  shrink list.

* Final dentry eviction, parent-chain walk, and parent reference release.

* Persistent-reference transfer for ``d_make_discardable()``.

* Private shrink-list insertion, consumption, and final release ownership.

* External-name release selection and the RCU versus immediate final-free
  choice.

The four ordinary exports ``dput()``, ``d_make_discardable()``,
``__move_to_shrink_list()``, and ``shrink_dentry_list()`` retain their export
class. ``dput_to_list()`` remains unexported. The three private callbacks
remain hidden.

Retained C boundaries
---------------------

The shared ``struct dentry``, ``struct inode``, ``struct file``, list, and
external-name layouts remain in C. Native ``fs/dcache.o`` retains 38 narrow
helpers for:

* Dentry and inode lock acquisition and release.

* Lockref count access and dead-state operations.

* Dentry flag loads and stores.

* RCU read-side sections, callbacks, and quiescent-state progress.

* Dentry hash and tree unlinking.

* The nested ``d_lock`` and inode ``i_lock`` kill protocol.

* Inode alias removal and the ``d_iput()`` or ``iput()`` callback.

* Dentry operations including ``d_delete()``, ``d_prune()``, and
  ``d_release()``.

* LRU insertion, removal, traversal, and accounting.

* Intrusive-list operations and diagnostic checks.

All 38 Native helpers have Rust references. The helpers expose no retention
policy, eviction order, shrink-list ownership, LRU isolation decision, or
final-free selection.

Two adjacent operations remain in C. ``dget()`` remains a header inline
because it is a hot wrapper around ``lockref_get()``. Converting it to an
out-of-line cross-language entry would add a call to a common reference
acquisition path. ``d_mark_dontcache()`` remains in C because it traverses
inode aliases while holding ``i_lock`` and changes inode state.

Important invariants
--------------------

``fast_dput()`` performs the lockref decrement under RCU protection. A failed
attempt takes ``d_lock`` before leaving RCU, then completes or retries the
reference decision under the lock.

A retention decision runs a read memory barrier before inspecting dentry
flags. Unhashed, disconnected, filesystem-deleted, and dontcache dentries are
not retained. A dentry that needs LRU insertion or flag repair must first
enter the locked path.

``dentry_kill()`` obtains the inode lock when required, marks the lockref dead,
runs ``d_prune()``, removes the dentry from the LRU and hash, detaches the
inode, runs ``d_release()``, and then unlinks the dentry tree entry. A
shrink-list member defers the final memory release to the list owner.

The parent lock is acquired before the nested child lock. A child release can
drop the last parent reference, and the return value continues that chain
without recursion.

``d_make_discardable()`` requires the persistent reference, clears the
persistent flag, removes that reference under ``d_lock``, and enters the same
slow eviction path.

``shrink_dentry_list()`` consumes only a thread-private list. A dentry already
detached from the tree is released directly. A live entry still completes the
normal eviction and parent-chain walk.

External names are released through their shared reference count. A normal
dentry is freed immediately only when ``DCACHE_NORCU`` excludes RCU readers.
All other dentry memory is reclaimed through RCU.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Null, final, and shared reference drops.

* External-name and RCU final release.

* Private shrink-list insertion and consumption.

* LRU retention, rotation, and shrink paths.

* Persistent-reference discardability.

* Concurrent lookup and reference drop through the real dcache.

* KASAN, lockdep, ``PROVE_LOCKING``, and PREEMPT_RT.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``226d5c9bd9c9``.

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
Fallback          SMP1                        KUnit 9 of 9
Fallback          SMP4                        KUnit 9 of 9
C-only            SMP1                        KUnit 9 of 9
C-only            SMP4                        KUnit 9 of 9
DEBUG_VFS         SMP1                        KUnit 9 of 9
DEBUG_VFS         SMP4                        KUnit 9 of 9
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, warning, or kernel halt.

The KASAN build used Clang because KASAN and Rust require ``CC_IS_CLANG``
together. Its configuration enabled ``CONFIG_RUST``,
``CONFIG_RUST_DENTRY_LIFECYCLE``, ``CONFIG_KASAN``,
``CONFIG_KASAN_INLINE``, ``CONFIG_PROVE_LOCKING``, ``CONFIG_KUNIT``, and
``CONFIG_DENTRY_LIFECYCLE_KUNIT_TEST``. The Native Rust object contained
``__asan_*`` references, so the KASAN run exercised instrumented Rust code.

The suite covers null drop, final negative-dentry release, shared references,
external names, private shrink lists, LRU shrink, referenced retention,
persistent discardability, and four concurrent lookup and drop workers.
Every case obtains and releases a real rootfs path through ``kern_path()`` and
``path_put()``, so the test also crosses path, mount, superblock, and dcache
lifetime code.

The migrated entries do not allocate dentry objects. Allocation and its
rollback remain in the ``d_alloc()`` C boundary. Final-reference cases enter
the locked slow path, and the concurrent case stresses RCU-protected lockref
drop and lookup behavior.

Native ``rust/kernel.o`` defined all five production entries and all three
private callbacks. Native ``fs/dcache.o`` contained no definition of the
five entries and retained undefined references from C callers. The linked
Native image contained one definition of each entry. Fallback and C-only
``fs/dcache.o`` each defined all five entries.

Native ``Module.symvers`` contained ``dput``, ``d_make_discardable``,
``__move_to_shrink_list``, and ``shrink_dentry_list`` as ordinary exports.
It contained no ``dput_to_list`` entry. External GPL and non-GPL modules
linked all four ordinary exports. Modpost rejected both modules that
referenced ``dput_to_list``.

Disassembly showed complete Rust implementations of ``dput()`` and
``shrink_dentry_list()``. Native ``fs/dcache.o`` contained C callers and no
duplicate definitions, hidden C entry, or public tail-call adapter.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx retained the existing unresolved
``kernel-docs`` and ``getting_llvm`` reference warnings. Strict checkpatch
reported no findings after the permitted new-file ``FILE_PATH_CHANGES``
message was ignored.

The behavior and constraints are covered by the dentry lifecycle KUnit
suite and this page.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The dentry, inode, list-LRU, or external-name layout changes.

* Lockref, RCU, hash-table, or dentry-tree synchronization changes.

* The nested parent, ``d_lock``, and inode ``i_lock`` protocol changes.

* Dentry operation callback selection or accounting changes.

* Kernel Rust gains an inline-safe interface for ``dget()``.

* Measurements show an unacceptable reference-drop or cache-lookup
  regression.
