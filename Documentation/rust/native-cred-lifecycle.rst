.. SPDX-License-Identifier: GPL-2.0

Native Rust credential lifecycle
================================

Status and scope
----------------

The ``CONFIG_RUST_CRED_LIFECYCLE`` option selects native Rust
implementations for credential allocation, preparation, commit, abort,
fork and exec copies, reference transfer, comparison, and final release.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when the Rust option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``__put_cred()``

* ``exit_creds()``

* ``get_task_cred()``

* ``cred_alloc_blank()``

* ``prepare_creds()``

* ``prepare_exec_creds()``

* ``copy_creds()``

* ``commit_creds()``

* ``abort_creds()``

* ``cred_fscmp()``

* ``set_cred_ucounts()``

* ``prepare_kernel_cred()``

* ``set_security_override()``

* ``set_create_files_as()``

Rust owns:

* Credential allocation initialization and partial-failure cleanup.

* Group, user, user-namespace, ucounts, LSM, and keyring dependency
  acquisition.

* Fork sharing and keyring inheritance selection.

* Exec identity and keyring transitions.

* Credential publication and task pointer updates.

* Dumpability, namespace, proc connector, and RLIMIT_NPROC decisions.

* RCU deferred release and final resource release order.

The nine historical ordinary exports remain ``EXPORT_SYMBOL``:
``__put_cred()``, ``get_task_cred()``, ``prepare_creds()``,
``commit_creds()``, ``abort_creds()``, ``cred_fscmp()``,
``prepare_kernel_cred()``, ``set_security_override()``, and
``set_create_files_as()``.

The other five entry points retain their original unexported class.

Retained C boundaries
---------------------

The ``struct cred`` and related type layouts remain in C. Native
``kernel/cred.o`` retains the credential slab cache and narrow helpers for:

* Atomic reference counts and ``non_rcu`` state.

* RCU read locking and task pointer publication.

* Group, user, user-namespace, ucounts, and key references.

* LSM and key callback invocation.

* Task, namespace, proc connector, ucount, dumpability, and architecture
  barrier primitives.

The helpers expose no credential lifecycle policy. They do not choose
reference transitions, publication order, rollback order, or final release
order.

Important invariants
--------------------

``prepare_creds()`` copies the current subjective credential. It initializes
one reference, acquires every copied dependency, clears copied LSM and
``non_rcu`` state, then prepares new LSM state. A failure releases exactly the
dependencies already acquired.

``commit_creds()`` requires identical subjective and objective credentials on
the current task. It adds the subjective reference, updates dumpability before
publication, publishes both RCU pointers, moves ucount and namespace
accounting, sends identity notifications, and releases the two old task
references.

Final release runs after the usage count reaches zero. The non-RCU path frees
directly. Other credentials use ``call_rcu()`` and free only in the callback.
The release order is LSM state, keyrings, groups, user, ucounts, user
namespace, then the credential slab object.

Validation gate
---------------

The unit must pass:

* Native Rust builds and boots with one and four CPUs.

* Rust fallback builds with ``CONFIG_RUST_CRED_LIFECYCLE=n``.

* C-only builds with ``CONFIG_RUST=n``.

* KASAN, lockdep, ``PROVE_LOCKING``, and PREEMPT_RT.

* Allocation, LSM, ucounts, user namespace, and copy failure injection.

* RCU and non-RCU final release, concurrent prepare and commit, real
  kthread fork and exit, and ``CLONE_THREAD`` credential sharing.

* Cross-namespace capability policy and supplementary group comparison.

* Symbol placement, disassembly, export class, and external module ABI
  checks.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit
``2b6b9c211bdae6eaeaa318fe77be13a7332571fd``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 15 of 15
Native Rust       SMP4                        KUnit 15 of 15
Native lockdep    SMP1                        KUnit 16 of 16
Native lockdep    SMP4                        KUnit 16 of 16
Fallback          SMP1                        KUnit 14 of 14
Fallback          SMP4                        KUnit 14 of 14
C-only            SMP1                        KUnit 14 of 14
C-only            SMP4                        KUnit 14 of 14
KASAN             SMP1                        KUnit 16 of 16
KASAN             SMP4                        KUnit 16 of 16
PREEMPT_RT        SMP1                        KUnit 16 of 16
PREEMPT_RT        SMP4                        KUnit 16 of 16
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
warning, KASAN report, oops, lockdep complaint, or kernel panic.

The Native suite covers commit policy, injection of each allocation and
callback failure, blank credentials, prepare and abort, ucounts replacement,
exec preparation, kernel credentials, objective references, identical
commit, filesystem and supplementary-group comparison, RCU and direct
release, invalid file-creation IDs, concurrent prepare and commit, real
kthread fork and exit, and ``CLONE_THREAD`` credential sharing.

Native ``rust/kernel.o`` defined all 14 production entries. Native
``kernel/cred.o`` defined none of them and retained 46 narrow helpers. The
fallback and C-only ``kernel/cred.o`` objects defined all 14 entries. Native
disassembly showed a complete Rust ``commit_creds()`` implementation and no
C tail-call adapter.

All nine ordinary symbols remained ``EXPORT_SYMBOL`` in ``Module.symvers``.
External GPL and non-GPL modules each linked against all nine symbols.

The C-only matrix also exposed a separate pre-existing mutex fallback compile
error. A focused prerequisite commit passes the missing ``prelude_done``
argument to ``__mutex_lock_common()``.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx retained two warnings that are
unrelated to this page: an unknown ``kernel-docs`` document and the historical
``getting_llvm`` label.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``struct cred`` layout or embedded RCU union changes.

* Task credential publication, RCU release, or reference-count ordering
  changes.

* LSM, keyring, user namespace, ucounts, or proc connector callback semantics
  change.

* Kernel Rust gains safe direct interfaces for a retained primitive.

* Measurements show that the helper calls cause an unacceptable lifecycle
  regression.
