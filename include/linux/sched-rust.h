/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RUST_H
#define _LINUX_SCHED_RUST_H

#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_SCHED_WAKE_STATE

int rust_helper_sched_task_state(const struct task_struct *task);
int rust_helper_sched_task_saved_state(const struct task_struct *task);
void rust_helper_sched_set_task_saved_state(struct task_struct *task,
					    unsigned int state);
void rust_helper_sched_warn_ttwu_state(unsigned int state);

int rust_sched_task_state_match(const struct task_struct *task,
				unsigned int state);
bool rust_sched_ttwu_state_match(struct task_struct *task, unsigned int state,
				 int *success);

#ifdef CONFIG_KUNIT
int rust_sched_state_match_select(unsigned int task_state,
				  unsigned int saved_state,
				  unsigned int requested_state);
#endif

#endif /* CONFIG_RUST_SCHED_WAKE_STATE */

#endif /* _LINUX_SCHED_RUST_H */
