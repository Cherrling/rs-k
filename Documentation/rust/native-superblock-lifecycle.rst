.. SPDX-License-Identifier: GPL-2.0

Native Rust superblock lifecycle
================================

Status and scope
----------------

The ``CONFIG_RUST_SUPERBLOCK_LIFECYCLE`` option selects native Rust for
superblock allocation and rollback, active and passive reference management,
lookup and reuse, publication, deactivation, death notification, and final
RCU-workqueue destruction.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``deactivate_locked_super()``

* ``deactivate_super()``

* ``sget_fc()``

Rust also defines the unexported ``put_super()`` and
``rust_kill_super_notify()`` entries, and the private ``alloc_super()`` and
``grab_super()`` helpers.

Rust owns:

* Superblock slab allocation and initialization order.

* Cleanup after security, writer-semaphore, shrinker, LRU, and device-table
  allocation failures.

* Active and passive reference transitions.

* Lookup under the global superblock lock, user-namespace validation, and
  exclusive-reuse rejection.

* The temporary-passive-reference trade used to wait for an existing
  superblock to become active or dead.

* The filesystem ``set`` callback failure path and ownership transfer of
  ``fc->s_fs_info``.

* Publication in the global and per-filesystem instance lists.

* Shrinker unregister ordering, filesystem ``kill_sb`` invocation, death
  notification, LRU destruction, and final passive-reference release.

* RCU grace-period scheduling and final workqueue destruction.

The three ordinary exports ``deactivate_locked_super()``,
``deactivate_super()``, and ``sget_fc()`` retain their export class.
``put_super()`` and ``rust_kill_super_notify()`` remain unexported.

Retained C boundaries
---------------------

The shared superblock, filesystem-type, superblock-device, writer, and
callback layouts remain in C. Native ``fs/super.o`` retains 82 narrow helpers
for:

* Superblock slab allocation and shared-layout initialization.

* File-system type, filesystem context, user namespace, flag, identifier, and
  callback access.

* ``s_umount`` and global superblock-lock operations.

* Active and passive reference-count and flag operations.

* Security, fsnotify, user-namespace, writer, shrinker, LRU, and device-table
  primitives.

* Global-list and per-filesystem instance-list publication and removal.

* Filesystem-type reference management and filesystem ``kill_sb`` dispatch.

* RCU, workqueue, wait, warning, and error-pointer primitives.

All 82 helpers have Rust references. The helpers expose no superblock reuse,
active-reference, passive-reference, rollback, publication, deactivation, or
final-free decision.

``generic_shutdown_super()`` remains in C. It serializes dcache shutdown,
filesystem synchronization, error and cgroup writeback cleanup, inode
eviction, security deletion, direct-I/O workqueue destruction, filesystem
``put_super`` callbacks, encryption keyring destruction, busy-inode checks,
and ``SB_DYING`` publication. Rust owns the surrounding active-reference and
final-release path.

Important invariants
--------------------

``s_passive`` protects lookup and lifetime after a superblock leaves the
active reference count. ``s_active`` requires exclusive ``s_umount`` ownership
for its final transition.

A new superblock starts with one active and one passive reference. The passive
reference keeps list unlinking, device-table removal, and death notification
valid while concurrent mount attempts wait.

``sget_fc()`` scans only superblocks that are not ``SB_DEAD``. A matching
superblock must have the requested user namespace and must not be requested
exclusively. The temporary passive reference prevents final release while
``grab_super()`` waits for ``s_umount`` and tries to acquire an active
reference. A dead or inactive superblock is waited for and discarded.

The filesystem `set` callback runs while ``sb_lock`` and the new superblock's
exclusive lock are held. A successful callback must register the superblock
device claim. A failed callback must leave the claim unregistered. The
filesystem context transfers ``s_fs_info`` only on success.

Publication is the final fallible setup step. Rust publishes the superblock
before dropping ``sb_lock``, then takes the filesystem-type reference and
registers the shrinker. The superblock remains in its nascent state until the
filesystem sets ``SB_BORN`` or shutdown sets ``SB_DYING``.

The final active-reference drop frees the shrinker, calls the filesystem
``kill_sb`` callback, publishes ``SB_DEAD``, destroys both LRUs, and releases
the initial passive reference. The final passive drop unlinks both instance
lists, waits for an RCU grace period, and queues final destruction on
``system_wq``.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Creation and final destruction.

* Shared active references.

* Filesystem setup failure.

* Exclusive reuse rejection.

* Four-thread concurrent lookup and release.

* Allocation failure.

* Rollback from security, every writer-semaphore, shrinker, dentry-LRU,
  inode-LRU, and device-table initialization stage.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``c5e2c40bbcfa``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 7 of 7
Native Rust       SMP4                        KUnit 7 of 7
KASAN Rust        SMP1                        KUnit 7 of 7
KASAN Rust        SMP4                        KUnit 7 of 7
lockdep           SMP1                        KUnit 7 of 7
lockdep           SMP4                        KUnit 7 of 7
PREEMPT_RT        SMP1                        KUnit 7 of 7
PREEMPT_RT        SMP4                        KUnit 7 of 7
Fallback          SMP1                        KUnit 5 of 5
Fallback          SMP4                        KUnit 5 of 5
C-only            SMP1                        KUnit 5 of 5
C-only            SMP4                        KUnit 5 of 5
DEBUG_VFS         SMP1                        KUnit 7 of 7
DEBUG_VFS         SMP4                        KUnit 7 of 7
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, warning, or kernel halt.

The KASAN build uses Clang because Rust and KASAN require
``CONFIG_CC_IS_CLANG``. Its configuration enables ``CONFIG_RUST``,
``CONFIG_RUST_SUPERBLOCK_LIFECYCLE``, ``CONFIG_KASAN``,
``CONFIG_KASAN_INLINE``, ``CONFIG_PROVE_LOCKING``, ``CONFIG_KUNIT``, and
``CONFIG_SUPERBLOCK_LIFECYCLE_KUNIT_TEST``. The Native Rust object contains
17 ``__asan_*`` references, so the KASAN run exercises instrumented Rust
code.

The native-only allocation failure case verifies that failed slab allocation
returns ``-ENOMEM`` without publishing a superblock. The native-only rollback
case injects nine failures after initial object creation and verifies that
every partially initialized resource is destroyed exactly once.

Native ``rust/kernel.o`` defines the three exported entries, both unexported
entries, and the private allocation and destruction helpers. Native
``fs/super.o`` defines none of the five production entries. Fallback and
C-only ``fs/super.o`` each define the complete implementation.

Native ``Module.symvers`` contains ordinary exports for
``deactivate_locked_super()``, ``deactivate_super()``, and ``sget_fc()``. It
contains no entries for ``put_super()`` or ``rust_kill_super_notify()``.
External GPL and non-GPL modules link all three ordinary exports. Modpost
rejects both license classes when a module references either unexported
entry.

Disassembly shows complete Rust implementations of ``sget_fc()``,
``deactivate_super()``, and the final destruction path. Native ``fs/super.o``
contains C callers and no duplicate public definition or public tail-call
adapter.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` pass. Focused Sphinx retains only existing unresolved
documentation-reference warnings. Strict checkpatch has no findings in the
changed patch except the permitted new-file ``FILE_PATH_CHANGES`` warning.

Review conditions
-----------------

Reopen a retained C boundary decision if any of these conditions changes:

* The superblock, filesystem-type, superblock-device, writer, or callback
  layout changes.

* Active, passive, device-claim, or filesystem-type reference rules change.

* ``sb_lock``, ``s_umount``, RCU, workqueue, or death-notification ordering
  changes.

* ``generic_shutdown_super()`` starts owning a Rust lifecycle decision.

* Shrinker, LRU, security, fsnotify, or filesystem callback teardown moves
  into Rust.
