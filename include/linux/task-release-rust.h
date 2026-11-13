/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TASK_RELEASE_RUST_H
#define _LINUX_TASK_RELEASE_RUST_H

#include <linux/types.h>

struct pid;
struct sighand_struct;
struct signal_struct;
struct task_struct;
struct tty_struct;

#ifdef CONFIG_RUST_TASK_RELEASE

void rust_task_release(struct task_struct *task);

void rust_helper_task_release_dec_nproc(struct task_struct *task);
void rust_helper_task_release_pidfs_exit(struct task_struct *task);
void rust_helper_task_release_cgroup(struct task_struct *task);
struct pid *rust_helper_task_release_get_thread_pid(struct task_struct *task);

void rust_helper_task_release_lock_tasklist(void);
void rust_helper_task_release_unlock_tasklist(void);
void rust_helper_task_release_ptrace(struct task_struct *task);

void rust_helper_task_release_dec_nr_threads(void);
void rust_helper_task_release_wake_pidfd(struct pid *pid);
void rust_helper_task_release_del_tasks(struct task_struct *task);
void rust_helper_task_release_del_sibling(struct task_struct *task);
void rust_helper_task_release_dec_process_count(void);
void rust_helper_task_release_del_thread_node(struct task_struct *task);

struct sighand_struct *
rust_helper_task_release_lock_sighand(struct task_struct *task);
void rust_helper_task_release_posix_timers_exit(struct task_struct *task);
void rust_helper_task_release_posix_timers_exit_group(struct task_struct *task);
void rust_helper_task_release_wake_process(struct task_struct *task);
struct task_struct *rust_helper_task_release_next_thread(struct task_struct *task);
void rust_helper_task_release_task_cputime(struct task_struct *task,
					   u64 *utime, u64 *stime);
void rust_helper_task_release_stats_lock(struct signal_struct *signal);
void rust_helper_task_release_stats_unlock(struct signal_struct *signal);
u64 rust_helper_task_release_gtime(struct task_struct *task);
unsigned long rust_helper_task_release_inblock(struct task_struct *task);
unsigned long rust_helper_task_release_oublock(struct task_struct *task);
void
rust_helper_task_release_io_accounting_add(struct signal_struct *signal,
					   struct task_struct *task);
void rust_helper_task_release_store_sighand_null(struct task_struct *task);
void rust_helper_task_release_unlock_sighand(struct sighand_struct *sighand);
void rust_helper_task_release_cleanup_sighand(struct sighand_struct *sighand);
void rust_helper_task_release_tty_put(struct tty_struct *tty);

bool rust_helper_task_release_thread_group_empty(struct task_struct *task);
bool rust_helper_task_release_notify_parent(struct task_struct *task, int sig);
void rust_helper_task_release_proc_flush_pid(struct pid *pid);
void rust_helper_task_release_exit_cred_namespaces(struct task_struct *task);
void rust_helper_task_release_randomness(struct task_struct *task);
void rust_helper_task_release_thread(struct task_struct *task);
void rust_helper_task_release_flush_shared_pending(struct task_struct *task);
void rust_helper_task_release_put_task_rcu_user(struct task_struct *task);

#ifdef CONFIG_TASK_RELEASE_KUNIT_TEST
bool rust_task_release_test_should_notify_leader(bool same_task,
						 bool group_empty,
						 int exit_state);
int rust_task_release_test_exit_code(u32 flags, int group_exit_code,
				     int exit_code);
u32 rust_task_release_test_release_count(void);
#endif

#endif /* CONFIG_RUST_TASK_RELEASE */

#endif /* _LINUX_TASK_RELEASE_RUST_H */
