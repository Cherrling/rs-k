.. SPDX-License-Identifier: GPL-2.0

Native Rust file descriptor table lifecycle
===========================================

Status and scope
----------------

The ``CONFIG_RUST_FDTABLE_LIFECYCLE`` option selects native Rust for file
descriptor table expansion, copying, descriptor allocation, installation,
close, final files-structure release, and RCU-protected reference lookup.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``dup_fd()``

* ``put_files_struct()``

* ``exit_files()``

* ``__get_unused_fd_flags()``

* ``get_unused_fd_flags()``

* ``put_unused_fd()``

* ``fd_install()``

* ``file_close_fd_locked()``

* ``close_fd()``

* ``file_close_fd()``

* ``get_file_rcu()``

* ``get_file_active()``

* ``fget()``

* ``fget_raw()``

* ``fget_task()``

* ``fget_task_next()``

* ``fdget()``

* ``fdget_raw()``

Rust owns:

* Descriptor-table capacity rounding, sysctl limits, and allocation-size
  checks.

* Expansion retry, wait, copy, publication, RCU reclamation, and barriers.

* Bitmap selection for free descriptors and the two-level full-word shortcut.

* Descriptor reservation, next-free-descriptor caching, and close-on-exec
  state.

* Lockless installation and the lock-taking resize fallback.

* Descriptor removal, close dispatch, and final table teardown order.

* Files-structure clone sizing, punched-hole size selection, descriptor
  copying, and file-reference transfer.

* RCU file-reference acquisition, table revalidation, mode filtering, and
  retry behavior.

* Borrowed and cloned lightweight descriptor selection.

The ordinary exports ``get_unused_fd_flags()``, ``put_unused_fd()``,
``fd_install()``, ``close_fd()``, ``fget()``, ``fget_raw()``,
``fget_task_next()``, and ``fdget()`` retain their export class. The GPL
exports ``get_file_rcu()`` and ``get_file_active()`` retain theirs. The other
entries remain unexported.

Retained C boundaries
---------------------

The shared ``struct fdtable``, ``struct files_struct``, ``struct fd_range``,
and ``struct fd`` layouts remain in C. Native ``fs/file.o`` retains narrow
helpers for:

* Files-structure and fdtable slab allocation.

* Dynamic fd-array and bitmap allocation.

* Bitmap copy, bit search, bit mutation, and descriptor-array clearing.

* Spinlocks, wait queues, atomics, task locks, and task file access.

* RCU read-side sections, publication, grace periods, and callbacks.

* Descriptor slot loads and stores.

* File references, mode reads, and ``filp_close()``.

* Resource-limit and sysctl reads.

* Scheduler rescheduling.

The helpers expose no descriptor-selection policy, expansion retry, clone
rollback, close ordering, RCU lookup retry, or files-structure destruction
policy.

Important invariants
--------------------

``alloc_fdtable()`` rounds capacity to the original allocation quantum and
keeps it aligned to one bitmap word. A lowered sysctl limit can reduce the
capacity only when it still satisfies the request. Allocation size remains
bounded before dynamic allocation.

``expand_fdtable()`` drops ``file_lock`` while allocating, synchronizes RCU
when the files structure is shared, reacquires the lock, copies the latest
table, publishes the new table, and releases the old dynamic table through
RCU.

``fd_install()`` uses scheduler RCU protection unless a resize is active.
The slow path holds ``file_lock`` and verifies that the reserved descriptor
still has no file entry. A successful install transfers the file reference.

``close_fd()`` removes the descriptor under ``file_lock``, releases the lock,
and then runs ``filp_close()``. Final files-structure release closes every
open descriptor before releasing dynamic tables and the files object.

``dup_fd()`` recalculates the required size after every allocation because
another task can resize or modify the shared source table. Every copied file
entry receives one reference. Entries reserved but not yet installed have no
file and are cleared in the clone.

``fget_files_rcu()`` acquires a file reference before validating both the
descriptor entry and the published fdtable. A mismatch releases the new
reference and retries. Mode filtering releases the reference before returning
an error.

``fdget()`` borrows a file only when the files-structure count is one. A
shared files structure receives a real reference and sets the cloned flag.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Descriptor allocation, installation, borrowed and cloned lookup, and close.

* Descriptor-table expansion across the embedded capacity.

* Files-structure clone and final release.

* Injected allocation failures and rollback.

* Concurrent RCU lookup across table expansion and descriptor replacement.

* KASAN, lockdep, ``PROVE_LOCKING``, and PREEMPT_RT.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Native Rust, lockdep, KASAN, and PREEMPT_RT each passed SMP1 and SMP4 with
6 of 6 cases. Rust fallback and C-only each passed SMP1 and SMP4 with 3 of 3
cases. All twelve final runs had no matching BUG, Oops, KASAN report,
warning, panic, or failed KUnit line.

A separate ``CONFIG_DEBUG_VFS=y`` build passed SMP1 and SMP4 with 6 of 6
cases.

The native cases cover borrowed and cloned ``fdget()``, allocation-failure
rollback, concurrent RCU lookup, lookup racing with close, table expansion,
and files-structure duplication and release. The fallback and C-only cases
cover lookup racing with close, table expansion, and duplication and release.

Native ``rust/kernel.o`` defined all 18 production entries. Native
``fs/file.o`` defined none. ``vmlinux`` contains one definition of each
entry. Native ``fs/file.o`` retains 57 helpers. The Rust implementation
references every helper.

Rust fallback and C-only ``fs/file.o`` each define all 18 entries. The Rust
fallback object defines none.

Native ``Module.symvers`` contains the eight ordinary exports and the two
``EXPORT_SYMBOL_GPL`` entries. It does not contain the eight unexported
entries. External modules linked the ordinary exports with both licenses.
A GPL module linked both GPL-only entries. A proprietary GPL-only probe and
both unexported probes failed modpost.

Disassembly of ``dup_fd()``, ``fd_install()``, and ``fget()`` showed complete
Rust control flow with calls to narrow C helpers. No selected entry is a
single tail call to a native C implementation.

The final Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` checks passed. Focused Sphinx reported the existing
unresolved ``kernel-docs`` reference in ``Documentation/rust/index.rst``.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The fdtable, files-structure, descriptor, or fd-range layout changes.

* Descriptor bitmap or full-word bitmap semantics change.

* Resize synchronization, RCU publication, or task file access changes.

* File reference semantics or close callbacks change.

* Kernel Rust gains safe direct interfaces for a retained primitive.

* Measurements show an unacceptable lookup or lifecycle regression.
