// SPDX-License-Identifier: GPL-2.0

#include <linux/hrtimer.h>
#include <linux/itimer-rust.h>
#include <linux/sched/cputime.h>
#include <linux/sched/signal.h>
#include <trace/events/timer.h>

__rust_helper void rust_helper_itimer_lock(struct task_struct *task)
{
	spin_lock_irq(&task->sighand->siglock);
}

__rust_helper void rust_helper_itimer_unlock(struct task_struct *task)
{
	spin_unlock_irq(&task->sighand->siglock);
}

__rust_helper void rust_helper_itimer_start(struct hrtimer *timer,
					    ktime_t expires)
{
	hrtimer_start(timer, expires, HRTIMER_MODE_REL);
}

__rust_helper void
rust_helper_itimer_cancel_wait_running(struct hrtimer *timer)
{
	hrtimer_cancel_wait_running(timer);
}

__rust_helper bool
rust_helper_itimer_is_queued(struct hrtimer *timer)
{
	return hrtimer_is_queued(timer);
}

__rust_helper void rust_helper_itimer_forward_now(struct hrtimer *timer,
						  ktime_t interval)
{
	hrtimer_forward_now(timer, interval);
}

__rust_helper void rust_helper_itimer_restart(struct hrtimer *timer)
{
	hrtimer_restart(timer);
}

__rust_helper u64
rust_helper_itimer_sample_cputime(struct task_struct *task,
				  unsigned int clock_id)
{
	u64 samples[CPUCLOCK_MAX];

	thread_group_sample_cputime(task, samples);
	return samples[clock_id];
}

__rust_helper void
rust_helper_itimer_set_process_cpu_timer(struct task_struct *task,
					 unsigned int clock_id, u64 *new_value,
					 u64 *old_value)
{
	set_process_cpu_timer(task, clock_id, new_value, old_value);
}

__rust_helper void
rust_helper_itimer_trace_state(unsigned int which,
			       const struct itimerspec64 *value, u64 new_value)
{
	trace_itimer_state(which, value, new_value);
}
