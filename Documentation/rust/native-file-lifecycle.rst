.. SPDX-License-Identifier: GPL-2.0

Native Rust file lifecycle
==========================

Status and scope
----------------

The ``CONFIG_RUST_FILE_LIFECYCLE`` option selects native Rust for file
allocation, pseudo-file creation, cloning, final release, and reference-drop
dispatch.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``alloc_empty_file()``

* ``alloc_file_pseudo()``

* ``alloc_file_clone()``

* ``__fput()``

* ``fput()``

* ``__fput_sync()``

* ``fput_close_sync()``

* ``fput_close()``

Rust owns:

* The file population limit check, privileged bypass, exact recount, and
  limit warning.

* File slab allocation, initialization failure cleanup, and accounting.

* Pseudo path composition and path rollback when file allocation fails.

* Clone path acquisition and mapping inheritance.

* Final opened-file callback ordering.

* Character-device release selection and mount dissolution selection.

* Security, credential, accounting, backing-file, and slab release order.

* The decision to release immediately, through task work, or through delayed
  work.

* Synchronous and close-optimized reference-drop paths.

``alloc_file_pseudo()``, ``fput()``, and ``__fput_sync()`` retain their
ordinary export class. The other five entries retain the unexported class.

Retained C boundaries
---------------------

The shared ``struct file`` and ``struct file_operations`` layouts remain in
C. Native ``fs/file_table.o`` retains 53 narrow helpers for:

* File slab allocation and release.

* File field initialization and current credential access.

* Per-CPU file accounting and limit diagnostics.

* Path and mount access, acquisition, and release.

* Security, fsnotify, eventpoll, lock, fasync, release, owner, and access
  callbacks.

* Dentry and inode access.

* Task-work and delayed-work queue operations.

All 53 Native helpers are referenced by ``rust/kernel.o``. The helpers expose
no file-limit policy, rollback order, final callback order, or deferred
release selection.

Important invariants
--------------------

``alloc_empty_file()`` checks the population limit before allocation. A
non-privileged caller at the approximate limit performs the exact recount.
A successful initialization increments the population count exactly once.
An initialization failure frees the slab object before returning the error.

``alloc_file_pseudo()`` releases the temporary pseudo path when file
allocation fails. A successful path transfers into the new opened file.
fsnotify remains disabled by default until a caller explicitly changes the
mode.

``alloc_file_clone()`` initializes the new file from the base path, acquires
one additional path reference, and inherits the base mapping.

``__fput()`` releases fsnotify, eventpoll, file locks, security state,
fasync, file operations, owner state, file access, dentry, and mount state in
the historical order. It releases the file slab object last.

``fput()`` and ``fput_close()`` defer final opened-file cleanup. A normal task
uses task work when possible. Interrupt context and kernel threads use
delayed work. An unopened or non-backing file releases immediately.

``__fput_sync()`` and ``fput_close_sync()`` run final cleanup in the caller
after the final reference is released.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Every injected limit, allocation, initialization, and pseudo path failure.

* Empty-file allocation and synchronous release.

* Pseudo-file release with fasync and synchronous close.

* Clone path and mapping inheritance.

* Multiple-reference and concurrent reference release.

* Real kernel-thread delayed fput.

* KASAN, lockdep, ``PROVE_LOCKING``, and PREEMPT_RT.

* Symbol placement, export class, external module ABI, and disassembly
  checks.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``0069d1d6cefa``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 8 of 8
Native Rust       SMP4                        KUnit 8 of 8
KASAN             SMP1                        KUnit 8 of 8
KASAN             SMP4                        KUnit 8 of 8
lockdep           SMP1                        KUnit 8 of 8
lockdep           SMP4                        KUnit 8 of 8
PREEMPT_RT        SMP1                        KUnit 8 of 8
PREEMPT_RT        SMP4                        KUnit 8 of 8
Fallback          SMP1                        KUnit 6 of 6
Fallback          SMP4                        KUnit 6 of 6
C-only            SMP1                        KUnit 6 of 6
C-only            SMP4                        KUnit 6 of 6
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, or kernel halt.

The Native suite covers every injected failure stage, task-work final
release, empty-file allocation and synchronous release, pseudo-file release
with fasync, clone inheritance, multiple references, real kernel-thread
delayed release through ``fput_close()``, and four concurrent reference
drops.

Native ``rust/kernel.o`` defined all eight entries. Native
``fs/file_table.o`` defined none of them and retained 53 helpers. The
fallback and C-only objects defined the seven external entries and the local
static ``__fput()`` implementation. The linked Native image had no unresolved
target symbol.

Native ``Module.symvers`` contained ``alloc_file_pseudo``, ``fput``, and
``__fput_sync`` as ordinary exports. It contained none of the other five
entries. External GPL and non-GPL modules linked all three exported entries.
Modpost rejected both modules that referenced the five unexported entries.

Disassembly showed complete Rust implementations of
``alloc_file_pseudo()``, ``__fput()``, and ``fput()``. Native
``fs/file_table.o`` contained references from C callers but no duplicate
definitions or public C tail-call adapter.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` passed. Strict checkpatch retained only the permitted
new-file ``FILE_PATH_CHANGES`` warning.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``struct file`` or ``struct file_operations`` layout changes.

* File population accounting or privileged bypass policy changes.

* Path, dentry, mount, security, eventpoll, lock, cdev, owner, or access
  release semantics change.

* Task-work or delayed-work context rules change.

* Kernel Rust gains safe direct interfaces for a retained primitive.

* Measurements show that the helper calls cause an unacceptable lifecycle
  regression.
