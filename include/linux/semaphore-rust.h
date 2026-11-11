/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_SEMAPHORE_RUST_H
#define _LINUX_SEMAPHORE_RUST_H

#include <linux/semaphore.h>

struct task_struct;
struct wake_q_head;

struct semaphore_waiter {
	struct list_head list;
	struct task_struct *task;
	bool up;
};

#ifdef CONFIG_RUST_SEMAPHORE

void rust_semaphore_down(struct semaphore *sem);
int rust_semaphore_down_interruptible(struct semaphore *sem);
int rust_semaphore_down_killable(struct semaphore *sem);
int rust_semaphore_down_trylock(struct semaphore *sem);
int rust_semaphore_down_timeout(struct semaphore *sem, long timeout);
void rust_semaphore_up(struct semaphore *sem, struct wake_q_head *wake_q);

void rust_semaphore_lock_irqsave(struct semaphore *sem,
				 unsigned long *flags);
void rust_semaphore_unlock_irqrestore(struct semaphore *sem,
				      unsigned long flags);
void rust_semaphore_lock_irq(struct semaphore *sem);
void rust_semaphore_unlock_irq(struct semaphore *sem);
void rust_semaphore_unlock_irqrestore_wake(struct semaphore *sem,
					   unsigned long flags,
					   struct wake_q_head *wake_q);

struct task_struct *rust_semaphore_current_task(void);
void rust_semaphore_set_current_state(unsigned int state);
int rust_semaphore_signal_pending_state(unsigned int state);
long rust_semaphore_schedule_timeout(long timeout);

void rust_semaphore_list_init(struct list_head *list);
void rust_semaphore_list_add_tail(struct list_head *new,
				  struct list_head *head);
void rust_semaphore_list_del(struct list_head *entry);
bool rust_semaphore_list_empty(const struct list_head *list);

void rust_semaphore_set_holder(struct semaphore *sem);
void rust_semaphore_clear_holder(struct semaphore *sem);
void rust_semaphore_slow_enter(struct semaphore *sem);
void rust_semaphore_slow_exit(struct semaphore *sem, int ret);
void rust_semaphore_wake_q_add(struct wake_q_head *wake_q,
			       struct task_struct *task);
void rust_semaphore_trace_release(struct semaphore *sem,
				  struct wake_q_head *wake_q);

#endif /* CONFIG_RUST_SEMAPHORE */

#endif /* _LINUX_SEMAPHORE_RUST_H */
