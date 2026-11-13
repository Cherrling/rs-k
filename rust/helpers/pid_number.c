// SPDX-License-Identifier: GPL-2.0

#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

__rust_helper struct pid *
rust_helper_pid_number_task_pid(struct task_struct *task, enum pid_type type)
{
	struct pid **pid_ptr = type == PIDTYPE_PID ?
		&task->thread_pid : &task->signal->pids[type];

	return rcu_dereference(*pid_ptr);
}
