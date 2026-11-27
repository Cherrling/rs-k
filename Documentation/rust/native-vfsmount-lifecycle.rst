.. SPDX-License-Identifier: GPL-2.0

Native Rust vfsmount lifecycle
==============================

Status and scope
----------------

The ``CONFIG_RUST_VFSMOUNT_LIFECYCLE`` option selects native Rust for mount
allocation and rollback, setup, cloning and propagation linking, reference
acquisition and release, detached mount cleanup, and the choice between task
work and delayed work.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``vfs_create_mount()``

* ``mntget()``

* ``mntput()``

Rust also defines the internal ``clone_mnt()``, ``mnt_clone_internal()``,
``mntput_no_expire()``, and ``mnt_make_shortterm()`` entries. The
``clone_mnt()`` entry is shared with the C mount creation paths.

Rust owns:

* Mount slab, cover, identifier, device-name, and per-CPU counter allocation
  order.

* Rollback after every allocation stage.

* Setup of the root dentry, active superblock reference, superblock instance
  list, and self-parent state.

* Clone flags, peer group allocation, idmap reference acquisition, setup, and
  peer and slave propagation links.

* Reference acquisition and release.

* The fast detached-reference path and full mount-lock final-reference
  serialization.

* Final reference detection, ``MNT_DOOMED`` publication, instance and expiry
  removal, cover removal, and dentry shrink-list handoff.

* The decision to clean an internal mount synchronously, defer a normal mount
  through task work, or use the delayed workqueue.

* Final cleanup ordering for writer warnings, pins, fsnotify, root dentry,
  superblock, mount identifier, and RCU destruction.

* Final idmap, cover, device-name, per-CPU counter, and slab release.

The ordinary exports ``vfs_create_mount()``, ``mntget()``, and ``mntput()``
retain their export class. ``clone_mnt()``, ``mnt_clone_internal()``,
``mntput_no_expire()``, and ``mnt_make_shortterm()`` remain unexported.

Retained C boundaries
---------------------

The shared ``struct mount``, ``struct vfsmount``, cover, mountpoint, namespace,
and propagation layouts remain in C. Native ``fs/namespace.o`` retains 84
narrow helpers for:

* Mount slab, cover, identifier, device-name, and per-CPU counter allocation
  and release.

* Mount field initialization and field access.

* Per-CPU reference count and writer count operations.

* ``mount_lock``, RCU, active superblock, dentry, idmap, and fsnotify
  primitives.

* Mount hash and global instance-list insertion and removal.

* Expiry lists, pins, covers, close and delayed work, and task work.

* Error-pointer, VFS warning, and final RCU primitives.

All 84 helpers have Rust references. The helpers expose no allocation
rollback, setup, clone, reference, final-drop, or cleanup decision.

The C delayed worker owns the global list and delayed work object. It removes
a batch and calls the Rust cleanup entry for each mount. C task work stores
its callback in the existing ``mnt_rcu`` union and calls the Rust cleanup
entry. These are scheduling primitives, not lifecycle adapters.

``umount_tree()``, namespace publication, propagation, mount hash lookup, and
mountpoint lifetime remain in C. Rust receives each final detached mount
reference after those C-bound controllers complete their ownership.

Important invariants
--------------------

``mnt_ns`` is non-refcounted. A non-NULL value guarantees that the namespace
holds a reference that survives the current RCU section. The fast path can
therefore decrement the per-CPU counter without taking ``mount_lock``.

A NULL namespace makes the caller's reference eligible to be the final one.
The slow path takes ``mount_lock``, decrements the counter, samples all CPUs,
and owns final cleanup only when the result is zero. It sets ``MNT_DOOMED``
before releasing RCU read-side protection.

With ``CONFIG_DEBUG_VFS``, the slow path preserves the original fatal
``VFS_BUG_ON(mnt->mnt_ns)`` check immediately before it takes ``mount_lock``.
The check does not fall back to a fast decrement after observing a namespace.

The normal mount cleanup path must preserve this order:

* Writer warning.

* Mount-pin release.

* fsnotify deletion.

* Root dentry release.

* Superblock deactivation.

* Mount identifier release.

* RCU delay.

* idmap, cover, device-name, per-CPU counter, and slab release.

Internal mounts perform cleanup synchronously after the final reference.
Normal detached mounts use task work on a process context. Kernel threads and
task-work failure use the delayed workqueue.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Final task-work cleanup.

* Delayed cleanup from a kernel thread.

* Shared references and concurrent final release.

* Internal clone creation and final cleanup.

* Successful shared clone and peer group publication.

* Shortterm mount transition.

* VFS path lookup and release through the detached mount.

* Allocation rollback after slab, cover, identifier, device-name, and
  per-CPU counter failure.

* Clone peer-group allocation rollback.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``97bc265b64079e93cdf4f3fa7166bc00ebf6bba8``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 11 of 11
Native Rust       SMP4                        KUnit 11 of 11
KASAN Rust        SMP1                        KUnit 11 of 11
KASAN Rust        SMP4                        KUnit 11 of 11
lockdep           SMP1                        KUnit 11 of 11
lockdep           SMP4                        KUnit 11 of 11
PREEMPT_RT        SMP1                        KUnit 11 of 11
PREEMPT_RT        SMP4                        KUnit 11 of 11
Fallback          SMP1                        KUnit 8 of 8
Fallback          SMP4                        KUnit 8 of 8
C-only            SMP1                        KUnit 8 of 8
C-only            SMP4                        KUnit 8 of 8
DEBUG_VFS         SMP1                        KUnit 11 of 11
DEBUG_VFS         SMP4                        KUnit 11 of 11
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, warning, or kernel halt.

The KASAN build uses Clang because Rust and KASAN require
``CONFIG_CC_IS_CLANG``. Its configuration enables ``CONFIG_RUST``,
``CONFIG_RUST_VFSMOUNT_LIFECYCLE``, ``CONFIG_KASAN``,
``CONFIG_KASAN_INLINE``, ``CONFIG_PROVE_LOCKING``, ``CONFIG_KUNIT``, and
``CONFIG_VFSMOUNT_LIFECYCLE_KUNIT_TEST``. The KASAN Rust object contains 17
``__asan_*`` references.

Native ``rust/kernel.o`` defines all seven lifecycle entries and the private
delayed cleanup callback. Native ``fs/namespace.o`` defines none of the seven
entries. Fallback and C-only ``fs/namespace.o`` each define the complete
internal and public implementation set.

Native ``Module.symvers`` contains ordinary exports for
``vfs_create_mount()``, ``mntget()``, and ``mntput()``. It contains no entries
for the other four lifecycle entries. External GPL and non-GPL modules link
all three ordinary exports. Modpost rejects both license classes when a
module references any unexported entry.

Disassembly shows complete Rust implementations of ``vfs_create_mount()``,
``clone_mnt()``, ``mntget()``, ``mntput()``, and the final-reference path.
Native ``fs/namespace.o`` contains only C callers and no duplicate definition
or public tail-call adapter.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` pass. Focused Sphinx retains only existing unresolved
documentation-reference warnings. Strict checkpatch reported no findings.

Risk
----

A wrong final-reference decision can free a mount still visible through an RCU
lookup, mount hash, cover, namespace tree, or dentry path. A wrong clone
rollback can leak a mount identifier, peer group, idmap, dentry, or
superblock reference.

A wrong task-work or delayed-work selection can run filesystem cleanup in an
unsafe context. A wrong cleanup order can access a released root dentry or
superblock.

Reopen conditions
-----------------

Reopen a retained C boundary decision if any of these conditions changes:

* The ``mount``, ``vfsmount``, cover, or mountpoint layout changes.

* Per-CPU reference, writer, namespace, or RCU reference rules change.

* Mount hash, namespace tree, expiry, pin, or cover ownership changes.

* ``umount_tree()`` starts transferring final references by another protocol.

* Task work or delayed work ordering changes.

* Clone propagation linking moves into Rust.

* Dentry, superblock, idmap, fsnotify, or filesystem teardown moves into Rust.
