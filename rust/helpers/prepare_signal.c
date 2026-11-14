// SPDX-License-Identifier: GPL-2.0

#include <linux/prepare-signal-rust.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>

__rust_helper bool
rust_helper_prepare_signal_is_kernel_stop(int sig)
{
	return sig_kernel_stop(sig);
}

__rust_helper void
rust_helper_prepare_signal_init_cont_flush(sigset_t *set)
{
	siginitset(set, sigmask(SIGCONT));
}

__rust_helper void
rust_helper_prepare_signal_init_stop_flush(sigset_t *set)
{
	siginitset(set, SIG_KERNEL_STOP_MASK);
}

__rust_helper void
rust_helper_prepare_signal_flush(struct task_struct *task, sigset_t *mask,
				 struct sigpending *pending)
{
	rust_prepare_signal_flush_bridge(task, mask, pending);
}

__rust_helper struct task_struct *
rust_helper_prepare_signal_first_thread(struct signal_struct *signal)
{
	struct task_struct *task;

	__for_each_thread(signal, task)
		return task;

	return NULL;
}

__rust_helper struct task_struct *
rust_helper_prepare_signal_next_group_thread(struct task_struct *task)
{
	if (list_is_last(&task->thread_node, &task->signal->thread_head))
		return NULL;

	return list_next_entry(task, thread_node);
}

__rust_helper void
rust_helper_prepare_signal_clear_stop_pending(struct task_struct *task)
{
	task_clear_jobctl_pending(task, JOBCTL_STOP_PENDING);
}

__rust_helper void
rust_helper_prepare_signal_clear_stopped(struct task_struct *task)
{
	task->jobctl &= ~JOBCTL_STOPPED;
}

__rust_helper void
rust_helper_prepare_signal_wake_stopped(struct task_struct *task)
{
	wake_up_state(task, __TASK_STOPPED);
}

__rust_helper bool
rust_helper_prepare_signal_is_seized(struct task_struct *task)
{
	return task->ptrace & PT_SEIZED;
}

__rust_helper void
rust_helper_prepare_signal_ptrace_notify(struct task_struct *task)
{
	rust_prepare_signal_ptrace_notify_bridge(task);
}

__rust_helper void
rust_helper_prepare_signal_set_stop_flags(struct signal_struct *signal,
					  unsigned int flags)
{
	rust_prepare_signal_set_stop_flags_bridge(signal, flags);
}

__rust_helper bool
rust_helper_prepare_signal_ignored(struct task_struct *task, int sig, bool force)
{
	return rust_prepare_signal_ignored_bridge(task, sig, force);
}
