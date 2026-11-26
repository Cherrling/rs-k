.. SPDX-License-Identifier: GPL-2.0

Native Rust namespace proxy lifecycle
=====================================

Status and scope
----------------

The ``CONFIG_RUST_NSPROXY_LIFECYCLE`` option selects native Rust for the
namespace proxy creation, copy, publication, switch, exec, unshare, and
release lifecycle.

The option depends on ``CONFIG_RUST``. The complete C implementation remains
available when this option is disabled or ``CONFIG_RUST`` is disabled.

Entry points and ownership
--------------------------

Rust directly defines these production interfaces:

* ``create_new_namespaces()``

* ``nsproxy_free()``

* ``deactivate_nsproxy()``

* ``copy_namespaces()``

* ``unshare_nsproxy_namespaces()``

* ``switch_task_namespaces()``

* ``exit_nsproxy_namespaces()``

* ``switch_cred_namespaces()``

* ``get_cred_namespaces()``

* ``exit_cred_namespaces()``

* ``exec_task_namespaces()``

Rust owns:

* Namespace copy ordering for mount, UTS, IPC, PID, cgroup, network, and
  time namespaces.

* Partial-failure rollback for every allocation and namespace copy stage.

* The clone fast path, capability rejection, and invalid flag combination.

* Task proxy publication, active-reference acquisition, and old-reference
  release.

* Time namespace fork and exec transitions.

* Unshare flag conversion and credential user namespace selection.

* Credential user namespace active-reference transitions.

* Final namespace reference release order and proxy slab release.

None of the eleven entries appears in ``Module.symvers``. Native Rust does not
add a module export.

Retained C boundaries
---------------------

The shared ``struct nsproxy`` and namespace layouts remain in C. Native
``kernel/nsproxy.o`` retains 58 narrow helpers for:

* Proxy slab allocation, reference counting, and object release.

* Namespace copy, get, and put primitives.

* Task, current-task, filesystem, credential, and user namespace access.

* Task locking and proxy pointer exchange.

* Capability checks, sleep checks, and time namespace fork transitions.

* User namespace active-reference accounting.

* Error pointer encoding and classification.

The helpers expose no namespace selection, rollback order, publication order,
or final release policy.

Important invariants
--------------------

``create_new_namespaces()`` copies namespaces in the historical order. Each
copy result must be checked before the next copy starts. A failed copy
releases exactly the references acquired by earlier stages and then frees the
unpublished proxy.

``copy_namespaces()`` shares the inherited proxy when the selected flags
permit sharing and the time namespace for children already matches the
current time namespace. Other successful copies acquire one active reference
before publication to the new task.

``switch_task_namespaces()`` acquires the active reference for a new proxy,
changes the task pointer under the task lock, and releases the old proxy
reference after the lock is dropped.

``exec_task_namespaces()`` is a no-op when the current and child time
namespaces match. Otherwise it creates a fresh proxy, applies the pending time
namespace to the task, and publishes the new proxy through the normal switch
path.

Final release releases mount, UTS, IPC, PID, current time, child time, cgroup,
and network namespace references in that order. It frees the proxy object
last.

Validation gate
---------------

The unit must pass:

* Native Rust, Rust fallback, and C-only builds.

* SMP1 and SMP4 boots.

* Every injected proxy allocation and namespace copy failure.

* Creation and final release, task switching, concurrent creation, real
  kthread fork and exit, unshare, credential namespace references, exec
  no-op, and exec time namespace transition.

* KASAN, lockdep, ``PROVE_LOCKING``, and PREEMPT_RT.

* Symbol placement, export class, external module ABI, and disassembly
  checks.

* Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
  ``git diff --check``.

Validation results
------------------

Validation uses base commit ``cb77d5d8036a``.

================  ==========================  =========================
Build             Runtime                     Result
================  ==========================  =========================
Native Rust       SMP1                        KUnit 10 of 10
Native Rust       SMP4                        KUnit 10 of 10
KASAN             SMP1                        KUnit 10 of 10
KASAN             SMP4                        KUnit 10 of 10
lockdep           SMP1                        KUnit 10 of 10
lockdep           SMP4                        KUnit 10 of 10
PREEMPT_RT        SMP1                        KUnit 10 of 10
PREEMPT_RT        SMP4                        KUnit 10 of 10
Fallback          SMP1                        KUnit 9 of 9
Fallback          SMP4                        KUnit 9 of 9
C-only            SMP1                        KUnit 9 of 9
C-only            SMP4                        KUnit 9 of 9
================  ==========================  =========================

Every boot completed without a failed KUnit case, expectation failure, BUG,
oops, KASAN report, lockdep complaint, or kernel halt.

The Native suite covers every allocation and namespace copy failure, creation
and final release, task switching, concurrent creation, real kthread fork and
exit, unshare, credential namespace references, the exec no-op path, and a
real pending time namespace transition during exec.

Native ``rust/kernel.o`` defined all eleven entries. Native
``kernel/nsproxy.o`` defined none of them and retained 58 narrow helpers.
Native ``Module.symvers`` contained none of the eleven symbols. The linked
image had no unresolved target symbol. Disassembly showed complete Rust
implementations of ``create_new_namespaces()`` and
``exec_task_namespaces()``.

The fallback and C-only ``kernel/nsproxy.o`` objects defined the ten public
entries and the internal static ``create_new_namespaces()`` implementation.
They retained no Native helper definitions. Their ``rust/kernel.o`` objects
did not define any target entry.

External GPL and non-GPL modules referenced all eleven entries. Modpost
rejected both modules with all eleven symbols unresolved, which preserves the
unexported ABI class.

Rustfmt, Clippy, Rustdoc, focused Sphinx, strict checkpatch, and
``git diff --check`` passed. Focused Sphinx retained the known unrelated
``kernel-docs`` reference warning.

Review conditions
-----------------

Reopen the retained C boundary decision if any of these conditions changes:

* The ``struct nsproxy`` layout, namespace ordering, or reference rules
  change.

* Task namespace publication or locking changes.

* Time namespace fork or exec semantics change.

* Namespace capability or user namespace active-reference accounting changes.

* Kernel Rust gains safe direct interfaces for a retained primitive.

* Measurements show that the helper calls cause an unacceptable lifecycle
  regression.
