// SPDX-License-Identifier: GPL-2.0

#include <linux/cgroup.h>
#include <linux/list.h>
#include <linux/nsproxy.h>
#include <linux/pid.h>
#include <linux/pidfs.h>
#include <linux/proc_fs.h>
#include <linux/ptrace.h>
#include <linux/random.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/cputime.h>
#include <linux/sched/signal.h>
#include <linux/sched/stat.h>
#include <linux/sched/task.h>
#include <linux/signal.h>
#include <linux/task-release-rust.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/tty.h>
#include <linux/user_namespace.h>

__rust_helper void
rust_helper_task_release_dec_nproc(struct task_struct *task)
{
	dec_rlimit_ucounts(task_ucounts(task), UCOUNT_RLIMIT_NPROC, 1);
}

__rust_helper void
rust_helper_task_release_pidfs_exit(struct task_struct *task)
{
	pidfs_exit(task);
}

__rust_helper void
rust_helper_task_release_cgroup(struct task_struct *task)
{
	cgroup_task_release(task);
}

__rust_helper struct pid *
rust_helper_task_release_get_thread_pid(struct task_struct *task)
{
	return get_pid(task_pid(task));
}

__rust_helper void rust_helper_task_release_lock_tasklist(void)
{
	write_lock_irq(&tasklist_lock);
}

__rust_helper void rust_helper_task_release_unlock_tasklist(void)
{
	write_unlock_irq(&tasklist_lock);
}

__rust_helper void
rust_helper_task_release_ptrace(struct task_struct *task)
{
	ptrace_release_task(task);
}

__rust_helper void rust_helper_task_release_dec_nr_threads(void)
{
	nr_threads--;
}

__rust_helper void
rust_helper_task_release_wake_pidfd(struct pid *pid)
{
	wake_up_all(&pid->wait_pidfd);
}

__rust_helper void
rust_helper_task_release_del_tasks(struct task_struct *task)
{
	list_del_rcu(&task->tasks);
}

__rust_helper void
rust_helper_task_release_del_sibling(struct task_struct *task)
{
	list_del_init(&task->sibling);
}

__rust_helper void rust_helper_task_release_dec_process_count(void)
{
	__this_cpu_dec(process_counts);
}

__rust_helper void
rust_helper_task_release_del_thread_node(struct task_struct *task)
{
	list_del_rcu(&task->thread_node);
}

__rust_helper struct sighand_struct *
rust_helper_task_release_lock_sighand(struct task_struct *task)
{
	struct sighand_struct *sighand;

	sighand = rcu_dereference_check(task->sighand,
					lockdep_tasklist_lock_is_held());
	spin_lock(&sighand->siglock);
	return sighand;
}

__rust_helper void
rust_helper_task_release_posix_timers_exit(struct task_struct *task)
{
#ifdef CONFIG_POSIX_TIMERS
	posix_cpu_timers_exit(task);
#endif
}

__rust_helper void
rust_helper_task_release_posix_timers_exit_group(struct task_struct *task)
{
#ifdef CONFIG_POSIX_TIMERS
	posix_cpu_timers_exit_group(task);
#endif
}

__rust_helper void
rust_helper_task_release_wake_process(struct task_struct *task)
{
	wake_up_process(task);
}

__rust_helper struct task_struct *
rust_helper_task_release_next_thread(struct task_struct *task)
{
	return next_thread(task);
}

__rust_helper void
rust_helper_task_release_task_cputime(struct task_struct *task,
				      u64 *utime, u64 *stime)
{
	task_cputime(task, utime, stime);
}

__rust_helper void
rust_helper_task_release_stats_lock(struct signal_struct *signal)
{
	write_seqlock(&signal->stats_lock);
}

__rust_helper void
rust_helper_task_release_stats_unlock(struct signal_struct *signal)
{
	write_sequnlock(&signal->stats_lock);
}

__rust_helper u64
rust_helper_task_release_gtime(struct task_struct *task)
{
	return task_gtime(task);
}

__rust_helper unsigned long
rust_helper_task_release_inblock(struct task_struct *task)
{
	return task_io_get_inblock(task);
}

__rust_helper unsigned long
rust_helper_task_release_oublock(struct task_struct *task)
{
	return task_io_get_oublock(task);
}

__rust_helper void
rust_helper_task_release_io_accounting_add(struct signal_struct *signal,
					   struct task_struct *task)
{
	task_io_accounting_add(&signal->ioac, &task->ioac);
}

__rust_helper void
rust_helper_task_release_store_sighand_null(struct task_struct *task)
{
	/* Pairs with smp_acquire__after_ctrl_dep() in lock_task_sighand(). */
	smp_store_release(&task->sighand, NULL);
}

__rust_helper void
rust_helper_task_release_unlock_sighand(struct sighand_struct *sighand)
{
	spin_unlock(&sighand->siglock);
}

__rust_helper void
rust_helper_task_release_cleanup_sighand(struct sighand_struct *sighand)
{
	__cleanup_sighand(sighand);
}

__rust_helper void
rust_helper_task_release_tty_put(struct tty_struct *tty)
{
	tty_kref_put(tty);
}

__rust_helper bool
rust_helper_task_release_thread_group_empty(struct task_struct *task)
{
	return thread_group_empty(task);
}

__rust_helper bool
rust_helper_task_release_notify_parent(struct task_struct *task, int sig)
{
	return do_notify_parent(task, sig);
}

__rust_helper void
rust_helper_task_release_proc_flush_pid(struct pid *pid)
{
	proc_flush_pid(pid);
}

__rust_helper void
rust_helper_task_release_exit_cred_namespaces(struct task_struct *task)
{
	exit_cred_namespaces(task);
}

__rust_helper void
rust_helper_task_release_randomness(struct task_struct *task)
{
	add_device_randomness(&task->se.sum_exec_runtime,
			      sizeof(task->se.sum_exec_runtime));
}

__rust_helper void
rust_helper_task_release_thread(struct task_struct *task)
{
	release_thread(task);
}

__rust_helper void
rust_helper_task_release_flush_shared_pending(struct task_struct *task)
{
	flush_sigqueue(&task->signal->shared_pending);
}

__rust_helper void
rust_helper_task_release_put_task_rcu_user(struct task_struct *task)
{
	put_task_struct_rcu_user(task);
}
