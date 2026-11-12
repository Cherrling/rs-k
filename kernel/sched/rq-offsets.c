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

	return 0;
}
