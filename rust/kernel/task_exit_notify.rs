// SPDX-License-Identifier: GPL-2.0

//! Native task-exit parent notification.
//!
//! Rust owns child-reaper selection, child reparenting, zombie notification,
//! self-reaping decisions, and dead-task release ordering. C helpers perform
//! one retained lock, list, signal, ptrace, or PID-namespace primitive each.

use crate::bindings;
use core::mem::{offset_of, MaybeUninit};
use macros::export;

#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type ListHead = bindings::list_head;
type Task = bindings::task_struct;

const EXIT_ZOMBIE: i32 = bindings::EXIT_ZOMBIE as i32;
const EXIT_DEAD: i32 = bindings::EXIT_DEAD as i32;
const PF_EXITING: u32 = bindings::PF_EXITING;
const SIGCHLD: i32 = bindings::SIGCHLD as i32;

const AUTOREAP_PTRACE: u32 = bindings::RUST_TASK_EXIT_NOTIFY_PTRACE;
const AUTOREAP_LEADER: u32 = bindings::RUST_TASK_EXIT_NOTIFY_LEADER;
const AUTOREAP_THREAD: u32 = bindings::RUST_TASK_EXIT_NOTIFY_THREAD;

const TASK_SIBLING_OFFSET: usize = offset_of!(Task, sibling);
const TASK_PTRACE_ENTRY_OFFSET: usize = offset_of!(Task, ptrace_entry);
const TASK_THREAD_NODE_OFFSET: usize = offset_of!(Task, thread_node);

#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
static EXIT_NOTIFY_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
static REPARENT_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
static CHILD_REAPER_COUNT: AtomicU32 = AtomicU32::new(0);
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
static PID_NS_ZAP_COUNT: AtomicU32 = AtomicU32::new(0);

fn autoreap_mode(ptraced: bool, leader: bool) -> u32 {
    if ptraced {
        AUTOREAP_PTRACE
    } else if leader {
        AUTOREAP_LEADER
    } else {
        AUTOREAP_THREAD
    }
}

fn ptrace_signal(group_empty: bool, reparented: bool, exit_signal: i32) -> i32 {
    if group_empty && !reparented {
        exit_signal
    } else {
        SIGCHLD
    }
}

/// Tests the production notification branch selection.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_autoreap_mode(ptraced: bool, leader: bool) -> u32 {
    autoreap_mode(ptraced, leader)
}

/// Tests the ptrace death-signal selection.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_ptrace_signal(
    group_empty: bool,
    reparented: bool,
    exit_signal: i32,
) -> i32 {
    ptrace_signal(group_empty, reparented, exit_signal)
}

/// Returns the number of production exit-notify entries observed by the test.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_count() -> u32 {
    EXIT_NOTIFY_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of production child-reparent operations observed.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_reparent_count() -> u32 {
    REPARENT_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of production child-reaper transfers observed.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_child_reaper_count() -> u32 {
    CHILD_REAPER_COUNT.load(Ordering::Relaxed)
}

/// Returns the number of production PID namespace teardowns observed.
#[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_exit_notify_test_pid_ns_zap_count() -> u32 {
    PID_NS_ZAP_COUNT.load(Ordering::Relaxed)
}

/// Converts one intrusive list node to its containing task.
///
/// # Safety
///
/// `node` must be the named link of a live task in the expected list.
unsafe fn task_from_node(node: *mut ListHead, offset: usize) -> *mut Task {
    // SAFETY: The caller guarantees that `node` is embedded in a task at the
    // supplied offset.
    unsafe { node.cast::<u8>().sub(offset).cast::<Task>() }
}

/// Finds a live thread in a task's thread group.
///
/// # Safety
///
/// `task` must be live and `tasklist_lock` must be held.
unsafe fn find_alive_thread(task: *mut Task) -> *mut Task {
    // SAFETY: The caller guarantees a live task.
    let head = unsafe { bindings::rust_helper_task_exit_notify_thread_head(task) };
    // SAFETY: The task's thread group list is protected by tasklist_lock.
    let mut node = unsafe { bindings::rust_helper_task_exit_notify_list_first(head) };

    while !node.is_null() {
        // SAFETY: The node is linked through `thread_node`.
        let thread = unsafe { task_from_node(node, TASK_THREAD_NODE_OFFSET) };
        // SAFETY: The thread remains live under tasklist_lock.
        if unsafe { (*thread).flags & PF_EXITING == 0 } {
            return thread;
        }
        // SAFETY: The list remains stable while tasklist_lock is held.
        node = unsafe { bindings::rust_helper_task_exit_notify_list_next(head, node) };
    }

    core::ptr::null_mut()
}

/// Releases tasks accumulated on the local dead list.
///
/// # Safety
///
/// `dead` must contain live dead-task list entries, and the caller must not
/// hold tasklist_lock.
unsafe fn release_dead(dead: *mut ListHead) {
    // SAFETY: The caller supplies the initialized dead list.
    let mut node = unsafe { bindings::rust_helper_task_exit_notify_list_first(dead) };

    while !node.is_null() {
        // SAFETY: `node` remains live until it is removed below.
        let next = unsafe { bindings::rust_helper_task_exit_notify_list_next(dead, node) };
        // SAFETY: The node is linked through `ptrace_entry`.
        let task = unsafe { task_from_node(node, TASK_PTRACE_ENTRY_OFFSET) };
        // SAFETY: The dead task is removed before its release.
        unsafe {
            bindings::rust_helper_task_exit_notify_list_del_init(node);
            bindings::rust_task_release(task);
        }
        node = next;
    }
}

/// Selects the reaper for a dying namespace child reaper.
///
/// # Safety
///
/// `father` must be live and `tasklist_lock` must be write-held. `dead` must
/// be an initialized local list.
unsafe fn find_child_reaper(father: *mut Task, dead: *mut ListHead) -> *mut Task {
    // SAFETY: The caller holds tasklist_lock and father remains live.
    let pid_ns = unsafe { bindings::rust_helper_task_exit_notify_active_pid_ns(father) };
    // SAFETY: The PID namespace remains live while its reaper exits.
    let reaper = unsafe { bindings::rust_helper_task_exit_notify_child_reaper(pid_ns) };

    if reaper != father {
        return reaper;
    }

    // SAFETY: The caller holds tasklist_lock.
    let thread = unsafe { find_alive_thread(father) };
    if !thread.is_null() {
        // SAFETY: A live thread becomes the namespace child reaper.
        unsafe { bindings::rust_helper_task_exit_notify_set_child_reaper(pid_ns, thread) };
        #[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
        CHILD_REAPER_COUNT.fetch_add(1, Ordering::Relaxed);
        return thread;
    }

    // SAFETY: The original sequence drops tasklist_lock for namespace teardown.
    unsafe {
        bindings::rust_helper_task_release_unlock_tasklist();
        release_dead(dead);
        bindings::rust_helper_task_exit_notify_zap_pid_ns(pid_ns);
        bindings::rust_helper_task_release_lock_tasklist();
    }
    #[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
    PID_NS_ZAP_COUNT.fetch_add(1, Ordering::Relaxed);

    father
}

/// Selects the task that receives a dying task's children.
///
/// # Safety
///
/// `father` must be live and `tasklist_lock` must be write-held.
unsafe fn find_new_reaper(father: *mut Task, child_reaper: *mut Task) -> *mut Task {
    // SAFETY: The caller holds tasklist_lock.
    let thread = unsafe { find_alive_thread(father) };
    if !thread.is_null() {
        return thread;
    }

    // SAFETY: The task remains live under tasklist_lock.
    let signal = unsafe { (*father).signal };
    // SAFETY: The signal structure remains live while the task exits.
    if unsafe { !(*signal).has_child_subreaper } {
        return child_reaper;
    }

    // SAFETY: A live task owns a live thread PID.
    let pid = unsafe { (*father).thread_pid };
    // SAFETY: A live task owns a live thread PID.
    let level = unsafe { (*pid).level };
    // SAFETY: The real parent remains live under tasklist_lock.
    let mut reaper = unsafe { (*father).real_parent };
    // SAFETY: The init task address is stable for the kernel lifetime.
    let init_task = unsafe { bindings::rust_helper_task_exit_notify_init_task() };

    while !reaper.is_null() {
        // SAFETY: Ancestors remain live under tasklist_lock.
        let pid = unsafe { (*reaper).thread_pid };
        // PID namespace ancestry ends at a task without a thread PID.
        if pid.is_null() {
            break;
        }
        // SAFETY: The task PID remains live under tasklist_lock.
        if unsafe { (*pid).level != level } {
            break;
        }
        if reaper == init_task {
            break;
        }

        // SAFETY: The ancestor remains live under tasklist_lock.
        let signal = unsafe { (*reaper).signal };
        // SAFETY: The signal structure remains live with its task.
        if unsafe { (*signal).is_child_subreaper } {
            // SAFETY: The ancestor is live and tasklist_lock is held.
            let thread = unsafe { find_alive_thread(reaper) };
            if !thread.is_null() {
                return thread;
            }
        }

        // SAFETY: The next ancestor remains reachable under tasklist_lock.
        reaper = unsafe { (*reaper).real_parent };
    }

    child_reaper
}

/// Reparents and notifies one zombie child leader.
///
/// # Safety
///
/// `father` and `child` must be live, `tasklist_lock` must be write-held, and
/// `dead` must be an initialized local list.
unsafe fn reparent_leader(father: *mut Task, child: *mut Task, dead: *mut ListHead) {
    // SAFETY: The child remains live under tasklist_lock.
    if unsafe { (*child).exit_state == EXIT_DEAD } {
        return;
    }

    // SAFETY: The child remains live under tasklist_lock.
    unsafe { (*child).exit_signal = SIGCHLD };

    // SAFETY: The child remains live under tasklist_lock.
    let untraced = unsafe { (*child).ptrace == 0 };
    // SAFETY: The child remains live under tasklist_lock.
    let zombie = unsafe { (*child).exit_state == EXIT_ZOMBIE };
    // SAFETY: The child remains live under tasklist_lock.
    let group_empty = unsafe { bindings::rust_helper_task_exit_notify_thread_group_empty(child) };

    if untraced && zombie && group_empty {
        // SAFETY: The child remains live under tasklist_lock.
        let signal = unsafe { (*child).exit_signal };
        // SAFETY: The child remains live under tasklist_lock.
        let autoreap =
            unsafe { bindings::rust_helper_task_exit_notify_notify_parent(child, signal) };
        if autoreap {
            // SAFETY: The child is now eligible for final release.
            unsafe {
                (*child).exit_state = EXIT_DEAD;
                bindings::rust_helper_task_exit_notify_list_add(&mut (*child).ptrace_entry, dead);
            }
        }
    }

    // SAFETY: The child remains live under tasklist_lock.
    unsafe { bindings::rust_helper_task_exit_notify_kill_orphaned_pgrp(child, father) };
}

/// Reparents every child of an exiting task.
///
/// # Safety
///
/// `father` must be live and `tasklist_lock` must be write-held. `dead` must
/// be an initialized local list.
unsafe fn forget_original_parent(father: *mut Task, dead: *mut ListHead) {
    // SAFETY: The exiting task's ptrace list is protected by tasklist_lock.
    let ptraced = unsafe { bindings::rust_helper_task_exit_notify_ptraced(father) };
    // SAFETY: The caller guarantees a live list.
    if unsafe { !bindings::rust_helper_task_exit_notify_list_empty(ptraced) } {
        // SAFETY: This transfers traced children while tasklist_lock is held.
        unsafe { bindings::exit_ptrace(father, dead) };
    }

    // SAFETY: The caller holds tasklist_lock.
    let child_reaper = unsafe { find_child_reaper(father, dead) };
    // SAFETY: The exiting task remains live.
    let children = unsafe { bindings::rust_helper_task_exit_notify_children(father) };
    // SAFETY: The caller supplies a live list.
    if unsafe { bindings::rust_helper_task_exit_notify_list_empty(children) } {
        return;
    }

    // SAFETY: The caller holds tasklist_lock.
    let reaper = unsafe { find_new_reaper(father, child_reaper) };
    // SAFETY: The children list remains stable under tasklist_lock.
    let mut node = unsafe { bindings::rust_helper_task_exit_notify_list_first(children) };

    while !node.is_null() {
        // SAFETY: Reparenting does not remove the child sibling link.
        let next = unsafe { bindings::rust_helper_task_exit_notify_list_next(children, node) };
        // SAFETY: The node is linked through `sibling`.
        let child = unsafe { task_from_node(node, TASK_SIBLING_OFFSET) };
        // SAFETY: The child remains live under tasklist_lock.
        let thread_head = unsafe { bindings::rust_helper_task_exit_notify_thread_head(child) };
        // SAFETY: Thread membership remains stable under tasklist_lock.
        let mut thread_node =
            unsafe { bindings::rust_helper_task_exit_notify_list_first(thread_head) };

        while !thread_node.is_null() {
            // SAFETY: Parent updates do not remove the thread link.
            let next_thread = unsafe {
                bindings::rust_helper_task_exit_notify_list_next(thread_head, thread_node)
            };
            // SAFETY: The node is linked through `thread_node`.
            let thread = unsafe { task_from_node(thread_node, TASK_THREAD_NODE_OFFSET) };

            // SAFETY: The thread remains live under tasklist_lock.
            unsafe { bindings::rust_helper_task_exit_notify_set_real_parent(thread, reaper) };
            // SAFETY: The original invariant checks the parent publication.
            if unsafe { bindings::rust_helper_task_exit_notify_parent_inconsistent(thread, father) }
            {
                // SAFETY: This preserves the original BUG_ON condition.
                unsafe { bindings::rust_helper_task_exit_notify_bug() };
            }
            // SAFETY: The thread remains live under tasklist_lock.
            if unsafe { (*thread).ptrace == 0 } {
                // SAFETY: An untraced thread follows its real parent.
                unsafe { bindings::rust_helper_task_exit_notify_set_parent(thread) };
            }
            // SAFETY: The thread remains live under tasklist_lock.
            let pdeath_signal = unsafe { (*thread).pdeath_signal };
            if pdeath_signal != 0 {
                // SAFETY: This preserves the original parent-death signal.
                unsafe {
                    bindings::rust_helper_task_exit_notify_send_pdeath(thread, pdeath_signal);
                }
            }

            thread_node = next_thread;
        }

        // SAFETY: Both tasks remain live under tasklist_lock.
        if unsafe { (*reaper).signal != (*father).signal } {
            // SAFETY: The caller holds tasklist_lock and the child is live.
            unsafe { reparent_leader(father, child, dead) };
        }

        node = next;
    }

    // SAFETY: Both children lists are live under tasklist_lock.
    let reaper_children = unsafe { bindings::rust_helper_task_exit_notify_children(reaper) };
    // SAFETY: The caller holds tasklist_lock.
    unsafe { bindings::rust_helper_task_exit_notify_list_splice_tail(children, reaper_children) };

    #[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
    REPARENT_COUNT.fetch_add(1, Ordering::Relaxed);
}

/// Notifies the parent and reparents children of an exiting task.
///
/// # Safety
///
/// `task` must satisfy the production requirements of the original
/// `exit_notify()` function.
unsafe fn exit_notify(task: *mut Task, group_dead: bool) {
    let mut dead = MaybeUninit::<ListHead>::uninit();
    // SAFETY: `dead` provides writable storage for a list sentinel.
    let dead = unsafe {
        bindings::rust_helper_task_exit_notify_list_init(dead.as_mut_ptr());
        dead.assume_init_mut()
    };

    // SAFETY: The caller holds no conflicting tasklist lock.
    unsafe { bindings::rust_helper_task_release_lock_tasklist() };
    // SAFETY: The task and its lists are protected by tasklist_lock.
    unsafe { forget_original_parent(task, dead) };

    if group_dead {
        // SAFETY: The group leader remains live under tasklist_lock.
        let leader = unsafe { (*task).group_leader };
        // SAFETY: This preserves process-group cleanup ordering.
        unsafe {
            bindings::rust_helper_task_exit_notify_kill_orphaned_pgrp(leader, core::ptr::null_mut())
        };
    }

    // SAFETY: The exiting task remains live under tasklist_lock.
    unsafe { (*task).exit_state = EXIT_ZOMBIE };
    // SAFETY: The task remains live under tasklist_lock.
    let ptraced = unsafe { (*task).ptrace != 0 };
    // SAFETY: A negative exit signal marks a non-leader thread.
    let leader = unsafe { (*task).exit_signal >= 0 };

    let autoreap = match autoreap_mode(ptraced, leader) {
        AUTOREAP_PTRACE => {
            // SAFETY: The task remains live under tasklist_lock.
            let group_empty =
                unsafe { bindings::rust_helper_task_exit_notify_thread_group_empty(task) };
            // SAFETY: The task remains live under tasklist_lock.
            let reparented =
                unsafe { bindings::rust_helper_task_exit_notify_ptrace_reparented(task) };
            // SAFETY: The task remains live under tasklist_lock.
            let exit_signal = unsafe { (*task).exit_signal };
            // SAFETY: The task remains live under tasklist_lock.
            unsafe {
                bindings::rust_helper_task_exit_notify_notify_parent(
                    task,
                    ptrace_signal(group_empty, reparented, exit_signal),
                )
            }
        }
        AUTOREAP_LEADER => {
            // SAFETY: The task remains live under tasklist_lock.
            let group_empty =
                unsafe { bindings::rust_helper_task_exit_notify_thread_group_empty(task) };
            if group_empty {
                // SAFETY: The task remains live under tasklist_lock.
                let exit_signal = unsafe { (*task).exit_signal };
                // SAFETY: The task remains live under tasklist_lock.
                unsafe { bindings::rust_helper_task_exit_notify_notify_parent(task, exit_signal) }
            } else {
                false
            }
        }
        AUTOREAP_THREAD => {
            // SAFETY: Untraced threads self-reap after PID-fd notification.
            unsafe { bindings::rust_helper_task_exit_notify_notify_pidfd(task) };
            true
        }
        _ => false,
    };

    if autoreap {
        // SAFETY: The task is now eligible for final release.
        unsafe {
            (*task).exit_state = EXIT_DEAD;
            bindings::rust_helper_task_exit_notify_list_add(&mut (*task).ptrace_entry, dead);
        }
    }

    // SAFETY: The signal structure remains live for the exiting task group.
    let signal = unsafe { (*task).signal };
    // SAFETY: The signal lock is not required for this group-exec wake decision.
    if unsafe { (*signal).notify_count < 0 } {
        // SAFETY: The group-exec task remains live until the group completes.
        unsafe {
            bindings::rust_helper_task_exit_notify_wake_process((*signal).group_exec_task);
        }
    }

    // SAFETY: This balances the tasklist write lock acquired above.
    unsafe { bindings::rust_helper_task_release_unlock_tasklist() };

    // SAFETY: The local dead list is initialized and tasklist_lock is released.
    unsafe { release_dead(dead) };
}

/// Notifies the parent and reparents children through the C ABI.
///
/// # Safety
///
/// The same requirements as [`exit_notify`] apply.
#[export]
unsafe extern "C" fn rust_task_exit_notify(task: *mut Task, group_dead: bool) {
    #[cfg(CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST)]
    EXIT_NOTIFY_COUNT.fetch_add(1, Ordering::Relaxed);

    // SAFETY: The caller upholds the function requirements.
    unsafe { exit_notify(task, group_dead) };
}
