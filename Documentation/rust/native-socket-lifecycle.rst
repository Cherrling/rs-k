.. SPDX-License-Identifier: GPL-2.0

Native Rust socket and sock lifecycle
=====================================

Status and scope
----------------

The ``CONFIG_RUST_SOCKET_LIFECYCLE`` option selects native Rust for socket
and sock allocation, file binding, protocol-family creation, reference
transitions, close, delayed write-memory release, and final destruction.

The option depends on ``CONFIG_NET``, ``CONFIG_RUST``,
``CONFIG_RUST_FILE_LIFECYCLE``, and ``CONFIG_RUST_FDTABLE_LIFECYCLE``. The
complete C implementation remains available when this option is disabled or
``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines:

* ``sock_alloc()``

* ``sock_alloc_file()``

* ``sock_release()``

* ``sock_create_lite()``

* ``__sock_create()``

* ``sock_create()``

* ``sock_create_kern()``

* ``__sys_socket_file()``

* ``__sys_socket()``

* ``sk_alloc()``

* ``sk_destruct()``

* ``sk_free()``

* The out-of-line built-in ``sock_put()``

Rust owns:

* Protocol-family and socket-type validation.

* Compatibility conversion of obsolete ``PF_INET`` packet sockets.

* Protocol-family lookup, module loading, module reference acquisition, and
  module reference release.

* Socket inode allocation, file allocation, file binding, descriptor binding,
  and failure rollback.

* LSM create and post-create callback ordering.

* Socket close, operation callback dispatch, inode locking, fasync diagnostic
  ordering, and final socket inode release.

* Sock allocation, security allocation, protocol owner references, network
  namespace accounting, cgroup and memcg accounting, and allocation rollback.

* Final sock reference transition, write-memory delay, reuseport detachment,
  RCU scheduling, diagnostic publication, and the final destructor.

Retained C boundaries
---------------------

``include/linux/socket-lifecycle-rust.h`` defines the narrow helper ABI.
Rust uses the helpers to invoke C layouts, locks, RCU primitives, accounting,
allocation, callback, and diagnostic primitives.

C retains the ``socket``, ``sock``, ``proto``, ``proto_ops``, inode, file,
network namespace, and module layouts. C also retains socketfs inode and file
allocation, protocol-family registration, LSM hooks, network namespace and
socket inuse accounting, memcg and cgroup accounting, socket locks, RCU
primitives, protocol callbacks, diagnostic callbacks, and network namespace
tracker primitives.

The Native C objects retain no duplicate definitions of the selected entries
and no public C tail-call adapters.

Important invariants
--------------------

Family values must be in the range from zero to ``NPROTO``. Socket types must
be in the range from zero to ``SOCK_MAX``. The syscall-level paths accept only
``SOCK_CLOEXEC`` and ``SOCK_NONBLOCK`` outside ``SOCK_TYPE_MASK``.

Rust compile-time assertions preserve the original C requirements:

* ``SOCK_CLOEXEC`` equals ``O_CLOEXEC``.

* ``SOCK_MAX`` and ``SOCK_TYPE_MASK`` combine without extending the mask.

* ``SOCK_CLOEXEC`` does not overlap ``SOCK_TYPE_MASK``.

* ``SOCK_NONBLOCK`` does not overlap ``SOCK_TYPE_MASK``.

Protocol-family module loading occurs only when the family registry entry is
missing. The family owner reference covers the protocol create callback. The
operation-table owner reference then covers socket release.

Every failed family, create, operation-table, module, and LSM transition
releases the socket and its acquired references in reverse acquisition order.

The final sock reference first removes the inuse count. A diagnostic listener
receives the destroy publication. Other socks enter the destructor. A
reuseport socket requires an RCU grace period. The write-memory reference can
delay the final destructor until all transmit references are gone.

The built-in ``sock_put()`` remains an out-of-line Rust entry. Loadable
modules retain the original inline C helper and do not gain a module ABI
dependency on the unexported entry.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* KASAN, lockdep, ``PROVE_LOCKING``, PREEMPT_RT, and DEBUG_VFS.

* Normal socket creation and close, file and descriptor binding, family
  lookup, protocol callbacks, and invalid inputs.

* Allocation failure, module failure, security failure, protocol failure, and
  post-create failure injection.

* Delayed write-memory destruction, reuse of the RCU path, and concurrent
  create and close.

* Symbol placement, export class, external module ABI, and disassembly.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit
``34d66dbdcdd81a44f182a267180e02bba962f511``.

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
Fallback          SMP1                        KUnit 10 of 10
Fallback          SMP4                        KUnit 10 of 10
C-only            SMP1                        KUnit 10 of 10
C-only            SMP4                        KUnit 10 of 10
DEBUG_VFS         SMP1                        KUnit 13 of 13
DEBUG_VFS         SMP4                        KUnit 13 of 13
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, or lockdep complaint.

Native ``rust/kernel.o`` defines every selected entry. Native
``net/socket.o`` and ``net/core/sock.o`` define none. Fallback and C-only
objects retain the complete C implementations.

Native ``Module.symvers`` records ordinary exports for ``sock_alloc_file()``,
``sock_alloc()``, ``sock_release()``, ``sock_create_lite()``,
``__sock_create()``, ``sock_create()``, ``sock_create_kern()``,
``sk_alloc()``, and ``sk_free()``. External GPL and non-GPL modules linked
all nine exports. Modpost rejected both license classes for ``sock_put()``,
``sk_destruct()``, ``__sys_socket()``, and ``__sys_socket_file()``.

Disassembly shows complete Rust implementations of every selected entry.
Native C objects contain no duplicate definition and no public C tail-call
adapter.

Risk
----

A wrong family or type mask can admit an invalid socket type. A wrong module
reference can unload protocol callbacks during creation or release. A wrong
rollback order can leak an inode, file, descriptor, namespace reference,
security state, or operation-table reference.

A wrong write-memory or RCU decision can free a sock during an RCU lookup or
before an in-flight transmit path releases its reference. A wrong diagnostic
ordering can publish a destroyed socket after its final release.

Review conditions
-----------------

Reopen this boundary decision if any of these conditions changes:

* The socket, sock, protocol, inode, file, namespace, or module layout changes.

* Protocol-family registration or module-reference policy changes.

* Socket types, syscall flags, or compatibility conversion rules change.

* LSM, memcg, cgroup, namespace, diagnostic, or network inuse accounting
  changes.

* Socket lock, RCU, write-memory, reuseport, or final-release semantics change.

* Kernel Rust gains safe direct interfaces for a retained primitive.
