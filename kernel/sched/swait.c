// SPDX-License-Identifier: GPL-2.0
/*
 * <linux/swait.h> (simple wait queues ) implementation:
 */
#include "sched.h"
#include <linux/swait-rust.h>

#ifndef CONFIG_RUST_SWAIT

void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
			     struct lock_class_key *key)
{
	raw_spin_lock_init(&q->lock);
	lockdep_set_class_and_name(&q->lock, key, name);
	INIT_LIST_HEAD(&q->task_list);
}
EXPORT_SYMBOL(__init_swait_queue_head);

/*
 * The thing about the wake_up_state() return value; I think we can ignore it.
 *
 * If for some reason it would return 0, that means the previously waiting
 * task is already running, so it will observe condition true (or has already).
 */
void swake_up_locked(struct swait_queue_head *q, int wake_flags)
{
	struct swait_queue *curr;

	if (list_empty(&q->task_list))
		return;

	curr = list_first_entry(&q->task_list, typeof(*curr), task_list);
	try_to_wake_up(curr->task, TASK_NORMAL, wake_flags);
	list_del_init(&curr->task_list);
}
EXPORT_SYMBOL(swake_up_locked);

/*
 * Wake up all waiters. This is an interface which is solely exposed for
 * completions and not for general usage.
 *
 * It is intentionally different from swake_up_all() to allow usage from
 * hard interrupt context and interrupt disabled regions.
 */
void swake_up_all_locked(struct swait_queue_head *q)
{
	while (!list_empty(&q->task_list))
		swake_up_locked(q, 0);
}

void swake_up_one(struct swait_queue_head *q)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&q->lock, flags);
	swake_up_locked(q, 0);
	raw_spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(swake_up_one);

/*
 * Does not allow usage from IRQ disabled, since we must be able to
 * release IRQs to guarantee bounded hold time.
 */
void swake_up_all(struct swait_queue_head *q)
{
	struct swait_queue *curr;
	LIST_HEAD(tmp);

	raw_spin_lock_irq(&q->lock);
	list_splice_init(&q->task_list, &tmp);
	while (!list_empty(&tmp)) {
		curr = list_first_entry(&tmp, typeof(*curr), task_list);

		wake_up_state(curr->task, TASK_NORMAL);
		list_del_init(&curr->task_list);

		if (list_empty(&tmp))
			break;

		raw_spin_unlock_irq(&q->lock);
		raw_spin_lock_irq(&q->lock);
	}
	raw_spin_unlock_irq(&q->lock);
}
EXPORT_SYMBOL(swake_up_all);

void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	wait->task = current;
	if (list_empty(&wait->task_list))
		list_add_tail(&wait->task_list, &q->task_list);
}

void prepare_to_swait_exclusive(struct swait_queue_head *q, struct swait_queue *wait, int state)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&q->lock, flags);
	__prepare_to_swait(q, wait);
	set_current_state(state);
	raw_spin_unlock_irqrestore(&q->lock, flags);
}
EXPORT_SYMBOL(prepare_to_swait_exclusive);

long prepare_to_swait_event(struct swait_queue_head *q, struct swait_queue *wait, int state)
{
	unsigned long flags;
	long ret = 0;

	raw_spin_lock_irqsave(&q->lock, flags);
	if (signal_pending_state(state, current)) {
		/*
		 * See prepare_to_wait_event(). TL;DR, subsequent swake_up_one()
		 * must not see us.
		 */
		list_del_init(&wait->task_list);
		ret = -ERESTARTSYS;
	} else {
		__prepare_to_swait(q, wait);
		set_current_state(state);
	}
	raw_spin_unlock_irqrestore(&q->lock, flags);

	return ret;
}
EXPORT_SYMBOL(prepare_to_swait_event);

void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	__set_current_state(TASK_RUNNING);
	if (!list_empty(&wait->task_list))
		list_del_init(&wait->task_list);
}

void finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	unsigned long flags;

	__set_current_state(TASK_RUNNING);

	if (!list_empty_careful(&wait->task_list)) {
		raw_spin_lock_irqsave(&q->lock, flags);
		list_del_init(&wait->task_list);
		raw_spin_unlock_irqrestore(&q->lock, flags);
	}
}
EXPORT_SYMBOL(finish_swait);

#else

void rust_swait_raw_spin_lock_init(raw_spinlock_t *lock)
{
	raw_spin_lock_init(lock);
}

void rust_swait_lockdep_set_class_and_name(raw_spinlock_t *lock,
					   struct lock_class_key *key,
					   const char *name)
{
	lockdep_set_class_and_name(lock, key, name);
}

void rust_swait_raw_spin_lock_irqsave(raw_spinlock_t *lock,
				      unsigned long *flags)
{
	raw_spin_lock_irqsave(lock, *flags);
}

void rust_swait_raw_spin_unlock_irqrestore(raw_spinlock_t *lock,
					   unsigned long flags)
{
	raw_spin_unlock_irqrestore(lock, flags);
}

void rust_swait_raw_spin_lock_irq(raw_spinlock_t *lock)
{
	raw_spin_lock_irq(lock);
}

void rust_swait_raw_spin_unlock_irq(raw_spinlock_t *lock)
{
	raw_spin_unlock_irq(lock);
}

void rust_swait_list_init(struct list_head *list)
{
	INIT_LIST_HEAD(list);
}

void rust_swait_list_add_tail(struct list_head *new, struct list_head *head)
{
	list_add_tail(new, head);
}

void rust_swait_list_del_init(struct list_head *entry)
{
	list_del_init(entry);
}

void rust_swait_list_splice_init(struct list_head *list,
				 struct list_head *head)
{
	list_splice_init(list, head);
}

bool rust_swait_list_empty(const struct list_head *head)
{
	return list_empty(head);
}

bool rust_swait_list_empty_careful(const struct list_head *head)
{
	return list_empty_careful(head);
}

struct swait_queue *rust_swait_first_wait(struct list_head *head)
{
	return list_first_entry(head, struct swait_queue, task_list);
}

void rust_swait_try_to_wake_up(struct task_struct *task, unsigned int state,
			       int wake_flags)
{
	try_to_wake_up(task, state, wake_flags);
}

void rust_swait_set_current_state(unsigned int state)
{
	set_current_state(state);
}

void rust_swait_set_current_state_relaxed(unsigned int state)
{
	__set_current_state(state);
}

int rust_swait_signal_pending_state(unsigned int state,
				    struct task_struct *task)
{
	return signal_pending_state(state, task);
}

void __init_swait_queue_head(struct swait_queue_head *q, const char *name,
			     struct lock_class_key *key)
{
	rust_swait_init_queue_head(q, name, key);
}
EXPORT_SYMBOL(__init_swait_queue_head);

void swake_up_locked(struct swait_queue_head *q, int wake_flags)
{
	rust_swait_wake_locked(q, wake_flags);
}
EXPORT_SYMBOL(swake_up_locked);

void swake_up_all_locked(struct swait_queue_head *q)
{
	rust_swait_wake_all_locked(q);
}

void swake_up_one(struct swait_queue_head *q)
{
	rust_swait_wake_one(q);
}
EXPORT_SYMBOL(swake_up_one);

void swake_up_all(struct swait_queue_head *q)
{
	rust_swait_wake_all(q);
}
EXPORT_SYMBOL(swake_up_all);

void __prepare_to_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	rust_swait_prepare_locked(q, wait);
}

void prepare_to_swait_exclusive(struct swait_queue_head *q,
				struct swait_queue *wait, int state)
{
	rust_swait_prepare_exclusive(q, wait, state);
}
EXPORT_SYMBOL(prepare_to_swait_exclusive);

long prepare_to_swait_event(struct swait_queue_head *q,
			    struct swait_queue *wait, int state)
{
	return rust_swait_prepare_event(q, wait, state);
}
EXPORT_SYMBOL(prepare_to_swait_event);

void __finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	rust_swait_finish_locked(q, wait);
}

void finish_swait(struct swait_queue_head *q, struct swait_queue *wait)
{
	rust_swait_finish(q, wait);
}
EXPORT_SYMBOL(finish_swait);

#endif /* CONFIG_RUST_SWAIT */
