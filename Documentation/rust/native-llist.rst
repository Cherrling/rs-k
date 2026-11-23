.. SPDX-License-Identifier: GPL-2.0

Native Rust lock-less lists
===========================

Status and scope
----------------

``CONFIG_RUST_LLIST`` selects native Rust for the out-of-line lock-less list
algorithms. Rust directly defines and exports the public
``llist_del_first()``, ``llist_del_first_this()``, and
``llist_reverse_order()`` entry points.

``lib/llist.c`` has no production implementation in the native
configuration. The complete C implementation remains available when the
option is disabled.

Ownership
---------

Rust owns:

* The first-node compare-exchange retry loop.
* Empty-list decisions.
* Conditional deletion of a given first node.
* Retry decisions after a concurrent producer changes the list head.
* Detached-chain traversal and reversal.
* Public symbol definitions and GPL exports.

C retains:

* Intrusive ``llist_head`` and ``llist_node`` layouts.
* Inline producer insertion and unordered operations.
* The shared C ABI used by those intrusive structures and inline operations.

Important invariants
--------------------

``llist_del_first()`` supports multiple producers and one concurrent
first-node consumer without a lock. Rust reloads the list head through the
compare-exchange helper until the operation succeeds or observes an empty
list.

``llist_del_first_this()`` permits concurrent calls with different target
nodes. A target that is not the current first node remains linked.

``llist_reverse_order()`` operates only on a detached chain. It does not
read a shared list head and does not participate in producer concurrency.

Rust uses the kernel ``Atomic`` abstraction for acquire loads, relaxed pointer
loads and stores, and compare-exchange operations. The architecture-specific
implementations of those operations remain at the atomic boundary. Rust owns
all retry and state-transition decisions.

Validation gate
---------------

The unit must pass these checks before integration:

* Empty-list deletion.
* Newest-first deletion order.
* Conditional first-node deletion success and rejection.
* Empty, single-node, and multi-node chain reversal.
* Concurrent producers with one first-node consumer.
* Complete node accounting with no loss or duplication.
* Native Rust, fallback, and C-only builds.
* One-CPU and multi-CPU execution.
* KASAN, lockdep, and PREEMPT_RT execution.
* Rustfmt, Clippy, Rustdoc, focused Sphinx generation, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

The final source passes the ``llist`` KUnit suite in these configurations:

* Native Rust, SMP4 and SMP1: 4 of 4 tests passed.
* Rust fallback, SMP4 and SMP1: 4 of 4 tests passed.
* C only, SMP4 and SMP1: 4 of 4 tests passed.
* Native Rust with KASAN, SMP4 and SMP1: 4 of 4 tests passed.
* Native Rust with lockdep, SMP4 and SMP1: 4 of 4 tests passed.
* Native Rust with PREEMPT_RT, SMP4 and SMP1: 4 of 4 tests passed.

The native Rust SMP4 image also passes the affected core regression suites:

* ``task_work``: 6 of 6 tests passed.
* ``irq_work``: 7 of 7 tests passed.
* ``list_lru``: 10 of 10 tests passed.
* ``lockref``: 4 of 4 tests passed.
* ``klist``: 11 of 11 tests passed.

The KASAN, lockdep, and PREEMPT_RT logs contain no KASAN report, BUG, warning,
Oops, lock inversion, recursive locking report, or RCU stall. The KASAN build
reports the existing Maple Tree objtool ``noreturn`` advisory.

``rust/kernel.o`` defines ``llist_del_first``, ``llist_del_first_this``, and
``llist_reverse_order``. The native ``lib/llist.o`` defines no llist symbol.
The fallback and C-only objects define all three public symbols. Native,
KASAN, lockdep, and PREEMPT_RT images contain no ``rust_llist_*`` symbol.
Disassembly maps the public functions directly to Rust-owned loops and atomic
operations.

Rustfmt, Clippy, Rustdoc, focused Sphinx generation, and ``git diff --check``
pass. Sphinx reports only existing reference warnings and no new warning from
the lock-less list documentation.

Review conditions
-----------------

Reopen the C boundary decision if any of these conditions changes:

* The unordered inline llist operations become out-of-line.
* The lock-less list consumer serialization rules change.
* First-node deletion gains a lock-based fallback.
* The kernel ``Atomic`` pointer operations change their LKMM semantics.
* Architecture compare-exchange support or NMI-safety requirements change.
