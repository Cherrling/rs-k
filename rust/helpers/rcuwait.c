// SPDX-License-Identifier: GPL-2.0

#include <linux/rcuwait.h>
#include <linux/sched/signal.h>

__rust_helper void rust_helper_rcuwait_assign_current(struct rcuwait *w)
{
	rcu_assign_pointer(w->task, current);
}

__rust_helper void rust_helper_rcuwait_assign_null(struct rcuwait *w)
{
	rcu_assign_pointer(w->task, NULL);
}

__rust_helper struct task_struct *
rust_helper_rcuwait_dereference(const struct rcuwait *w)
{
	return rcu_dereference(w->task);
}

__rust_helper bool rust_helper_rcuwait_access(const struct rcuwait *w)
{
	return !!rcu_access_pointer(w->task);
}

__rust_helper void rust_helper_rcuwait_set_current_state(unsigned int state)
{
	set_current_state(state);
}

__rust_helper void rust_helper_rcuwait_set_current_running(void)
{
	__set_current_state(TASK_RUNNING);
}

__rust_helper int
rust_helper_rcuwait_signal_pending_state(unsigned int state)
{
	return signal_pending_state(state, current);
}

__rust_helper void rust_helper_rcuwait_schedule(void)
{
	schedule();
}

__rust_helper long rust_helper_rcuwait_schedule_timeout(long timeout)
{
	return schedule_timeout(timeout);
}

__rust_helper int rust_helper_rcuwait_wake_up_process(struct task_struct *task)
{
	return wake_up_process(task);
}
