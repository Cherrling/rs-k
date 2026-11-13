/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_RUST_H
#define _LINUX_SCHED_RUST_H

#include <linux/types.h>

struct task_struct;
struct rq;
struct rq_flags;

#if defined(CONFIG_RUST_SCHED_TTWU_RUNNABLE) || \
	defined(CONFIG_RUST_SCHED_RQ_TRANSITION)

#define RUST_SCHED_TASK_ON_RQ_QUEUED		1

#endif

#ifdef CONFIG_RUST_SCHED_RQ_TRANSITION
#define RUST_SCHED_TASK_ON_RQ_MIGRATING		2
#endif

#ifdef CONFIG_RUST_SCHED_WAKE_STATE

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

#define RUST_SCHED_TTWU_TASK_IS_BLOCKED		0x01
#define RUST_SCHED_TTWU_TASK_ON_CPU		0x02
#define RUST_SCHED_TTWU_TASK_DELAYED		0x04

void rust_helper_sched_ttwu_update_clock(struct rq *rq);
void rust_helper_sched_ttwu_enqueue_delayed(struct rq *rq,
					    struct task_struct *task);
bool rust_helper_sched_ttwu_proxy_return(struct rq *rq,
					 struct task_struct *task);

int rust_sched_ttwu_runnable_locked(struct rq *rq, struct task_struct *task,
				    int wake_flags);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_ttwu_task_flags_select(struct task_struct *task);
#endif

#endif /* CONFIG_RUST_SCHED_TTWU_RUNNABLE */

#if defined(CONFIG_RUST_SCHED_TTWU_RUNNABLE) || \
	defined(CONFIG_RUST_SCHED_TTWU_ACTIVATE)

void rust_helper_sched_ttwu_preempt(struct rq *rq, struct task_struct *task,
				    int wake_flags);
void rust_helper_sched_ttwu_trace_wakeup(struct task_struct *task);

#ifdef CONFIG_KUNIT
void rust_sched_ttwu_wakeup_state_select(struct task_struct *task);
#endif

#endif

#if defined(CONFIG_RUST_SCHED_TTWU_ACTIVATE) || \
	defined(CONFIG_RUST_SCHED_RQ_TRANSITION)

#define RUST_SCHED_ENQUEUE_MIGRATED		0x00040000

#endif

#ifdef CONFIG_RUST_SCHED_TTWU_ACTIVATE

#define RUST_SCHED_TTWU_ACTIVATE_DEC_LOAD	0x01
#define RUST_SCHED_TTWU_ACTIVATE_RQ_SELECTED	0x02
#define RUST_SCHED_TTWU_ACTIVATE_MIGRATED	0x04
#define RUST_SCHED_TTWU_ACTIVATE_IO_WAIT	0x08
#define RUST_SCHED_WF_MIGRATED			0x20
#define RUST_SCHED_WF_RQ_SELECTED		0x80
#define RUST_SCHED_ENQUEUE_WAKEUP		0x0001
#define RUST_SCHED_ENQUEUE_NOCLOCK		0x0008
#define RUST_SCHED_ENQUEUE_RQ_SELECTED		0x00100000

void rust_helper_sched_ttwu_delayacct_blkio_end(struct task_struct *task);
void rust_helper_sched_rq_unpin_lock(struct rq *rq, struct rq_flags *rf);
void rust_helper_sched_rq_repin_lock(struct rq *rq, struct rq_flags *rf);

void activate_task(struct rq *rq, struct task_struct *task, int flags);

void rust_sched_ttwu_activate_locked(struct rq *rq, struct task_struct *task,
				     int wake_flags, struct rq_flags *rf);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_ttwu_activate_select(bool contributes_load,
					     bool rq_selected, bool migrated,
					    bool in_iowait);
unsigned int rust_sched_ttwu_task_activate_flags_select(struct task_struct *task);
unsigned int rust_sched_ttwu_wake_flags_select(int wake_flags);
int rust_sched_ttwu_activate_flags_select(bool rq_selected,
					  bool migrated);
unsigned long rust_sched_ttwu_dec_uninterruptible_select(unsigned long initial);
int rust_sched_ttwu_dec_iowait_select(int initial);
#endif

#endif /* CONFIG_RUST_SCHED_TTWU_ACTIVATE */

#ifdef CONFIG_RUST_SCHED_BLOCK_TASK

#define RUST_SCHED_BLOCK_CONTRIBUTES_LOAD	0x01
#define RUST_SCHED_BLOCK_SPECIAL		0x02

bool rust_helper_sched_block_dequeue(struct rq *rq,
				     struct task_struct *task, bool special);
void rust_helper_sched_block_finish(struct rq *rq,
				    struct task_struct *task);

void rust_sched_block_task_locked(struct rq *rq, struct task_struct *task,
				  unsigned int task_state);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_block_task_select(unsigned int task_state);
void rust_sched_block_task_set_contributes_load_select(struct task_struct *task,
						       bool contributes_load);
#endif

#endif /* CONFIG_RUST_SCHED_BLOCK_TASK */

#ifdef CONFIG_RUST_SCHED_RQ_TRANSITION

#define RUST_SCHED_RQ_ADD_MIGRATED		0x01
#define RUST_SCHED_RQ_WARN_DEQUEUE_SLEEP	0x02
#define RUST_SCHED_DEQUEUE_SLEEP		0x0001

void enqueue_task(struct rq *rq, struct task_struct *task, int flags);
bool dequeue_task(struct rq *rq, struct task_struct *task, int flags);

void rust_helper_sched_rq_assert_exclusive_writer(struct task_struct *task);
void rust_helper_sched_rq_warn_dequeue_sleep(void);

void rust_sched_activate_task_locked(struct rq *rq, struct task_struct *task,
				     int flags);
void rust_sched_deactivate_task_locked(struct rq *rq, struct task_struct *task,
				       int flags);

#ifdef CONFIG_KUNIT
unsigned int rust_sched_rq_transition_select(bool migrating,
					     int deactivate_flags);
#endif

#endif /* CONFIG_RUST_SCHED_RQ_TRANSITION */

#endif /* _LINUX_SCHED_RUST_H */
