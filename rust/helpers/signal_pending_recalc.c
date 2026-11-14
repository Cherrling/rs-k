// SPDX-License-Identifier: GPL-2.0

#include <linux/cgroup.h>
#include <linux/freezer.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal-pending-recalc-rust.h>

__rust_helper void
rust_helper_signal_pending_recalc_set(struct task_struct *task)
{
	set_tsk_thread_flag(task, TIF_SIGPENDING);
}

__rust_helper void
rust_helper_signal_pending_recalc_clear(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
}

__rust_helper bool
rust_helper_signal_pending_recalc_test(struct task_struct *task)
{
	return test_tsk_thread_flag(task, TIF_SIGPENDING);
}

__rust_helper bool
rust_helper_signal_pending_recalc_cgroup_frozen(struct task_struct *task)
{
	return cgroup_task_frozen(task);
}

__rust_helper bool
rust_helper_signal_pending_recalc_freezing(struct task_struct *task)
{
	return freezing(task);
}
