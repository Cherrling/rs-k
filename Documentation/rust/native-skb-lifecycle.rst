.. SPDX-License-Identifier: GPL-2.0

Native Rust skb lifecycle
=========================

Status and scope
----------------

The ``CONFIG_RUST_SKB_LIFECYCLE`` option selects native Rust for generic skb
allocation, cloning, reference release, list drop, NAPI consumption, and final
shell release. The option depends on ``CONFIG_NET``, ``CONFIG_RUST``, and
``CONFIG_TRACEPOINTS``. The complete C implementation remains available when
this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines:

* ``__alloc_skb()``

* ``__consume_stateless_skb()``

* ``__kfree_skb()``

* ``sk_skb_reason_drop()``

* ``kfree_skb_list_reason()``

* ``consume_skb()``

* ``napi_consume_skb()``

* ``skb_clone()``

* ``alloc_skb_for_msg()``

* ``skb_morph()``

Rust owns allocation cache selection, the NUMA fallback decision, the NAPI
cache fast path, memalloc-socket handling, shell and head allocation failure
cleanup, fclone-group setup, reference transitions, consume and drop tracing,
bulk list release, NAPI deferred-free selection, clone setup, message-wrapper
setup, and final shell sequencing.

The ordinary exports are ``__alloc_skb()``, ``__kfree_skb()``,
``sk_skb_reason_drop()``, ``kfree_skb_list_reason()``, ``consume_skb()``,
``napi_consume_skb()``, and ``skb_clone()``. The GPL-only exports are
``alloc_skb_for_msg()`` and ``skb_morph()``.
``__consume_stateless_skb()`` remains unexported.

Retained C boundaries
---------------------

``include/linux/skb-lifecycle-rust.h`` defines the narrow helper ABI. Rust
uses the helpers for allocation, cache access, shared layouts, fclone state,
reference counts, tracepoint delivery, destructor callbacks, NAPI caches,
bulk slab release, and deferred free.

C retains the skb, shared-info, fclone, socket, NAPI-cache, page-fragment,
DMA, and page-pool layouts. C also retains slab and page-fragment allocation,
NAPI cache storage and bulk reclaim, DMA and page-pool recycling, tracepoint
delivery, destructor callbacks, protocol-header primitives, KASAN cache
operations, and driver interfaces.

Native ``net/core/skbuff.o`` contains no definition of the ten selected
entries. It retains the export metadata and internal call relocations for
the nine exported entries.

Important invariants
--------------------

An fclone allocation must use the fclone cache. Other allocations may use the
generic cache or a suitable NAPI cache. A specific NUMA node request cannot use
the local NAPI shell cache.

An allocation failure releases every acquired resource in reverse order. A
shell failure releases no object. A head failure releases the shell to its
original cache. An fclone shell becomes the original member of a new fclone
group only after head allocation succeeds.

``SKB_CONSUMED`` selects the consume tracepoint. Every other valid reason
selects the drop tracepoint. The tracepoint delivery remains in C and reports
the Rust helper call site.

The bulk list path processes at most 16 generic shells for one slab bulk
release call. Fclone members use their dedicated group release.

``napi_consume_skb()`` with a non-zero budget requires softirq context. A
cross-CPU skb with no shared state takes the deferred-free path before the
final reference transition. Other final references use the NAPI shell cache
unless the shell belongs to an fclone group.

A clone resets list, socket, and destructor state. It copies the protocol and
shared header state, initializes one user reference, increments the shared
data reference, and marks the source as cloned.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Normal allocation, head allocation failure, shell allocation failure, and
  clone allocation failure.

* Shared-data clones, fclone fast clones, reference drop, stateless consume,
  linked-list bulk release, and NAPI budget paths.

* Message wrappers, morph, socket-owned skbs, and cross-layer credential,
  file, socket, and skb release.

* Concurrent allocation, clone, and release.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit
``30078df997e51439aee1202977cc970a025ae595``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 13 of 13
Native Rust       SMP4                        KUnit 13 of 13
KASAN Rust        SMP1                        KUnit 13 of 13
KASAN Rust        SMP4                        KUnit 13 of 13
lockdep           SMP1                        KUnit 13 of 13
lockdep           SMP4                        KUnit 13 of 13
PREEMPT_RT        SMP1                        KUnit 13 of 13
PREEMPT_RT        SMP4                        KUnit 13 of 13
Fallback          SMP1                        KUnit 12 of 12
Fallback          SMP4                        KUnit 12 of 12
C-only            SMP1                        KUnit 12 of 12
C-only            SMP4                        KUnit 12 of 12
DEBUG_VFS         SMP1                        KUnit 13 of 13
DEBUG_VFS         SMP4                        KUnit 13 of 13
================  ==========================  =========================

Every boot completed without a failed KUnit case, BUG, oops, KASAN report, or
lockdep complaint. Fallback and C-only omit the Rust-only failure-injection
case. The KASAN build uses Clang 21 with KASAN, lockdep, and
``PROVE_LOCKING``. Its validation configuration disables the unrelated
``CONFIG_FRAME_WARN`` check to accommodate instrumentation growth in existing
drivers and tests. ``CONFIG_WERROR`` remains enabled.

Native ``rust/kernel.o`` defines all ten entries. Native
``net/core/skbuff.o`` defines none. Fallback and C-only
``net/core/skbuff.o`` retain all ten definitions.

Native ``Module.symvers`` records seven ordinary exports and two GPL-only
exports. ``__consume_stateless_skb()`` has no export record. External GPL
modules linked all nine exports. External non-GPL modules linked the seven
ordinary exports. Modpost rejected a non-GPL module that used the two GPL-only
exports and rejected both license classes for the unexported entry.

Disassembly shows complete Rust implementations of every selected entry.
Native C contains no duplicate function definition and no public tail-call
adapter.

Focused Sphinx completed with only the existing ``kernel-docs`` document
warning. It does not reference this unit.
Strict checkpatch reports only the generic new-file MAINTAINERS prompt.
MAINTAINERS contains every new file from this unit.

Risk
----

A wrong cache or NUMA decision can corrupt a per-CPU NAPI cache or allocate
from the wrong node. A wrong failure sequence can leak a shell or return a
shell to the wrong cache.

A wrong reference transition can free an skb during shared-data use. A wrong
fclone decision can misuse a paired shell. A wrong destructor or shared-data
release order can leak a socket, page fragment, DMA mapping, or page-pool
reference.

A wrong NAPI decision can enter sleepable code in softirq context, use a
per-CPU cache from the wrong CPU, or defer a shared skb. A wrong tracepoint
decision can report a successful consume as a drop.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The skb, shared-info, fclone, NAPI-cache, socket, DMA, page-fragment, or
  page-pool layout changes.

* Allocation flags, cache selection, NUMA fallback, or memalloc policy
  changes.

* Destructor, shared-data, fclone, or reference transition semantics change.

* NAPI deferred-free, cross-CPU, or per-CPU cache policy changes.

* Tracepoint names, drop reasons, or caller-location semantics change.

* Kernel Rust gains safe direct interfaces for a retained primitive.
