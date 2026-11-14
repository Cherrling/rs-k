// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <linux/sched/signal.h>

__rust_helper void
rust_helper_signal_wake_state_set_pending(struct task_struct *task)
{
	set_tsk_thread_flag(task, TIF_SIGPENDING);
}

__rust_helper bool
rust_helper_signal_wake_state_wake_up(struct task_struct *task,
				      unsigned int state)
{
	return wake_up_state(task, state);
}

__rust_helper void
rust_helper_signal_wake_state_kick(struct task_struct *task)
{
	kick_process(task);
}
