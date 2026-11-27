.. SPDX-License-Identifier: GPL-2.0

Native Rust path lookup orchestration
=====================================

Status and scope
----------------

The ``CONFIG_RUST_PATH_LOOKUP`` option selects native Rust for filename and
parent path lookup, RCU and revalidation retries, scoped completion, directory
checks, audit dispatch, path transfer, and the exported path lookup wrappers.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines:

* ``path_lookupat()``

* ``filename_lookup()``

* ``filename_parentat()``

* ``kern_path()``

* ``vfs_path_lookup()``

* ``vfs_path_parent_lookup()``

Rust owns:

* RCU lookup initiation, ref-walk retry after ``-ECHILD``, and revalidation
  retry after ``-ESTALE``.

* The lookup and parent-lookup entry loops.

* Starting-point mount traversal through ``LOOKUP_DOWN``.

* Final mountpoint traversal through ``LOOKUP_MOUNTPOINT``.

* Successful walk completion, scoped escape rejection, and directory checks.

* The order of path transfer and ``terminate_walk()`` cleanup.

* Parent name and last-component type transfer.

* Successful inode and parent audit dispatch.

* Kernel filename acquisition and release for the exported wrappers.

* The three ordinary public exports:

  * ``kern_path()``

  * ``vfs_path_lookup()``

  * ``vfs_path_parent_lookup()``

The internal ``path_lookupat()``, ``filename_lookup()``, and
``filename_parentat()`` entries remain unexported.

Retained C boundaries
---------------------

``struct nameidata`` remains a C layout boundary in
``include/linux/path-lookup-rust.h``. Rust allocates the layout on its stack
and passes it to narrow C helpers. Rust does not inspect randomized fields
directly.

C retains 23 path lookup helpers for:

* ``nameidata`` initialization, restoration, and field access.

* ``path_init()`` and the RCU or ref-walk starting-point selection.

* ``link_path_walk()``, ``lookup_last()``, and ``handle_lookup_down()``.

* ``complete_walk()`` and ``terminate_walk()``.

* Path transfer, path release, error-pointer handling, and audit delivery.

* Kernel filename allocation and release.

All 23 helpers have Rust references. These helpers expose no retry policy,
entry-loop ordering, parent output selection, audit selection, or exported C
adapter.

C also retains name hashing, component lookup, RCU and ref-walk traversal,
mount crossing, symlink handling, filesystem ``d_revalidate()``,
``d_weak_revalidate()``, ``d_hash()``, and permission callbacks.

Important invariants
--------------------

The first filename or parent lookup attempt uses ``LOOKUP_RCU``. An
``-ECHILD`` result is retried once without forced RCU mode. An ``-ESTALE``
result is retried with ``LOOKUP_REVAL``.

A successful attempt transfers the referenced path out of ``nameidata``
before ``terminate_walk()`` runs. Failed attempts leave the output path
untouched.

The parent lookup entry emits its last component and its type only after a
successful completion. ``vfs_path_parent_lookup()`` releases the returned
parent path when the last component is not ``LAST_NORM``.

The exported wrappers release the temporary kernel ``struct filename`` on
every non-error return path. Error pointers from ``getname_kernel()`` are
returned directly and are not passed to ``putname()``.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Relative filename lookup and missing-component failure.

* Directory requirement enforcement.

* ``LOOKUP_IN_ROOT`` escape clamping and ``LOOKUP_BENEATH`` escape rejection.

* ``LOOKUP_DOWN`` and ``LOOKUP_MOUNTPOINT`` traversal.

* Parent lookup and invalid final-component rejection.

* ``-ECHILD`` ref-walk retry and ``-ESTALE`` revalidation retry.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``024f67415839dea08127303c87065d507d2fd09f``.

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

Native ``rust/kernel.o`` defines all six lifecycle entries. Native
``fs/namei.o`` defines none of them. Fallback and C-only ``fs/namei.o`` each
retain complete definitions for the public wrappers and filename lookup.

Native ``Module.symvers`` contains ordinary exports only for ``kern_path()``,
``vfs_path_lookup()``, and ``vfs_path_parent_lookup()``. External GPL and
non-GPL modules link all three exports. Modpost rejects both license classes
when a module references an unexported lookup entry.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` pass.

Risk
----

A wrong retry order can reuse a terminated walk without the mandatory RCU or
revalidation mode. A wrong success decision can return an unreferenced path,
leak a referenced path, or audit a partial lookup.

A wrong completion or cleanup order can leave an RCU read-side section active,
drop a parent path twice, or expose a scoped escape.

Reopen conditions
-----------------

Reopen a retained C boundary decision if any of these conditions changes:

* The ``nameidata`` or saved-link layout changes.

* Randomized layout support becomes a required kernel configuration.

* ``path_init()`` or successful-walk completion moves into Rust.

* Component lookup, mount crossing, symlink resolution, or dentry
  revalidation moves into Rust.

* RCU or ref-walk retry contracts change.

* Kernel filename allocation or audit ownership changes.

