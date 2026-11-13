// SPDX-License-Identifier: GPL-2.0

//! Native zombie task release.
//!
//! Rust owns the release ordering, signal accounting, PID and thread-list
//! unhashing, group-leader notification, and zap-leader repeat decision.
//! C helpers perform one retained lock, RCU, accounting, or subsystem
//! primitive each.

use crate::bindings;
use macros::export;

#[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
use core::sync::atomic::{AtomicU32, Ordering};

type Pid = bindings::pid;
type Sighand = bindings::sighand_struct;
type Task = bindings::task_struct;
type Tty = bindings::tty_struct;

const PID_TYPE_MAX: usize = bindings::pid_type_PIDTYPE_MAX as usize;
const PIDTYPE_PID: bindings::pid_type = bindings::pid_type_PIDTYPE_PID;
const PIDTYPE_TGID: bindings::pid_type = bindings::pid_type_PIDTYPE_TGID;
const PIDTYPE_PGID: bindings::pid_type = bindings::pid_type_PIDTYPE_PGID;
const PIDTYPE_SID: bindings::pid_type = bindings::pid_type_PIDTYPE_SID;

const EXIT_ZOMBIE: i32 = bindings::EXIT_ZOMBIE as i32;
const EXIT_DEAD: i32 = bindings::EXIT_DEAD as i32;
const SIGNAL_GROUP_EXIT: u32 = bindings::SIGNAL_GROUP_EXIT;

#[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
static RELEASE_COUNT: AtomicU32 = AtomicU32::new(0);

fn should_notify_leader(same_task: bool, group_empty: bool, exit_state: i32) -> bool {
    !same_task && group_empty && exit_state == EXIT_ZOMBIE
}

fn selected_exit_code(flags: u32, group_exit_code: i32, exit_code: i32) -> i32 {
    if flags & SIGNAL_GROUP_EXIT != 0 {
        group_exit_code
    } else {
        exit_code
    }
}

/// Tests the production group-leader notification predicate.
#[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_release_test_should_notify_leader(
    same_task: bool,
    group_empty: bool,
    exit_state: i32,
) -> bool {
    should_notify_leader(same_task, group_empty, exit_state)
}

/// Tests the production group-exit code selection.
#[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_release_test_exit_code(
    flags: u32,
    group_exit_code: i32,
    exit_code: i32,
) -> i32 {
    selected_exit_code(flags, group_exit_code, exit_code)
}

/// Returns the number of production release entries observed by the test.
#[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
#[export]
extern "C" fn rust_task_release_test_release_count() -> u32 {
    RELEASE_COUNT.load(Ordering::Relaxed)
}

/// Removes a task from its PID and thread-group lists.
///
/// # Safety
///
/// `task` must be live, `tasklist_lock` and `sighand->siglock` must be
/// write-held, and `pids` must contain `PIDTYPE_MAX` writable pointer slots.
unsafe fn unhash_process(pids: &mut [*mut Pid; PID_TYPE_MAX], task: *mut Task, group_dead: bool) {
    // SAFETY: The caller holds the locks that protect the task PID pointer.
    let pid = unsafe { (*task).thread_pid };

    // SAFETY: The caller holds the locks required by the original sequence.
    unsafe { bindings::rust_helper_task_release_dec_nr_threads() };
    // SAFETY: The caller provides the PID stash and holds tasklist_lock.
    unsafe { bindings::rust_pid_detach(pids.as_mut_ptr(), task, PIDTYPE_PID) };
    // SAFETY: The task PID remains live until it is released after unlock.
    unsafe { bindings::rust_helper_task_release_wake_pidfd(pid) };

    if group_dead {
        // SAFETY: The caller provides the PID stash and holds tasklist_lock.
        unsafe {
            bindings::rust_pid_detach(pids.as_mut_ptr(), task, PIDTYPE_TGID);
            bindings::rust_pid_detach(pids.as_mut_ptr(), task, PIDTYPE_PGID);
            bindings::rust_pid_detach(pids.as_mut_ptr(), task, PIDTYPE_SID);
            bindings::rust_helper_task_release_del_tasks(task);
            bindings::rust_helper_task_release_del_sibling(task);
            bindings::rust_helper_task_release_dec_process_count();
        }
    }

    // SAFETY: The task is still live and tasklist_lock protects this list.
    unsafe { bindings::rust_helper_task_release_del_thread_node(task) };
}

/// Releases signal state and unhashes one exiting task.
///
/// # Safety
///
/// `task` must be live and `tasklist_lock` must be write-held. `pids` must
/// contain `PIDTYPE_MAX` writable pointer slots.
unsafe fn exit_signal(pids: &mut [*mut Pid; PID_TYPE_MAX], task: *mut Task) {
    // SAFETY: The caller guarantees a live task.
    let signal = unsafe { (*task).signal };
    // SAFETY: A negative exit signal marks a non-leader thread.
    let group_dead = unsafe { (*task).exit_signal >= 0 };
    // SAFETY: The caller holds tasklist_lock for writing.
    let sighand: *mut Sighand = unsafe { bindings::rust_helper_task_release_lock_sighand(task) };

    #[cfg(CONFIG_POSIX_TIMERS)]
    // SAFETY: The caller guarantees a live task and holds its signal lock.
    unsafe {
        bindings::rust_helper_task_release_posix_timers_exit(task);
        if group_dead {
            bindings::rust_helper_task_release_posix_timers_exit_group(task);
        }
    }

    let mut tty: *mut Tty = core::ptr::null_mut();
    if group_dead {
        // SAFETY: The signal lock protects its TTY pointer.
        tty = unsafe { (*signal).tty };
        // SAFETY: The signal lock protects its TTY pointer.
        unsafe { (*signal).tty = core::ptr::null_mut() };
    } else {
        // SAFETY: The signal lock protects the group-exit notification
        // fields.
        if unsafe { (*signal).notify_count > 0 } {
            // SAFETY: The signal lock serializes this counter.
            unsafe { (*signal).notify_count -= 1 };
            // SAFETY: The signal lock protects the task pointer.
            if unsafe { (*signal).notify_count == 0 } {
                // SAFETY: The wake target remains live while the signal lock
                // serializes the transition.
                unsafe {
                    bindings::rust_helper_task_release_wake_process((*signal).group_exec_task);
                }
            }
        }

        // SAFETY: The signal lock protects the target selection.
        if unsafe { task == (*signal).curr_target } {
            // SAFETY: The task is still linked under this lock.
            let next = unsafe { bindings::rust_helper_task_release_next_thread(task) };
            // SAFETY: The signal lock protects the target selection.
            unsafe { (*signal).curr_target = next };
        }
    }

    let mut utime = 0;
    let mut stime = 0;
    // SAFETY: The task is live for the complete signal release operation.
    unsafe {
        bindings::rust_helper_task_release_task_cputime(task, &mut utime, &mut stime);
        bindings::rust_helper_task_release_stats_lock(signal);
        (*signal).utime += utime;
        (*signal).stime += stime;
        (*signal).gtime += bindings::rust_helper_task_release_gtime(task);
        (*signal).min_flt += (*task).min_flt;
        (*signal).maj_flt += (*task).maj_flt;
        (*signal).nvcsw += (*task).nvcsw;
        (*signal).nivcsw += (*task).nivcsw;
        (*signal).inblock += bindings::rust_helper_task_release_inblock(task);
        (*signal).oublock += bindings::rust_helper_task_release_oublock(task);
        bindings::rust_helper_task_release_io_accounting_add(signal, task);
        (*signal).sum_sched_runtime += (*task).se.sum_exec_runtime;
        (*signal).nr_threads -= 1;
    }

    // SAFETY: Both required locks are held.
    unsafe { unhash_process(pids, task, group_dead) };

    // SAFETY: The original sequence publishes all prior state before clearing
    // the sighand pointer.
    unsafe {
        bindings::rust_helper_task_release_stats_unlock(signal);
        bindings::rust_helper_task_release_store_sighand_null(task);
        bindings::rust_helper_task_release_unlock_sighand(sighand);
        bindings::rust_helper_task_release_cleanup_sighand(sighand);
        if group_dead {
            bindings::rust_helper_task_release_tty_put(tty);
        }
    }
}

/// Releases one zombie and returns the next task and repeat decision.
///
/// # Safety
///
/// `task` must be a dead task in the release state required by
/// `release_task()`.
unsafe fn release_one(task: *mut Task) -> (*mut Task, bool) {
    let mut pids = [core::ptr::null_mut(); PID_TYPE_MAX];

    // SAFETY: The caller guarantees the release-state task.
    unsafe {
        bindings::rust_helper_task_release_dec_nproc(task);
        bindings::rust_helper_task_release_pidfs_exit(task);
        bindings::rust_helper_task_release_cgroup(task);
    }

    // SAFETY: The caller guarantees that the task PID remains live until the
    // release path drops this reference after tasklist_lock is released.
    let thread_pid = unsafe { bindings::rust_helper_task_release_get_thread_pid(task) };

    // SAFETY: The caller holds no conflicting lock at release entry.
    unsafe { bindings::rust_helper_task_release_lock_tasklist() };
    // SAFETY: The tasklist write lock is held.
    unsafe {
        bindings::rust_helper_task_release_ptrace(task);
        exit_signal(&mut pids, task);
    }

    // SAFETY: The caller guarantees a live group leader association.
    let leader = unsafe { (*task).group_leader };
    let same_task = task == leader;
    let group_empty = if same_task {
        false
    } else {
        // SAFETY: The leader stays live while tasklist_lock is write-held.
        unsafe { bindings::rust_helper_task_release_thread_group_empty(leader) }
    };
    // SAFETY: The zombie state remains stable under tasklist_lock.
    let exit_state = unsafe { (*leader).exit_state };
    let mut zap_leader = false;

    if should_notify_leader(same_task, group_empty, exit_state) {
        // SAFETY: The leader remains live under tasklist_lock.
        let signal = unsafe { (*leader).signal };
        // SAFETY: The leader remains live under tasklist_lock.
        unsafe {
            (*leader).exit_code = selected_exit_code(
                (*signal).flags,
                (*signal).group_exit_code,
                (*leader).exit_code,
            );
            zap_leader =
                bindings::rust_helper_task_release_notify_parent(leader, (*leader).exit_signal);
            if zap_leader {
                (*leader).exit_state = EXIT_DEAD;
            }
        }
    }

    // SAFETY: This balances the tasklist write lock held above.
    unsafe { bindings::rust_helper_task_release_unlock_tasklist() };

    // SAFETY: The pinned PID reference remains live after unhashing.
    unsafe {
        bindings::rust_helper_task_release_proc_flush_pid(thread_pid);
        bindings::rust_pid_put(thread_pid);
        bindings::rust_helper_task_release_exit_cred_namespaces(task);
        bindings::rust_helper_task_release_randomness(task);
        bindings::rust_pid_free_all(pids.as_mut_ptr());
        bindings::rust_helper_task_release_thread(task);
    }

    // SAFETY: A negative exit signal marks a non-leader thread.
    if unsafe { (*task).exit_signal >= 0 } {
        // SAFETY: The task was removed from every signal list.
        unsafe { bindings::rust_helper_task_release_flush_shared_pending(task) };
    }

    // SAFETY: This drops the RCU-visible task user after all prior cleanup.
    unsafe { bindings::rust_helper_task_release_put_task_rcu_user(task) };

    (leader, zap_leader)
}

/// Releases a zombie task and any zap-owned group leader.
///
/// # Safety
///
/// `task` must satisfy the requirements of the original C `release_task()`.
#[export]
unsafe extern "C" fn rust_task_release(mut task: *mut Task) {
    #[cfg(CONFIG_TASK_RELEASE_KUNIT_TEST)]
    RELEASE_COUNT.fetch_add(1, Ordering::Relaxed);

    loop {
        // SAFETY: The caller and prior loop iteration guarantee release state.
        let (leader, zap_leader) = unsafe { release_one(task) };
        task = leader;

        if !zap_leader {
            break;
        }
    }
}
