/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust simple wait queue implementation.
 */

#ifndef _LINUX_SWAIT_RUST_H
#define _LINUX_SWAIT_RUST_H

#include <linux/swait.h>

#ifdef CONFIG_RUST_SWAIT

void rust_swait_init_queue_head(struct swait_queue_head *q, const char *name,
				struct lock_class_key *key);
void rust_swait_wake_locked(struct swait_queue_head *q, int wake_flags);
void rust_swait_wake_all_locked(struct swait_queue_head *q);
void rust_swait_wake_one(struct swait_queue_head *q);
void rust_swait_wake_all(struct swait_queue_head *q);
void rust_swait_prepare_locked(struct swait_queue_head *q,
			       struct swait_queue *wait);
void rust_swait_prepare_exclusive(struct swait_queue_head *q,
				  struct swait_queue *wait, int state);
long rust_swait_prepare_event(struct swait_queue_head *q,
			      struct swait_queue *wait, int state);
void rust_swait_finish_locked(struct swait_queue_head *q,
			      struct swait_queue *wait);
void rust_swait_finish(struct swait_queue_head *q,
		       struct swait_queue *wait);

void rust_swait_raw_spin_lock_init(raw_spinlock_t *lock);
void rust_swait_lockdep_set_class_and_name(raw_spinlock_t *lock,
					   struct lock_class_key *key,
					   const char *name);
void rust_swait_raw_spin_lock_irqsave(raw_spinlock_t *lock,
				      unsigned long *flags);
void rust_swait_raw_spin_unlock_irqrestore(raw_spinlock_t *lock,
					   unsigned long flags);
void rust_swait_raw_spin_lock_irq(raw_spinlock_t *lock);
void rust_swait_raw_spin_unlock_irq(raw_spinlock_t *lock);

void rust_swait_list_init(struct list_head *list);
void rust_swait_list_add_tail(struct list_head *new, struct list_head *head);
void rust_swait_list_del_init(struct list_head *entry);
void rust_swait_list_splice_init(struct list_head *list,
				 struct list_head *head);
bool rust_swait_list_empty(const struct list_head *head);
bool rust_swait_list_empty_careful(const struct list_head *head);
struct swait_queue *rust_swait_first_wait(struct list_head *head);

void rust_swait_try_to_wake_up(struct task_struct *task, unsigned int state,
			       int wake_flags);
void rust_swait_set_current_state(unsigned int state);
void rust_swait_set_current_state_relaxed(unsigned int state);
int rust_swait_signal_pending_state(unsigned int state,
				    struct task_struct *task);

#endif /* CONFIG_RUST_SWAIT */

#endif /* _LINUX_SWAIT_RUST_H */
