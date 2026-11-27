.. SPDX-License-Identifier: GPL-2.0

Native Rust VFS port roadmap
============================

Purpose
-------

This page defines the first native Rust migration wave from the core object
lifecycles toward VFS and networking. The sequence is:

credentials -> namespaces -> files -> file descriptors -> dentries -> inodes
-> superblocks -> mounts -> path lookup -> open -> VFS I/O -> sockets ->
socket buffers.

The wave moves production ownership of validation, reference transitions,
rollback, dispatch order, and lifecycle decisions into Rust. It does not
remove the C layouts, callbacks, allocator primitives, locks, RCU primitives,
user-copy helpers, or architecture code that make those decisions possible.

Status values
-------------

``pending``
  The unit has not started.

``in-progress``
  Implementation or validation is active.

``complete``
  The unit meets its ownership, fallback, test, ABI, documentation, and static
  analysis requirements.

``boundary-retained``
  The unit cannot provide a safe native Rust ownership boundary. The evidence,
  risk, and reopening conditions are recorded.

``blocked``
  The unit needs new authority or an external state change.

Wave 1 work packages
--------------------

================  ==========================  =============  =====================================================
Order             Unit                        Status         Primary ownership
================  ==========================  =============  =====================================================
1                 ``direct-entry-audit``      complete       Helper and adapter classification.
2                 ``cred-lifecycle``          complete       Credential creation, commit, abort, and release.
3                 ``nsproxy-lifecycle``       complete       Namespace proxy copy, switch, and teardown.
4                 ``file-lifecycle``          complete       File creation, close, delayed close, and release.
5                 ``fdtable-lifecycle``       complete       FD allocation, growth, lookup, and RCU release.
6                 ``dentry-lifecycle``        complete       Dentry reference, retention, and release.
7                 ``inode-lifecycle``         complete       Inode allocation, reference, LRU, and destruction.
8                 ``superblock-lifecycle``    complete       Superblock allocation, activation, and teardown.
9                 ``vfsmount-lifecycle``      complete       Mount clone, reference, detach, and release.
10                ``path-lookup``             complete       Name resolution control flow and rollback.
11                ``open-entry``              complete       Open dispatch, completion, and cleanup.
12                ``vfs-io``                  pending        Read, write, seek, and operation dispatch.
13                ``socket-lifecycle``        pending        Socket allocation, reference, close, and release.
14                ``skb-lifecycle``           pending        SKB allocation, clone, recycle, and destruction.
================  ==========================  =============  =====================================================

Unit dependencies
-----------------

Credentials and namespaces come first because file creation, security
accounting, and namespace visibility consume both.

Files and file descriptors come before dentries and inodes because VFS object
lifetime tests need a stable open-file path.

Dentries, inodes, superblocks, and mounts form one dependency chain. A later
unit can start only after the earlier unit records its retained C boundary.

Path lookup and open consume the complete object lifetime chain. VFS I/O then
uses the resulting open-file state.

Sockets reuse credential, file, namespace, wait, and socket-buffer ownership.
SKB work depends on page, slab, socket, and network-device accounting
primitives.

Per-unit completion gate
------------------------

Every production unit must satisfy these requirements:

* Rust defines the selected production entry points.
* Native C objects contain no duplicate public entry definition.
* C public functions do not tail-call Rust and call the result migrated.
* C fallback and C-only configurations retain the complete implementation.
* Focused KUnit covers normal transitions, errors, rollback, and final
  release.
* Native, fallback, and C-only builds pass.
* SMP1 and SMP4 boots pass.
* Export class, external module ABI, and symbol placement are checked.
* Disassembly proves that the selected C entry is absent or contains only the
  documented primitive boundary.
* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check`` pass.

Credential, namespace, file, dentry, inode, mount, socket, and SKB units must
also pass KASAN, lockdep, and PREEMPT_RT. They must include a concurrent test,
a failure-injection test, and a cross-layer integration test.

Completion and merge policy
---------------------------

Each unit uses one ``rs-core/<unit>`` branch from the latest ``origin/master``.
A unit may use several focused commits, but unrelated migrations must not
share a commit.

The unit branch is merged to ``rs`` and ``master`` only after its complete
validation matrix passes. The three branches are pushed atomically.

An oversized unit continues until its acceptance gate passes or an
evidence-backed boundary decision replaces the implementation. A failed unit
blocks later units instead of accumulating unverified migration work.
