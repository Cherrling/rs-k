.. SPDX-License-Identifier: GPL-2.0

Native Rust VFS I/O orchestration
=================================

Status and scope
----------------

The ``CONFIG_RUST_VFS_IO`` option selects native Rust for synchronous read,
write, seek, and range verification dispatch.

The option depends on ``CONFIG_RUST`` and
``CONFIG_RUST_FILE_LIFECYCLE``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines:

* ``vfs_llseek()``

* ``rw_verify_area()``

* ``vfs_read()``

* ``vfs_write()``

Rust owns:

* File read, write, and seek capability checks.

* Seek whences dispatch to the file operation callback.

* Count overflow, negative offset, unsigned offset, and signed-limit checks.

* User-address validation selection and failure ordering.

* The ``MAX_RW_COUNT`` clamp.

* Legacy read and write callback selection.

* Synchronous ``read_iter`` and ``write_iter`` callback selection.

* Short-read and short-write return handling.

* Successful fsnotify selection and task accounting order.

* Write-freeze entry and exit bracketing around write callbacks.

* Error return conversion and complete read and write failure paths.

``vfs_llseek()`` and ``rw_verify_area()`` retain their ordinary export class.
``vfs_read()`` and ``vfs_write()`` remain unexported.

Retained C boundaries
---------------------

``include/linux/vfs-io-rust.h`` defines the shared constants and 12 narrow C
helpers. C retains the file, inode, superblock, ``file_operations``, iovec,
and kiocb layouts.

C retains these primitives:

* User-address validation.

* Unsigned-offset capability lookup.

* LSM permission checks and fsnotify range permission checks.

* fsnotify access and modification notifications.

* Superblock write-freeze entry and exit.

* Synchronous iovec and kiocb construction.

* Page-cache, filesystem, and file operation callbacks.

* Current-task read and write throughput accounting.

Rust references all 12 Native helpers. The helpers expose no read, write, or
seek dispatch policy.

Important invariants
--------------------

``vfs_read()`` and ``vfs_write()`` check access mode before capability.
Missing access returns ``-EBADF``. Missing callback capability returns
``-EINVAL``. User-address validation runs before range verification, matching
the original C order.

Range verification rejects counts larger than ``SSIZE_MAX``. A negative
position requires an unsigned-offset file operation. A count that reaches the
magnitude of a negative position returns ``-EOVERFLOW``. A signed position
that wraps below zero requires unsigned offsets.

The count clamp occurs after range verification and before callback dispatch.
This preserves the original range checks for signed files.

A legacy callback takes precedence over its iterator callback. A successful
read emits access notification before read accounting. A successful write
emits modification notification before write accounting.

``file_start_write()`` brackets every write callback, including the missing
callback error path. Matching ``file_end_write()`` runs after write
accounting. Read paths never enter the write-freeze section.

``vfs_llseek()`` rejects a file without seek capability with ``-ESPIPE``.
Valid seek calls dispatch directly to the file operation callback.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Legacy read and write callbacks.

* Iterator read and write callbacks.

* Missing file mode, callback capability, and callback errors.

* Bad user ranges.

* ``MAX_RW_COUNT`` clamping for read and write.

* Count overflow, negative offsets, signed limits, unsigned offsets, and valid
  ranges.

* Seek dispatch and missing seek capability.

* Write-freeze callback observation and balanced entry and exit.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit
``f054ad5e583b484c7faa4a930bc86571140de014``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 20 of 20
Native Rust       SMP4                        KUnit 20 of 20
KASAN Rust        SMP1                        KUnit 20 of 20
KASAN Rust        SMP4                        KUnit 20 of 20
lockdep           SMP1                        KUnit 20 of 20
lockdep           SMP4                        KUnit 20 of 20
PREEMPT_RT        SMP1                        KUnit 20 of 20
PREEMPT_RT        SMP4                        KUnit 20 of 20
Fallback          SMP1                        KUnit 20 of 20
Fallback          SMP4                        KUnit 20 of 20
C-only            SMP1                        KUnit 20 of 20
C-only            SMP4                        KUnit 20 of 20
DEBUG_VFS         SMP1                        KUnit 20 of 20
DEBUG_VFS         SMP4                        KUnit 20 of 20
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, or warning.

Native ``rust/kernel.o`` defines all four production entries. Native
``fs/read_write.o`` defines none and references all four from C callers.
Fallback and C-only ``fs/read_write.o`` retain complete definitions. Their
Rust objects define none.

Native ``Module.symvers`` records ordinary exports for ``vfs_llseek()`` and
``rw_verify_area()``. It contains no export entries for ``vfs_read()`` or
``vfs_write()``. External GPL and non-GPL modules linked both ordinary
exports. Modpost rejected both license classes when a module referenced the
two unexported entries.

Disassembly shows complete Rust implementations of all four entries. Native
``fs/read_write.o`` contains no duplicate definition and no public C
tail-call adapter.

The initial KUnit bring-up exposed two invalid test fixture states. The first
test changed an ``O_RDWR`` file to read-only mode without balancing inode read
accounting. The second set seek capability on an empty operation table. The
fixtures now select flags from the overridden mode and test only reachable
production states.

Risk
----

A wrong range check can pass an overflowing user range to a filesystem. A
wrong callback selection can route an operation to the wrong callback. A
wrong notification or accounting order can change audit visibility or task
I/O statistics.

A missing write-freeze exit can hold a superblock write section indefinitely.
A mismatched legacy and iterator callback selection can violate filesystem
operation contracts.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The file, inode, superblock, ``file_operations``, iovec, or kiocb layout
  changes.

* User-address, LSM, fsnotify, task accounting, or write-freeze semantics
  change.

* ``MAX_RW_COUNT`` or signed-offset policy changes.

* Kernel Rust gains safe direct interfaces for a retained primitive.

* Measurements show that the helper calls cause an unacceptable I/O
  regression.
