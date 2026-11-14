// SPDX-License-Identifier: GPL-2.0

#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>

__rust_helper bool
rust_helper_complete_signal_member(const sigset_t *set, int sig)
{
	return sigismember((sigset_t *)set, sig);
}

__rust_helper bool
rust_helper_complete_signal_fatal(struct task_struct *task, int sig)
{
	return sig_fatal(task, sig);
}

__rust_helper bool
rust_helper_complete_signal_kernel_coredump(int sig)
{
	return sig_kernel_coredump(sig);
}

__rust_helper bool
rust_helper_complete_signal_thread_group_empty(struct task_struct *task)
{
	return thread_group_empty(task);
}

__rust_helper bool
rust_helper_complete_signal_stopped_or_traced(struct task_struct *task)
{
	return task_is_stopped_or_traced(task);
}

__rust_helper bool
rust_helper_complete_signal_task_curr(struct task_struct *task)
{
	return task_curr(task);
}

__rust_helper bool
rust_helper_complete_signal_task_pending(struct task_struct *task)
{
	return task_sigpending(task);
}

__rust_helper struct task_struct *
rust_helper_complete_signal_next_thread(struct task_struct *task)
{
	return next_thread(task);
}

__rust_helper struct task_struct *
rust_helper_complete_signal_first_thread(struct signal_struct *signal)
{
	struct task_struct *task;

	__for_each_thread(signal, task)
		return task;

	return NULL;
}

__rust_helper struct task_struct *
rust_helper_complete_signal_next_group_thread(struct task_struct *task)
{
	if (list_is_last(&task->thread_node, &task->signal->thread_head))
		return NULL;

	return list_next_entry(task, thread_node);
}

__rust_helper void
rust_helper_complete_signal_clear_jobctl(struct task_struct *task,
					 unsigned long mask)
{
	task_clear_jobctl_pending(task, mask);
}

__rust_helper void
rust_helper_complete_signal_add_pending(sigset_t *set, int sig)
{
	sigaddset(set, sig);
}
