/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust task_work implementation.
 */

#ifndef _LINUX_TASK_WORK_RUST_H
#define _LINUX_TASK_WORK_RUST_H

#include <linux/task_work.h>

#ifdef CONFIG_RUST_TASK_WORK

struct task_struct *rust_task_work_current(void);
struct callback_head *rust_task_work_exited(void);
struct callback_head **rust_task_work_head_ptr(struct task_struct *task);
struct callback_head **rust_task_work_next_ptr(struct callback_head *work);
struct callback_head *rust_task_work_read_ptr(struct callback_head **ptr);
bool rust_task_work_try_cmpxchg(struct callback_head **ptr,
				struct callback_head **old,
				struct callback_head *new);
bool rust_task_work_task_exiting(struct task_struct *task);
bool rust_task_work_func_matches(struct callback_head *work,
				 task_work_func_t func);

void rust_task_work_record_aux_stack(struct callback_head *work);
void rust_task_work_notify_resume(struct task_struct *task);
void rust_task_work_notify_signal(struct task_struct *task);
void rust_task_work_set_signal_only(struct task_struct *task);
void rust_task_work_notify_nmi_current(void);
void rust_task_work_warn_invalid_nmi_task(void);
void rust_task_work_warn_invalid_mode(void);

void rust_task_work_pi_lock(struct task_struct *task, unsigned long *flags);
void rust_task_work_pi_unlock(struct task_struct *task, unsigned long flags);
void rust_task_work_cond_resched(void);

#ifdef CONFIG_TASK_WORK_KUNIT_TEST
/* Exercise exit sealing on a synthetic task from KUnit. */
void rust_task_work_test_run_task(struct task_struct *task);
#endif

#endif /* CONFIG_RUST_TASK_WORK */

#endif /* _LINUX_TASK_WORK_RUST_H */
