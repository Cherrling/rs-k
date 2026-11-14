// SPDX-License-Identifier: GPL-2.0

#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <linux/task-group-exit-sync-rust.h>

__rust_helper struct sighand_struct *
rust_helper_task_group_exit_sync_lock(struct task_struct *task)
{
	struct sighand_struct *sighand = task->sighand;

	spin_lock_irq(&sighand->siglock);
	return sighand;
}

__rust_helper void
rust_helper_task_group_exit_sync_unlock(struct sighand_struct *sighand)
{
	spin_unlock_irq(&sighand->siglock);
}
