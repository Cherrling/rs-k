// SPDX-License-Identifier: GPL-2.0
#define COMPILE_OFFSETS
#include <linux/kbuild.h>
#include <linux/types.h>
#include "sched.h"

int main(void)
{
	DEFINE(RQ_nr_pinned, offsetof(struct rq, nr_pinned));
	DEFINE(RQ_nr_uninterruptible,
	       offsetof(struct rq, nr_uninterruptible));
	DEFINE(RQ_nr_iowait, offsetof(struct rq, nr_iowait));
	DEFINE(RQ_donor, offsetof(struct rq, donor));
	DEFINE(RQ_curr, offsetof(struct rq, curr));
	DEFINE(RQ_cpu, offsetof(struct rq, cpu));
	DEFINE(RQ_next_class, offsetof(struct rq, next_class));
	DEFINE(RQ_clock_update_flags, offsetof(struct rq, clock_update_flags));
	DEFINE(SCHED_CLASS_wakeup_preempt,
	       offsetof(struct sched_class, wakeup_preempt));
	DEFINE(SCHED_CLASS_task_woken,
	       offsetof(struct sched_class, task_woken));
	DEFINE(SCHED_CLASS_select_task_rq,
	       offsetof(struct sched_class, select_task_rq));

	return 0;
}
