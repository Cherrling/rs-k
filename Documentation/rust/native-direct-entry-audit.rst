.. SPDX-License-Identifier: GPL-2.0

Native Rust direct-entry audit
==============================

Purpose
-------

This page records the helper and public ABI boundary used by the VFS-centered
native Rust migration wave described in :doc:`native-port-roadmap`.

The audit has two goals. First, keep C primitives that are necessary to
express safe Rust operations. Second, identify temporary C ABI adapters that
must not be counted as production Rust ownership.

Baseline
--------

The initial audit used commit
``5d5c5c4fa34ed9be5e6e6ba6ef2595084da6d96a``.

At that commit:

* ``rust/helpers`` contained 88 C files.
* 84 helper files contained ``rust_helper_*`` symbols.
* 729 unique literal ``rust_helper_*`` spellings were present.
* The helper files contained 740 ``__rust_helper`` marker occurrences.
* ``include/linux`` contained 93 ``*-rust*.h`` boundary headers.

The literal helper count includes macro-generated fragments. It is an
inventory baseline, not a count of linked function symbols.

The four helper source files without a ``rust_helper_*`` name are
``build_assert.c``, ``helpers.c``, ``list_lru.c``, and ``lockref.c``. They
provide assertions, shared helpers, or macro-backed operations.

Audit method
------------

The helper inventory counts C sources below ``rust/helpers`` recursively.
This includes ``rust/helpers/net/genetlink.c``.

The public adapter scan excludes tests and tools. It searches production C and
header files for calls to ``rust_*`` while excluding ``rust_helper_*``:

.. code-block:: console

   rg --pcre2 \
     '(?<![A-Za-z0-9_])rust_(?!helper_)[A-Za-z0-9_]+\s*\(' \
     --glob '*.[ch]' --glob '!rust/**' \
     --glob '!**/tests/**' --glob '!tools/**'

The search produces adapter candidates. A candidate is a public ABI adapter
only after source review confirms these conditions:

* The caller defines or preserves a public production entry.
* The caller forwards the entry or its state-transition policy to Rust.
* The caller does not own the final lifecycle or error decision.

The review also checks for the inverse case. A Rust-defined public entry can
call a C helper that later calls another Rust decision helper. That C
round-trip does not make the public entry a C adapter when Rust still defines
the public symbol.

Helper inventory by ownership area
----------------------------------

All 88 helper sources are covered by the following ownership areas. The
classification is a baseline for the next package. Each package must review
every helper that it newly uses and record any per-function exception.

.. list-table::
   :widths: 24 61 15
   :header-rows: 1

   * - Area
     - Files
     - Default class
   * - CPU and ABI primitives
     - ``atomic.c``, ``atomic_ext.c``, ``barrier.c``, ``bitops.c``,
       ``bitmap.c``, ``build_assert.c``, ``build_bug.c``, ``bug.c``,
       ``err.c``, ``interrupt.c``, ``io.c``, ``jump_label.c``,
       ``processor.c``, ``string.c``, ``sync.c``, and ``uaccess.c``.
     - ``primitive``
   * - Locking and scheduling primitives
     - ``completion.c``, ``cpu.c``, ``cpumask.c``, ``list.c``,
       ``lockref.c``, ``mutex.c``, ``poll.c``, ``rbtree.c``, ``rcu.c``,
       ``rcuwait.c``, ``refcount.c``, ``spinlock.c``, ``srcu.c``,
       ``time.c``, ``wait.c``, and ``workqueue.c``.
     - Mixed ``primitive`` and ``layout-access``
   * - Task, PID, security, and signal
     - ``complete_signal.c``, ``cred.c``, ``itimer.c``, ``pid.c``,
       ``pid_allocation.c``, ``pid_association.c``,
       ``pid_namespace.c``, ``pid_number.c``, ``prepare_signal.c``,
       ``security.c``, ``send_signal.c``, ``signal.c``,
       ``signal_dequeue.c``, ``signal_exit.c``,
       ``signal_pending_recalc.c``, ``signal_retarget.c``,
       ``signal_wake_state.c``, ``task.c``, ``task_exit_notify.c``,
       ``task_group_exit_sync.c``, and ``task_release.c``.
     - Mixed ``primitive`` and ``layout-access``
   * - Memory and I/O primitives
     - ``list_lru.c``, ``mm.c``, ``page.c``, ``scatterlist.c``,
       ``slab.c``, and ``vmalloc.c``.
     - Mixed ``primitive`` and ``layout-access``
   * - Shared data structures
     - ``genradix.c``, ``klist.c``, ``maple_tree.c``, and
       ``xarray.c``.
     - ``layout-access``
   * - VFS and networking
     - ``fs.c`` and ``net/genetlink.c``.
     - ``primitive``
   * - Device and driver ABI
     - ``auxiliary.c``, ``blk.c``, ``clk.c``, ``cpufreq.c``,
       ``device.c``, ``dma-resv.c``, ``dma.c``, ``drm.c``,
       ``drm_gpuvm.c``, ``fwctl.c``, ``gpu.c``, ``irq.c``, ``of.c``,
       ``pci.c``, ``platform.c``, ``property.c``, ``pwm.c``,
       ``regulator.c``, ``serdev.c``, and ``usb.c``.
     - ``primitive``
   * - Consumer-specific bridges
     - ``binder.c``.
     - ``primitive``
   * - KUnit support
     - ``kunit.c``.
     - ``test-only``
   * - Shared macro definitions
     - ``helpers.c``.
     - ``primitive``

``primitive`` means the helper implements a compiler, architecture, locking,
allocation, callback, or ABI contract. ``layout-access`` means the helper
exposes a shared data-structure representation. ``test-only`` means the
helper is not part of a production state machine.

The baseline classification does not grant permanent retention. A helper that
reads or writes a shared object only because Rust lacks a safe representation
is ``temporary-access``. It becomes eligible for removal when the owning Rust
type provides the same layout and access checks.

Candidate call-site audit
-------------------------

The production scan found 144 ``rust_*`` call sites in 16 C files after
excluding helpers, tests, tools, and unrelated identifier substrings. Source
review gives these dispositions:

.. list-table::
   :widths: 40 8 52
   :header-rows: 1

   * - C file
     - Sites
     - Disposition
   * - ``lib/xarray.c``
     - 28
     - Mixed public ABI adapters and retained XArray primitives.
   * - ``mm/vma.c``
     - 26
     - Mixed VMA public and internal ABI adapters.
   * - ``kernel/time/hrtimer.c``
     - 26
     - Mixed native decision helpers, tracepoint adapters, and fallback
       branches. The selected public entries are Rust-defined.
   * - ``lib/maple_tree.c``
     - 19
     - Maple Tree traversal, store, and preallocation ABI adapters.
   * - ``mm/mremap.c``
     - 8
     - mremap entry and operation adapters.
   * - ``drivers/android/binder/rust_binderfs.c``
     - 11
     - Binder filesystem and device glue around the Rust Binder
       implementation. This is not an adapter for the VFS migration spine.
   * - ``kernel/locking/rwsem.c``
     - 11
     - Rust-owned public rwsem entries use C internal decision bridges.
       These are not public ABI adapters.
   * - ``kernel/locking/mutex.c``
     - 5
     - Rust-owned public mutex entries use C internal decision bridges.
       These are not public ABI adapters.
   * - ``mm/mprotect.c``
     - 2
     - mprotect entry and fixup adapters.
   * - ``mm/mmap.c``
     - 2
     - ``brk`` and ``do_mmap()`` adapters.
   * - ``lib/vsprintf.c``
     - 1
     - C caller uses a Rust formatting service.
   * - ``lib/kobject.c``
     - 1
     - C internal initialization step uses a Rust native policy helper.
   * - ``kernel/signal.c``
     - 1
     - C exit path uses the native retarget state machine.
   * - ``kernel/irq_work.c``
     - 1
     - C lazy execution path uses the native IRQ-work state machine.
   * - ``kernel/fork.c``
     - 1
     - C finalization path uses the native mm final state machine.
   * - ``kernel/exit.c``
     - 1
     - ``do_exit()`` uses the native group-exit synchronization state
       machine.

The 16 files and 144 sites account for every production candidate returned by
the scan. A call site is not a promotion claim. It records whether the C side
defines a public adapter, performs a primitive call into Rust policy, or acts
as an intentional consumer bridge.

Public ABI adapter debt
-----------------------

The following groups preserve C public or internal ABI adapters while Rust
owns substantial policy. They are not counted as direct native entries.

.. list-table::
   :widths: 19 46 35
   :header-rows: 1

   * - Area
     - Adapter boundary
     - Clear condition
   * - User mapping
     - ``brk``, ``do_mmap()``, and the opaque ``rust_brk`` and
       ``rust_mmap_entry`` state wrappers.
     - Rust exports both public symbols. The C object has no definitions.
       C keeps only primitive helpers.
   * - VMA region creation
     - The wrappers documented in :doc:`mmap-region`,
       :doc:`mmap-region-create`, :doc:`mmap-region-setup`,
       :doc:`mmap-region-prepare`, :doc:`mmap-region-new-vma`,
       :doc:`mmap-region-file`, and :doc:`mmap-region-complete`.
     - Rust exports the selected public entry. The state wrappers become
       typed helpers with no public symbol.
   * - VMA modification
     - The operation adapters in :doc:`vma-copy`, :doc:`vma-expand`,
       :doc:`vma-split`, :doc:`vma-shrink`, :doc:`vma-modify`, and
       :doc:`vma-merge-commit`.
     - Rust exports the public or internal caller symbol and owns the entry
       state. C retains narrow primitives.
   * - mremap
     - ``do_mremap()``, ``move_page_tables()``, ``move_pgt_entry()``,
       ``copy_vma()``, ``move_vma()``, ``mremap_to()``, ``mremap_at()``,
       and ``remap_move()``.
     - Rust exports the public entry or internal caller symbol. C has no
       dispatch wrapper.
   * - mprotect
     - ``do_mprotect_pkey()`` and ``mprotect_fixup()``.
     - Rust exports both selected entries. The syscall and fixup wrappers
       are removed.
   * - mm finalization
     - ``__mmput()`` forwards to ``rust_mm_final()``.
     - ``__mmput()`` is a Rust definition. C retains only the workqueue
       callback ABI.
   * - Maple Tree traversal
     - ``mas_next*()``, ``mas_find*()``, ``mas_prev*()``,
       ``mas_preallocate()``, and store helpers.
     - Rust exports each public symbol. The C traversal wrappers are absent.
   * - XArray
     - Load, store, mark, destroy, and free-node adapters documented in the
       native XArray pages.
     - Rust exports each public symbol. C wrappers are absent.
   * - Signal control
     - Inline ``signal_wake_up()``, ``ptrace_signal_wake_up()``,
       ``retarget_shared_pending()``, and ``synchronize_group_exit()``.
     - Rust owns a compatible public entry or the boundary receives an
       evidence-backed ``boundary-retained`` decision.

Every row clears only after the native object, fallback object, final image,
export class, external module ABI, and disassembly checks agree. An adapter
that disappears from the native image but remains in fallback is expected. An
adapter that disappears from both configurations is a fallback regression.

The following are not public ABI adapter debt:

* C callback thunks that recover an object from an embedded callback node and
  call a Rust-defined public entry.
* C inline fast paths that call Rust policy while Rust owns the public symbol.
* Rust helper calls into C for layout access, locks, RCU, allocation, user
  copies, callbacks, or architecture operations.
* Binder filesystem glue that consumes the independent Rust Binder object.
* Test-only helpers and KUnit observers.

Boundary classes
----------------

``primitive``
  A C operation that implements an architecture or compiler contract. Common
  examples are atomics, barriers, spinlocks, interrupt state, RCU entry and
  exit, user copies, MMIO, allocation, page mapping, and callback ABI.

``layout-access``
  A narrow C accessor for a shared object layout that Rust cannot yet model
  safely. The helper does not select the migrated state transition.

``temporary-access``
  A C helper that reads or updates internal state for an earlier Rust state
  machine. It can be removed after Rust gains a safe representation or a
  narrower typed accessor.

``public-abi-adapter``
  A C public function that forwards to Rust. It preserves the C entry symbol
  but does not provide native Rust ownership of the public entry. This class
  is not migrated.

Current helper boundary
-----------------------

The first Rust APIs used by the VFS spine depend on these C helper families:

====================  ======  ================================================
Helper file           Count   Role
====================  ======  ================================================
``cred.c``            2       Credential reference and release wrappers.
``security.c``        7       LSM and Binder credential checks.
``task.c``            10      Current task, task reference, UID, and user NS.
``rcu.c``             2       RCU read-side entry and exit.
``workqueue.c``       1       Lockdep-aware work initialization.
``fs.c``              1       File reference increment.
``pid_namespace.c``   3       PID namespace reference and task lookup.
``mm.c``              9       Address-space reference and mmap lock primitives.
``page.c``            4       Page allocation and local mapping.
``slab.c``            2       Aligned slab reallocation.
``vmalloc.c``         2       Virtual allocation checks and reallocation.
====================  ======  ================================================

These helpers are primitive or layout-access boundaries during Wave 1. They
remain in C even when Rust owns the calling lifecycle.

Target adapter audit
--------------------

The primary C files for the 14 units do not currently contain a native
``rust_<unit>_*`` public lifecycle adapter. They still contain the complete C
implementations. A package therefore starts from a clean direct-entry state.

The target source and export inventory is:

.. list-table::
   :widths: 22 23 10 10 10 25
   :header-rows: 1

   * - Unit
     - C source
     - Total
     - Ordinary
     - GPL
     - Adapter state
   * - Credential
     - ``kernel/cred.c``
     - 9
     - 9
     - 0
     - None
   * - Namespace
     - ``kernel/nsproxy.c``
     - 0
     - 0
     - 0
     - None
   * - File table
     - ``fs/file_table.c``
     - 9
     - 3
     - 6
     - None
   * - File descriptors
     - ``fs/file.c``
     - 13
     - 9
     - 4
     - None
   * - Dentries
     - ``fs/dcache.c``
     - 48
     - 46
     - 2
     - None
   * - Inodes
     - ``fs/inode.c``
     - 67
     - 63
     - 4
     - None
   * - Superblocks
     - ``fs/super.c``
     - 30
     - 23
     - 7
     - None
   * - Mount namespace
     - ``fs/namespace.c``
     - 26
     - 14
     - 12
     - None
   * - Path lookup
     - ``fs/namei.c``
     - 64
     - 62
     - 1
     - None. One entry uses
       ``EXPORT_SYMBOL_FOR_MODULES``.
   * - Open
     - ``fs/open.c``
     - 13
     - 10
     - 3
     - None
   * - VFS I/O
     - ``fs/read_write.c``
     - 24
     - 21
     - 2
     - None. One entry uses
       ``EXPORT_SYMBOL_FOR_MODULES``.
   * - Socket
     - ``net/socket.c``
     - 34
     - 31
     - 3
     - None
   * - Sock
     - ``net/core/sock.c``
     - 95
     - 84
     - 11
     - None
   * - SKB
     - ``net/core/skbuff.c``
     - 125
     - 88
     - 36
     - None. One entry uses
       ``EXPORT_SYMBOL_NS_GPL``.

The 15 target files contain 557 export directives. They comprise 463 ordinary
exports, 91 GPL exports, and three special export classes. The special classes
must be preserved during migration rather than folded into the generated Rust
GPL class.

The audit also searched every target source for the substring ``rust_``.
There were no matches. The same sources contain no target-specific
``CONFIG_RUST_*`` selection. Their current C implementations therefore have no
hidden native Rust entry and no C-to-Rust public adapter.

.. list-table::
   :widths: 20 45 35
   :header-rows: 1

   * - Unit
     - Existing helper dependency
     - Current adapter state
   * - Credential
     - ``cred``, ``security``, ``task``, and ``rcu``.
     - None. C owns the lifecycle.
   * - Namespace
     - ``task``, ``pid_namespace``, and ``cred``.
     - None. C owns the lifecycle.
   * - File and fdtable
     - ``fs``, ``task``, ``security``, ``rcu``, and ``workqueue``.
     - None. Rust wrappers use C reference primitives.
   * - Dentry and inode
     - Lockref, refcount, list LRU, RCU, and VFS callbacks.
     - None. C owns the lifecycle.
   * - Superblock and mount
     - ``fs``, ``rcu``, ``workqueue``, security, mount, and path
       primitives.
     - None. C owns the lifecycle.
   * - Path, open, and VFS I/O
     - Security, user-copy, iterator, file-operation, and page-cache
       primitives.
     - None. C owns the lifecycle.
   * - Socket, sock, and SKB
     - ``task``, ``security``, ``rcu``, ``workqueue``, ``page``,
       ``slab``, and network callbacks.
     - None. C owns the lifecycle.

The audit searched calls to ``rust_*`` outside ``rust/helpers``. Existing
adapter debt remains in earlier MM and VMA orchestration paths, including
C wrappers that initialize opaque state and call ``rust_mmap_*``,
``rust_mremap_*``, and ``rust_vma_*`` entries. Those components are not part
of the VFS ownership chain and remain recorded as pending or
boundary-retained by the existing coverage ledger.

Direct-entry rules
------------------

A package must reject any implementation that adds a public C function with
the following shape:

.. code-block:: c

   int exported_entry(args)
   {
       return rust_exported_entry(args);
   }

The package may keep a C callback, layout accessor, lock helper, or callback
registration function. The C object must not own the exported lifecycle
entry or make the final state transition.

Each package must inspect the following evidence before promotion:

* The Native C object defines none of the migrated public entry points.
* The Rust object defines every migrated public entry point.
* The final ``vmlinux`` contains one production definition of each entry.
* The C fallback and C-only objects retain the original implementation.
* The export class matches the original symbol policy.
* The disassembly contains no hidden public C tail-call adapter.

Temporary helper rule
---------------------

A new temporary helper must satisfy all of these conditions:

* It performs one layout access, primitive operation, or callback registration.
* It contains no lifecycle policy.
* Its Rust caller documents the complete operation that uses it.
* Its header name identifies the migrated component.
* Its removal is recorded with an observable condition.

A helper that selects a retry, release path, reference transition, ownership
transfer, callback order, or rollback decision is production policy and must
remain in Rust.

Validation record
-----------------

The final audit revision passed:

* ``make SPHINXDIRS=rust htmldocs``.
* ``make LLVM=1 rustfmtcheck``.
* ``make LLVM=1 CLIPPY=1 rust/kernel.o``.
* ``make LLVM=1 rustdoc``.
* ``scripts/checkpatch.pl --strict``.
* ``git diff --check``.

Focused Sphinx reported only the existing cross-reference warnings in
``Documentation/rust/index.rst`` and ``Documentation/rust/quick-start.rst``.
Strict checkpatch reported only the permitted ``FILE_PATH_CHANGES`` warning
for the two new documents.

This package changes documentation only. Native, fallback, C-only, QEMU,
KASAN, lockdep, PREEMPT_RT, runtime integration, and external module ABI
matrices do not execute any changed artifact. The first production package
must run those matrices.
