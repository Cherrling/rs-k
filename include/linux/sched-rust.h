/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RUST_H
#define _LINUX_SCHED_RUST_H

#include <linux/types.h>

struct task_struct;
struct rq;
struct rq_flags;

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

#ifdef CONFIG_RUST_SCHED_TTWU_RUNNABLE

bool rust_helper_sched_ttwu_rq_queued(struct task_struct *task);
void rust_helper_sched_ttwu_update_clock(struct rq *rq);
bool rust_helper_sched_ttwu_is_blocked(struct task_struct *task);
bool rust_helper_sched_ttwu_is_delayed(struct task_struct *task);
void rust_helper_sched_ttwu_enqueue_delayed(struct rq *rq,
					    struct task_struct *task);
bool rust_helper_sched_ttwu_proxy_return(struct rq *rq,
					 struct task_struct *task);
bool rust_helper_sched_ttwu_on_cpu(struct rq *rq, struct task_struct *task);

int rust_sched_ttwu_runnable_locked(struct rq *rq, struct task_struct *task,
				    int wake_flags);

#endif /* CONFIG_RUST_SCHED_TTWU_RUNNABLE */

#if defined(CONFIG_RUST_SCHED_TTWU_RUNNABLE) || \
	defined(CONFIG_RUST_SCHED_TTWU_ACTIVATE)

void rust_helper_sched_ttwu_preempt(struct rq *rq, struct task_struct *task,
				    int wake_flags);
void rust_helper_sched_ttwu_wake(struct task_struct *task);

#endif

#ifdef CONFIG_RUST_SCHED_TTWU_ACTIVATE

#define RUST_SCHED_TTWU_ACTIVATE_DEC_LOAD	0x01
#define RUST_SCHED_TTWU_ACTIVATE_RQ_SELECTED	0x02
#define RUST_SCHED_TTWU_ACTIVATE_MIGRATED	0x04
#define RUST_SCHED_TTWU_ACTIVATE_IO_WAIT	0x08

bool rust_helper_sched_ttwu_contributes_load(struct task_struct *task);
void rust_helper_sched_ttwu_dec_uninterruptible(struct rq *rq);
bool rust_helper_sched_ttwu_rq_selected(int wake_flags);
bool rust_helper_sched_ttwu_migrated(int wake_flags);
bool rust_helper_sched_ttwu_in_iowait(struct task_struct *task);
void rust_helper_sched_ttwu_io_wait_end(struct task_struct *task);
void rust_helper_sched_ttwu_activate(struct rq *rq, struct task_struct *task,
				     bool rq_selected, bool migrated);
bool rust_helper_sched_ttwu_has_task_woken(struct task_struct *task);
void rust_helper_sched_ttwu_task_woken(struct rq *rq,
				       struct task_struct *task,
				       struct rq_flags *rf);

void rust_sched_ttwu_activate_locked(struct rq *rq, struct task_struct *task,
				     int wake_flags, struct rq_flags *rf);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_ttwu_activate_select(bool contributes_load,
					     bool rq_selected, bool migrated,
					    bool in_iowait);
#endif

#endif /* CONFIG_RUST_SCHED_TTWU_ACTIVATE */

#ifdef CONFIG_RUST_SCHED_BLOCK_TASK

#define RUST_SCHED_BLOCK_CONTRIBUTES_LOAD	0x01
#define RUST_SCHED_BLOCK_SPECIAL		0x02

void rust_helper_sched_block_set_contributes_load(struct task_struct *task,
						  bool contributes_load);
bool rust_helper_sched_block_dequeue(struct rq *rq,
				     struct task_struct *task, bool special);
void rust_helper_sched_block_finish(struct rq *rq,
				    struct task_struct *task);

void rust_sched_block_task_locked(struct rq *rq, struct task_struct *task,
				  unsigned int task_state);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_block_task_select(unsigned int task_state);
#endif

#endif /* CONFIG_RUST_SCHED_BLOCK_TASK */

#endif /* _LINUX_SCHED_RUST_H */
