.. SPDX-License-Identifier: GPL-2.0

Native Rust open entry orchestration
====================================

Status and scope
----------------

The ``CONFIG_RUST_OPEN_ENTRY`` option selects native Rust for file open
dispatch, final-component lookup and creation, atomic-open coordination,
RCU and revalidation retries, open completion, and failed-open cleanup.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines:

* ``finish_open()``

* ``finish_no_open()``

* ``vfs_open()``

* ``vfs_lookup_open()``

* ``dentry_create()``

* ``do_file_open()``

* ``do_file_open_root()``

Rust also owns the private open flow for:

* ``path_openat()`` dispatch and cleanup.

* O_PATH and O_TMPFILE control flow.

* Final-component lookup, lookup revalidation, and creation.

* Atomic-open callback coordination.

* The RCU, ref-walk, and revalidation retry sequence.

* ``-EOPENSTALE`` conversion and delegation-break retry.

* Parent locking and mount write acquisition.

* Permission, sticky-directory, truncation, security, and audit ordering.

* ``do_dentry_open()`` initialization and rollback.

* Creation and open notification selection.

The ordinary exports ``finish_open()``, ``finish_no_open()``, and
``dentry_create()`` remain available to GPL and non-GPL modules.
``vfs_lookup_open()`` remains GPL-only and restricted to the ``module:nfsd``
namespace. The internal ``vfs_open()``, ``do_file_open()``, and
``do_file_open_root()`` entries remain unexported.

Retained C boundaries
---------------------

``include/linux/open-entry-rust.h`` defines the shared open layout and the
narrow C helpers. C retains the file, dentry, inode, path, and ``nameidata``
layouts.

C retains 76 open helpers for:

* File and dentry field access and error-pointer handling.

* Parent inode locking, mount write references, and dead-directory checks.

* Dentry lookup, parallel lookup, revalidation, invalidation, and release.

* Filesystem lookup, create, atomic-open, and open callbacks.

* Permission checks and idmap and mode preparation.

* Lease and delegation handling.

* LSM, fsnotify, audit, and truncation callbacks.

* File operation reference, write access, and final open cleanup.

Rust references all 76 helpers. These helpers expose no open dispatch,
retry policy, completion order, or public C adapter.

C also retains actual ``vfs_tmpfile()`` and ``vfs_create()`` creation.
Rust owns their O_TMPFILE and final-component orchestration, but these C
creation primitives remain C boundaries in this package.

Important invariants
--------------------

The first open attempt uses ``LOOKUP_RCU``. An ``-ECHILD`` result is retried
without forced RCU mode. An ``-ESTALE`` result is retried with
``LOOKUP_REVAL``. ``-EOPENSTALE`` becomes ``-ECHILD`` during an RCU attempt
and ``-ESTALE`` during a ref-walk attempt.

The parent inode is locked shared for lookup and exclusively for creation.
The exclusive path remains selected when ``may_o_create()`` produces an
error or clears ``O_CREAT``.

Each lookup attempt clears ``FMODE_CREATED``. An atomic-open callback that
opens a different dentry transfers a new reference. A negative result from
``finish_no_open()`` releases the lookup reference before Rust returns
``-ENOENT``.

``finish_open()`` rejects a file that already has ``FMODE_OPENED`` set. It
emits a warning and returns ``-EBUSY`` before it changes the file path.
The original C implementation terminates the kernel with ``BUG_ON()``.

A delegated parent releases its lock and mount write reference before the
delegation wait. A successful wait retries the final-component lookup. A
failed wait returns the wait error.

``do_dentry_open()`` acquires a second path reference through ``path_get()``.
Every failure after that point releases path access, file operations, and
the path reference in the same order as C. An O_DIRECT rejection leaves the
completed open file for ``fput_close()`` cleanup.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Ordinary open, O_PATH, missing-file, create, exclusive create, and
  O_TMPFILE behavior.

* Direct ``vfs_open()`` and O_DIRECT rejection.

* ``finish_open()`` and ``finish_no_open()``.

* Atomic open, missing atomic lookup, and Atomic ``dentry_create()``.

* Direct ``vfs_lookup_open()`` lookup, creation, exclusive failure, and
  missing-file failure.

* Injected ``-ECHILD`` and ``-EOPENSTALE`` retries.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit
``1a5cfdaa17e389b4ed319382d04d6630367918ea``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 10 of 10
Native Rust       SMP4                        KUnit 10 of 10
KASAN Rust        SMP1                        KUnit 10 of 10
KASAN Rust        SMP4                        KUnit 10 of 10
lockdep           SMP1                        KUnit 10 of 10
lockdep           SMP4                        KUnit 10 of 10
PREEMPT_RT        SMP1                        KUnit 10 of 10
PREEMPT_RT        SMP4                        KUnit 10 of 10
Fallback          SMP1                        KUnit 9 of 9
Fallback          SMP4                        KUnit 9 of 9
C-only            SMP1                        KUnit 9 of 9
C-only            SMP4                        KUnit 9 of 9
DEBUG_VFS         SMP1                        KUnit 10 of 10
DEBUG_VFS         SMP4                        KUnit 10 of 10
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, warning, or kernel halt.

KASAN SMP4 exposed an older path lookup defect during O_TMPFILE testing.
Rust called the directory check after a failed lookup, when the final dentry
could be null. The directory check now runs only after a successful walk.
KASAN SMP1 and SMP4 then passed.

Native ``rust/kernel.o`` defines all seven production entries. Native
``fs/open.o`` and ``fs/namei.o`` define none of them. Fallback and C-only
``fs/open.o`` and ``fs/namei.o`` retain complete definitions. The fallback
Rust object defines none.

Native ``Module.symvers`` records ordinary exports for ``finish_open()``,
``finish_no_open()``, and ``dentry_create()``. It records
``vfs_lookup_open()`` as GPL-only in the ``module:nfsd`` namespace. The
other three entries have no export entry.

External GPL and non-GPL modules linked the three ordinary exports. A
GPL module named ``nfsd`` linked ``vfs_lookup_open()``. A non-GPL module
named ``nfsd`` failed modpost as a GPL-incompatible user of that symbol.
Both license classes failed modpost with ``vfs_open()``,
``do_file_open()``, and ``do_file_open_root()``.

Disassembly showed complete Rust implementations of all seven entries.
No public C tail-call adapter exists.

Risk
----

A wrong reference transition can leak or double-release a dentry, path,
inode, file operation table, or mount write reference.

A wrong retry or delegation order can reuse terminated walk state, retry
without the required revalidation mode, or return while the parent lock is
held.

A wrong completion order can open a file before permission checks, truncate
an existing file after an exclusive-open failure, or omit required
``fsnotify()`` and audit events.

Reopen conditions
-----------------

Reopen a retained C boundary decision if any of these conditions changes:

* The file, dentry, inode, path, ``nameidata``, or ``open_flags`` layout
  changes.

* Filesystem lookup, create, atomic-open, or file operation callbacks move
  into Rust.

* Permission, idmap, LSM, fsnotify, audit, lease, or delegation contracts
  change.

* RCU and ref-walk open retry semantics change.

* ``vfs_tmpfile()`` or ``vfs_create()`` moves into Rust.

* The ``vfs_lookup_open()`` export namespace or license changes.
