.. SPDX-License-Identifier: GPL-2.0

Rust
====

Documentation related to Rust within the kernel. To start using Rust
in the kernel, please read the quick-start.rst guide.


Code documentation
------------------

Given a kernel configuration, the kernel may generate Rust code documentation,
i.e. HTML rendered by the ``rustdoc`` tool.

.. only:: rustdoc and html

	This kernel documentation was built with `Rust code documentation
	<rustdoc/kernel/index.html>`_.

.. only:: not rustdoc and html

	This kernel documentation was not built with Rust code documentation.

A pregenerated version is provided at:

	https://rust.docs.kernel.org

Please see the :ref:`Code documentation <rust_code_documentation>` section for
more details.

.. toctree::
    :maxdepth: 1

    quick-start
    general-information
    coding-guidelines
    arch-support
    testing
    dup-task-struct
    task-teardown
    task-release
    task-group-exit-sync
    complete-signal
    send-signal
    prepare-signal
    signal-dequeue
    signal-pending-recalc
    signal-retarget
    signal-struct-copy
    signal-wake-select
    signal-wake-state
    task-exit-notify
    pid-allocation
    pid-association
    pid-number
    pid-lifecycle
    mm-lifetime
    mm-teardown
    brk
    mmap-entry
    mmap-region
    mmap-region-complete
    mmap-region-create
    mmap-region-file
    mmap-region-new-vma
    mmap-region-prepare
    mmap-region-setup
    mprotect
    exit-mmap
    vma-unmap
    vma-munmap-complete
    vma-munmap-gather
    vma-merge-compat
    vma-merge-commit
    vma-merge-existing
    vma-merge-new-range
    vma-prepare-complete
    vma-region-teardown
    vma-copy
    move-pgt-entry
    move-page-tables
    mremap-batch
    mremap-copy
    mremap-entry
    mremap-move
    mremap-to
    mremap-resize
    vma-expand
    vma-shrink
    vma-modify
    vma-split
    copy-mm
    dup-mmap
    sched
    cpupri
    itimer
    timerqueue
    hrtimer
    completion
    mutex
    rwsem
    rwlock
    semaphore
    waitqueue
    wait-bit
    native-find-bit
    native-bitmap-ops
    native-kfifo
    native-genradix
    native-xarray-load
    native-xarray-store
    native-percpu-refcount
    refcount
    swait

You can also find learning materials for Rust in its section in
:doc:`../process/kernel-docs`.
