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
    native-port-roadmap
    native-direct-entry-audit
    native-cred-lifecycle
    native-nsproxy-lifecycle
    native-file-lifecycle
    native-fdtable-lifecycle
    native-dentry-lifecycle
    native-inode-lifecycle
    native-superblock-lifecycle
    native-vfsmount-lifecycle
    native-path-lookup
    native-port-coverage
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
    signal-exit
    signal-struct-copy
    signal-wake-select
    signal-wake-state
    task-exit-notify
    pid-allocation
    pid-association
    pid-number
    pid-lifecycle
    native-irq-event
    native-irq-spurious
    native-irq-resend
    native-irq-work
    native-rcuwait
    native-closure
    native-task-work
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
    native-btree-entry
    native-btree-traversal
    native-btree-structural
    native-btree-visitors
    native-list-sort
    native-plist
    native-rbtree
    native-interval-tree
    native-kref
    native-dec-and-lock
    native-klist
    native-dmapool
    native-llist
    native-kobject
    native-kset
    native-class
    native-bus
    native-driver
    native-device
    native-list-lru
    native-kfifo
    native-objpool
    native-genradix
    native-maple-tree-load
    native-maple-tree-walk
    native-maple-tree-next
    native-maple-tree-prev
    native-maple-tree-find
    native-maple-tree-prealloc
    native-maple-tree-node-alloc
    native-maple-tree-store
    native-maple-tree-node-store
    native-maple-tree-spanning-store
    native-maple-tree-leaf-gap
    native-maple-tree-node-copy
    native-maple-tree-split-store
    native-maple-tree-rebalance-store
    native-maple-tree-root-store
    native-idr-alloc
    native-idr-next
    native-xarray-alloc
    native-xarray-create
    native-xarray-create-range
    native-xarray-delete-node
    native-xarray-destroy
    native-xarray-expand
    native-xarray-find
    native-xarray-find-conflict
    native-xarray-find-marked
    native-xarray-free-nodes
    native-xarray-load
    native-xarray-marks
    native-xarray-next-prev
    native-xarray-nomem
    native-xarray-pause
    native-xarray-range-store
    native-xarray-split
    native-xarray-squash
    native-xarray-store
    native-xarray-try-split
    native-percpu-refcount
    refcount
    native-lockref
    swait

You can also find learning materials for Rust in its section in
:doc:`../process/kernel-docs`.
