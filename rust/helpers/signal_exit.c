// SPDX-License-Identifier: GPL-2.0

#include <linux/cgroup.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>

__rust_helper void
rust_helper_signal_exit_threadgroup_change_begin(struct task_struct *task)
{
	cgroup_threadgroup_change_begin(task);
}

__rust_helper void
rust_helper_signal_exit_threadgroup_change_end(struct task_struct *task)
{
	cgroup_threadgroup_change_end(task);
}

__rust_helper void rust_helper_signal_exit_lock(struct task_struct *task)
{
	spin_lock_irq(&task->sighand->siglock);
}

__rust_helper void rust_helper_signal_exit_unlock(struct task_struct *task)
{
	spin_unlock_irq(&task->sighand->siglock);
}

__rust_helper bool
rust_helper_signal_exit_task_sigpending(struct task_struct *task)
{
	return task_sigpending(task);
}

__rust_helper bool
rust_helper_signal_exit_thread_group_empty(struct task_struct *task)
{
	return thread_group_empty(task);
}

__rust_helper void
rust_helper_signal_exit_copy_unblocked(struct task_struct *task,
				       sigset_t *unblocked)
{
	*unblocked = task->blocked;
	signotset(unblocked);
}
