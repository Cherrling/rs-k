// SPDX-License-Identifier: GPL-2.0-only

//! Native `irq_work` state machine.
//!
//! Rust owns claim and reference-count style state transitions, duplicate
//! suppression, local and remote queue selection, LAZY and PREEMPT_RT policy,
//! callback dispatch, list draining, and synchronization decisions.
//!
//! C helpers retain per-CPU storage, preemption, IPI delivery, hotplug and RT
//! thread plumbing, KASAN, lockdep, architecture IRQ entry points, and the
//! low-level `rcuwait` and RCU primitives.

use crate::bindings;
use crate::sync::atomic::{Atomic, Full, Relaxed};
use core::ffi::{c_int, c_void};
use macros::export;

type Work = bindings::irq_work;
type List = bindings::llist_head;
type Node = bindings::llist_node;
type Callback = Option<unsafe extern "C" fn(*mut Work)>;

const PENDING: i32 = bindings::IRQ_WORK_PENDING as i32;
const BUSY: i32 = bindings::IRQ_WORK_BUSY as i32;
const CLAIMED: i32 = bindings::IRQ_WORK_CLAIMED as i32;
const LAZY: i32 = bindings::IRQ_WORK_LAZY as i32;
const HARD_IRQ: i32 = bindings::IRQ_WORK_HARD_IRQ as i32;
const CSD_TYPE_IRQ_WORK: i32 = bindings::CSD_TYPE_IRQ_WORK as i32;

/// Returns the atomic flags for an IRQ work item.
///
/// # Safety
///
/// `work` must point to valid IRQ work storage.
#[inline]
unsafe fn flags_ptr(work: *mut Work) -> *mut i32 {
    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { bindings::rust_irq_work_flags(work) }.cast::<i32>()
}

/// Returns the list node embedded in an IRQ work item.
///
/// # Safety
///
/// `work` must point to valid IRQ work storage.
#[inline]
unsafe fn work_node(work: *mut Work) -> *mut Node {
    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { bindings::rust_irq_work_node(work) }
}

/// Returns the IRQ work item containing a list node.
///
/// # Safety
///
/// `node` must be embedded in a live IRQ work item.
#[inline]
unsafe fn work_from_node(node: *mut Node) -> *mut Work {
    // SAFETY: The caller guarantees that `node` is embedded in an IRQ work.
    unsafe { bindings::rust_irq_work_from_node(node) }
}

/// Returns the next node in a detached lockless-list chain.
///
/// # Safety
///
/// `node` must point to valid list-node storage.
#[inline]
unsafe fn next_node(node: *mut Node) -> *mut Node {
    // SAFETY: The caller guarantees that `node` is valid.
    unsafe { bindings::rust_irq_work_next_node(node) }
}

/// Returns the current CPU's raised or lazy list.
///
/// # Safety
///
/// The caller must not migrate before the returned per-CPU pointer is used.
#[inline]
unsafe fn current_list(lazy: bool) -> *mut List {
    // SAFETY: The caller prevents migration.
    unsafe { bindings::rust_irq_work_list_current(lazy) }
}

/// Returns a target CPU's raised or lazy list.
///
/// # Safety
///
/// `cpu` must identify a valid per-CPU area.
#[inline]
unsafe fn cpu_list(cpu: c_int, lazy: bool) -> *mut List {
    // SAFETY: The caller guarantees that `cpu` is valid.
    unsafe { bindings::rust_irq_work_list_cpu(cpu, lazy) }
}

/// Adds a node to a lockless list and returns true if it was empty.
///
/// # Safety
///
/// `node` and `list` must point to valid, NMI-safe storage.
#[inline]
unsafe fn list_add(node: *mut Node, list: *mut List) -> bool {
    // SAFETY: The caller guarantees that both pointers are valid.
    unsafe { bindings::rust_irq_work_llist_add(node, list) }
}

/// Tests whether a lockless list appears empty.
///
/// # Safety
///
/// `list` must point to valid list-head storage.
#[inline]
unsafe fn list_empty(list: *mut List) -> bool {
    // SAFETY: The caller guarantees that `list` is valid.
    unsafe { bindings::rust_irq_work_llist_empty(list) }
}

/// Detaches all nodes from a lockless list.
///
/// # Safety
///
/// `list` must point to valid list-head storage and the caller must be the
/// list consumer.
#[inline]
unsafe fn list_del_all(list: *mut List) -> *mut Node {
    // SAFETY: The caller guarantees that `list` is valid.
    unsafe { bindings::rust_irq_work_llist_del_all(list) }
}

/// Claims an IRQ work item.
///
/// Returns true only for the caller that installed the first PENDING bit. The
/// CSD type is installed on every claim to match the call-single queue ABI.
///
/// # Safety
///
/// `work` must point to valid IRQ work storage.
unsafe fn claim(work: *mut Work) -> bool {
    // SAFETY: The caller guarantees that `work` is valid.
    let flags = unsafe { Atomic::from_ptr(flags_ptr(work)) };
    let mut old = flags.load(Relaxed);
    loop {
        let new = old | CLAIMED | CSD_TYPE_IRQ_WORK;
        match flags.cmpxchg(old, new, Full) {
            Ok(_) => return old & PENDING == 0,
            Err(observed) => old = observed,
        }
    }
}

/// Loads the current IRQ work state flags.
///
/// # Safety
///
/// `work` must point to valid IRQ work storage.
#[inline]
unsafe fn load_flags(work: *mut Work) -> i32 {
    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { Atomic::from_ptr(flags_ptr(work)) }.load(Relaxed)
}

/// Raises the architecture IRQ work interrupt.
///
/// # Safety
///
/// `work` must point to valid IRQ work storage.
#[inline]
unsafe fn raise(work: *mut Work) {
    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { bindings::rust_irq_work_raise(work) };
}

/// Enqueues claimed work on the current CPU.
///
/// The caller must hold a preemption-disabled section.
///
/// # Safety
///
/// `work` must be claimed and point to valid storage.
unsafe fn queue_local(work: *mut Work) {
    // SAFETY: The caller guarantees that `work` is valid.
    let flags = unsafe { load_flags(work) };
    let explicit_lazy = flags & LAZY != 0;
    let rt_lazy = cfg!(CONFIG_PREEMPT_RT) && flags & HARD_IRQ == 0;

    // SAFETY: Preemption is disabled by the caller.
    let list = unsafe { current_list(explicit_lazy || rt_lazy) };
    // SAFETY: `work` is claimed and `list` is the current CPU's queue.
    if !unsafe { list_add(work_node(work), list) } {
        return;
    }

    // Explicit lazy work waits for the next tick unless the tick is stopped.
    // PREEMPT_RT lazy work still raises the hard wake work immediately.
    // SAFETY: The caller guarantees that `work` is valid.
    if !explicit_lazy || unsafe { bindings::rust_irq_work_tick_stopped() } {
        // SAFETY: The caller guarantees that `work` is valid.
        unsafe { raise(work) };
    }
}

/// Enqueues an IRQ work item on the current CPU.
///
/// # Safety
///
/// `work` must point to valid storage that remains allocated until the
/// callback finishes.
#[export]
unsafe extern "C" fn irq_work_queue(work: *mut Work) -> bool {
    // SAFETY: The caller guarantees that `work` is valid.
    if !unsafe { claim(work) } {
        return false;
    }

    // SAFETY: This helper starts a preemption-disabled section.
    unsafe { bindings::rust_irq_work_preempt_disable() };
    // SAFETY: The work is claimed and this CPU cannot migrate.
    unsafe { queue_local(work) };
    // SAFETY: This helper closes the preemption-disabled section.
    unsafe { bindings::rust_irq_work_preempt_enable() };
    true
}

/// Enqueues an IRQ work item on a selected CPU.
///
/// # Safety
///
/// `work` must point to valid storage that remains allocated until the
/// callback finishes.
#[cfg(CONFIG_SMP)]
unsafe fn queue_on_smp(work: *mut Work, cpu: c_int) -> bool {
    // SAFETY: This helper only emits a diagnostic.
    unsafe { bindings::rust_irq_work_warn_offline(cpu) };

    // SAFETY: The caller guarantees that `work` is valid.
    if !unsafe { claim(work) } {
        return false;
    }

    // SAFETY: The caller guarantees that `work` is valid.
    unsafe { bindings::rust_irq_work_record_aux_stack(work) };
    // SAFETY: This helper starts a preemption-disabled section.
    unsafe { bindings::rust_irq_work_preempt_disable() };

    // SAFETY: This helper returns the current CPU with preemption disabled.
    let current = unsafe { bindings::rust_irq_work_current_cpu() };
    if cpu == current {
        // SAFETY: The work is claimed and preemption is disabled.
        unsafe { queue_local(work) };
    } else {
        // Remote IPI delivery is not NMI-safe.
        // SAFETY: This helper only emits a diagnostic.
        unsafe { bindings::rust_irq_work_warn_in_nmi() };

        let mut queued = work;
        // SAFETY: The claimed work remains valid while preemption is disabled.
        let remote_lazy = cfg!(CONFIG_PREEMPT_RT) && unsafe { load_flags(work) } & HARD_IRQ == 0;
        if remote_lazy {
            // SAFETY: `cpu` is valid and preemption is disabled.
            let list = unsafe { cpu_list(cpu, true) };
            // SAFETY: The list and claimed work are valid.
            if !unsafe { list_add(work_node(work), list) } {
                // SAFETY: This helper closes the preemption-disabled section.
                unsafe { bindings::rust_irq_work_preempt_enable() };
                return true;
            }

            // SAFETY: The caller supplied a valid target CPU.
            queued = unsafe { bindings::rust_irq_work_remote_wakeup(cpu) };
            // SAFETY: The per-CPU wake work is valid.
            if !unsafe { claim(queued) } {
                // SAFETY: This helper closes the preemption-disabled section.
                unsafe { bindings::rust_irq_work_preempt_enable() };
                return true;
            }
        }

        // The call-single queue owns list publication and IPI delivery.
        // SAFETY: `cpu` is valid, preemption is disabled, and the work is claimed.
        unsafe { bindings::rust_irq_work_remote_queue(cpu, queued) };
    }

    // SAFETY: This helper closes the preemption-disabled section.
    unsafe { bindings::rust_irq_work_preempt_enable() };
    true
}

/// Enqueues an IRQ work item on a selected CPU.
///
/// # Safety
///
/// `work` must point to valid storage that remains allocated until the
/// callback finishes.
#[export]
unsafe extern "C" fn irq_work_queue_on(work: *mut Work, cpu: c_int) -> bool {
    #[cfg(not(CONFIG_SMP))]
    {
        let _ = cpu;
        // SAFETY: The caller provides the same contract for the local queue.
        unsafe { irq_work_queue(work) }
    }

    #[cfg(CONFIG_SMP)]
    {
        // SAFETY: The caller provides the same contract for the remote queue.
        unsafe { queue_on_smp(work, cpu) }
    }
}

/// Returns whether the current CPU must process IRQ work.
///
/// # Safety
///
/// The caller must not migrate during the per-CPU list checks. The caller must
/// also provide the context required by the IRQ work entry point.
#[export]
unsafe extern "C" fn irq_work_needs_cpu() -> bool {
    // SAFETY: The caller must not migrate during these per-CPU accesses.
    let raised = unsafe { current_list(false) };
    // SAFETY: The caller must not migrate during these per-CPU accesses.
    let lazy = unsafe { current_list(true) };

    // SAFETY: Both pointers identify current per-CPU lists.
    if unsafe { list_empty(raised) } || unsafe { bindings::rust_irq_work_has_interrupt() } {
        // SAFETY: `lazy` identifies the current per-CPU list.
        if unsafe { list_empty(lazy) } {
            return false;
        }
    }

    // SAFETY: This helper only emits a diagnostic.
    unsafe { bindings::rust_irq_work_warn_current_offline() };
    true
}

/// Runs one detached IRQ work item.
///
/// # Safety
///
/// `work` must be claimed, detached from every queue, and point to valid
/// storage for the duration of the callback.
unsafe fn run_one(work: *mut Work) {
    // SAFETY: The caller guarantees that `work` is valid.
    let flags = unsafe { Atomic::from_ptr(flags_ptr(work)) };
    let callback_flags = flags.load(Relaxed) & !PENDING;

    // PENDING acts as the enqueue lock. The callback consumer owns it and can
    // clear it without a compare-exchange.
    flags.store(callback_flags, Relaxed);
    // SAFETY: This helper provides the barrier paired with claim().
    unsafe { bindings::rust_irq_work_smp_mb() };

    // SAFETY: The callback flags came from the valid work item.
    unsafe { bindings::rust_irq_work_lockdep_enter(callback_flags) };
    // SAFETY: The queue contract installs a valid callback.
    let callback: Callback = unsafe { (*work).func };
    // SAFETY: The queue contract guarantees that the callback is non-null.
    let callback = unsafe { callback.unwrap_unchecked() };
    // SAFETY: The callback contract permits execution from IRQ-work context.
    unsafe { callback(work) };
    // SAFETY: The callback flags came from the valid work item.
    unsafe { bindings::rust_irq_work_lockdep_exit(callback_flags) };

    // A reentrant claim leaves PENDING set, so this compare-exchange preserves
    // the new pending state and defers clearing BUSY until the next callback.
    let _ = flags.cmpxchg(callback_flags, callback_flags & !BUSY, Full);

    // SAFETY: This helper reports a static architecture property.
    let has_interrupt = unsafe { bindings::rust_irq_work_has_interrupt() };
    if (cfg!(CONFIG_PREEMPT_RT) && callback_flags & HARD_IRQ == 0) || !has_interrupt {
        // SAFETY: The work remains valid until BUSY is cleared.
        unsafe { bindings::rust_irq_work_rcuwait_wake(work) };
    }
}

/// Runs one IRQ work item supplied by the call-single queue ABI.
///
/// # Safety
///
/// `arg` must point to a claimed, detached, valid IRQ work item.
#[export]
unsafe extern "C" fn irq_work_single(arg: *mut c_void) {
    // SAFETY: The call-single queue supplies a valid IRQ work pointer.
    unsafe { run_one(arg.cast::<Work>()) };
}

/// Drains a detached lockless-list chain.
///
/// # Safety
///
/// `node` must be the head of a detached chain of valid IRQ work items.
unsafe fn run_chain(mut node: *mut Node) {
    while !node.is_null() {
        // Read the next pointer before the callback can reuse the current node.
        // SAFETY: The detached chain owns `node`.
        let next = unsafe { next_node(node) };
        // SAFETY: `node` is embedded in a valid IRQ work item.
        let work = unsafe { work_from_node(node) };
        // SAFETY: The detached chain owns `work` until its callback returns.
        unsafe { run_one(work) };
        node = next;
    }
}

/// Drains one per-CPU IRQ work list.
///
/// # Safety
///
/// `list` must be the current CPU's list and the caller must satisfy the
/// context rules for IRQ work callbacks.
unsafe fn run_list(list: *mut List) {
    // SAFETY: The caller supplies the production IRQ work context.
    unsafe { bindings::rust_irq_work_assert_run_context() };

    // SAFETY: The caller guarantees that `list` is the current CPU's list.
    if unsafe { list_empty(list) } {
        return;
    }

    // SAFETY: The caller guarantees that `list` is the current CPU's list.
    let node = unsafe { list_del_all(list) };
    // SAFETY: `node` is a detached chain.
    unsafe { run_chain(node) };
}

/// Wakes the current CPU's RT IRQ work thread when lazy work is pending.
///
/// # Safety
///
/// The current CPU's per-CPU state must be accessible.
#[inline]
unsafe fn wake_thread() {
    // SAFETY: This helper checks both the lazy list and worker pointer.
    unsafe { bindings::rust_irq_work_wake_thread() };
}

/// Drains raised work and dispatches lazy work for the current CPU.
///
/// # Safety
///
/// The caller must run on the current CPU with migration disabled. The caller
/// must provide the IRQ work interrupt or hotplug flush context.
#[export]
unsafe extern "C" fn irq_work_run() {
    // SAFETY: The caller is the IRQ work interrupt or hotplug flush path.
    let raised = unsafe { current_list(false) };
    // SAFETY: `raised` is the current CPU's list.
    unsafe { run_list(raised) };

    if cfg!(CONFIG_PREEMPT_RT) {
        // SAFETY: PREEMPT_RT runs non-hard work in the per-CPU thread.
        unsafe { wake_thread() };
    } else {
        // SAFETY: The caller supplies the non-RT hardirq context.
        let lazy = unsafe { current_list(true) };
        // SAFETY: `lazy` is the current CPU's list.
        unsafe { run_list(lazy) };
    }
}

/// Processes timer-tick IRQ work for the current CPU.
///
/// # Safety
///
/// The caller must run in timer-tick context on the current CPU with migration
/// disabled.
#[export]
unsafe extern "C" fn irq_work_tick() {
    // SAFETY: The timer tick runs without CPU migration.
    let raised = unsafe { current_list(false) };
    // SAFETY: `raised` is the current CPU's list.
    let raised_empty = unsafe { list_empty(raised) };
    if !raised_empty {
        // SAFETY: This helper reports a static architecture property.
        let has_interrupt = unsafe { bindings::rust_irq_work_has_interrupt() };
        if !has_interrupt {
            // SAFETY: `raised` is the current CPU's list.
            unsafe { run_list(raised) };
        }
    }

    if cfg!(CONFIG_PREEMPT_RT) {
        // SAFETY: PREEMPT_RT runs non-hard work in the per-CPU thread.
        unsafe { wake_thread() };
    } else {
        // SAFETY: The timer tick supplies the required context.
        let lazy = unsafe { current_list(true) };
        // SAFETY: `lazy` is the current CPU's list.
        unsafe { run_list(lazy) };
    }
}

/// Runs the current CPU's lazy list from the RT worker thread.
///
/// # Safety
///
/// The caller must be the current CPU's RT IRQ work worker.
#[export]
unsafe extern "C" fn rust_irq_work_run_lazy() {
    // SAFETY: The RT worker owns lazy-list consumption on this CPU.
    let lazy = unsafe { current_list(true) };
    // SAFETY: `lazy` is the current CPU's list.
    unsafe { run_list(lazy) };
}

/// Waits until an IRQ work callback is no longer active.
///
/// # Safety
///
/// `work` must point to valid storage whose final release follows this call.
#[export]
unsafe extern "C" fn irq_work_sync(work: *mut Work) {
    // SAFETY: This helper only runs the required context assertions.
    unsafe { bindings::rust_irq_work_sync_checks() };

    // SAFETY: The caller guarantees that `work` is valid.
    let remote_lazy = cfg!(CONFIG_PREEMPT_RT) && unsafe { load_flags(work) } & HARD_IRQ == 0;
    // SAFETY: This helper reports a static architecture property.
    let no_interrupt = unsafe { bindings::rust_irq_work_has_interrupt() };
    let slow_path = remote_lazy || !no_interrupt;

    if slow_path {
        // BUSY is checked by the rcuwait condition helper. Rust selects this
        // slow path before entering the scheduler primitive.
        // SAFETY: The caller guarantees that `work` is valid and may sleep.
        unsafe { bindings::rust_irq_work_rcuwait_wait(work) };
        // SAFETY: The RT worker and asynchronous IRQ paths access work in an
        // RCU read-side section.
        unsafe { bindings::rust_irq_work_sync_rcu() };
        return;
    }

    // SAFETY: The caller guarantees that `work` remains valid.
    while unsafe { load_flags(work) } & BUSY != 0 {
        // SAFETY: This helper only supplies an architecture pause.
        unsafe { bindings::rust_irq_work_cpu_relax() };
    }
}
