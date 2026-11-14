// SPDX-License-Identifier: GPL-2.0

#include <linux/posix-timers.h>
#include <linux/sched/signal.h>
#include <linux/signal-dequeue-rust.h>
#include <linux/signal.h>

__rust_helper struct sigqueue *
rust_helper_signal_dequeue_first(struct sigpending *pending)
{
	if (list_empty(&pending->list))
		return NULL;

	return list_first_entry(&pending->list, struct sigqueue, list);
}

__rust_helper struct sigqueue *
rust_helper_signal_dequeue_next(struct sigqueue *queue,
				struct sigpending *pending)
{
	if (list_is_last(&queue->list, &pending->list))
		return NULL;

	return list_next_entry(queue, list);
}

__rust_helper void
rust_helper_signal_dequeue_collect(struct sigpending *pending, int sig,
				   kernel_siginfo_t *info,
				   struct sigqueue **timer_sigq)
{
	rust_signal_dequeue_collect_bridge(pending, sig, info, timer_sigq);
}

__rust_helper void
rust_helper_signal_dequeue_remove_sync(struct sigqueue *queue,
				       kernel_siginfo_t *info)
{
	rust_signal_dequeue_remove_sync_bridge(queue, info);
}

__rust_helper void
rust_helper_signal_dequeue_recalc(void)
{
	recalc_sigpending();
}

__rust_helper void
rust_helper_signal_dequeue_rearm_itimer(struct task_struct *task)
{
	posixtimer_rearm_itimer(task);
}

__rust_helper bool
rust_helper_signal_dequeue_deliver_timer(struct kernel_siginfo *info,
					 struct sigqueue *timer_sigq)
{
	return posixtimer_deliver_signal(info, timer_sigq);
}

__rust_helper bool
rust_helper_signal_dequeue_is_kernel_stop(int sig)
{
	return sig_kernel_stop(sig);
}
