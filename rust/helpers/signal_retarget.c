// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>

__rust_helper struct task_struct *
rust_helper_signal_retarget_next_thread(struct task_struct *task)
{
	return next_thread(task);
}

__rust_helper bool
rust_helper_signal_retarget_task_sigpending(struct task_struct *task)
{
	return task_sigpending(task);
}

__rust_helper void
rust_helper_signal_retarget_wake_up(struct task_struct *task)
{
	signal_wake_up(task, false);
}
