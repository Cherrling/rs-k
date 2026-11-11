/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#ifndef CONFIG_PREEMPT_RT
#include <linux/mutex.h>
#include <linux/mutex-rust.h>

/*
 * Internal helper function; C doesn't allow us to hide it :/
 *
 * DO NOT USE (outside of mutex & scheduler code).
 */
static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	if (!lock)
		return NULL;
	return (struct task_struct *)(atomic_long_read(&lock->owner) & ~MUTEX_FLAGS);
}

static inline struct mutex *get_task_blocked_on(struct task_struct *p)
{
	guard(raw_spinlock_irqsave)(&p->blocked_lock);
	return __get_task_blocked_on(p);
}

#ifdef CONFIG_DEBUG_MUTEXES
extern void debug_mutex_lock_common(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void debug_mutex_wake_waiter(struct mutex *lock,
				    struct mutex_waiter *waiter);
extern void debug_mutex_free_waiter(struct mutex_waiter *waiter);
extern void debug_mutex_add_waiter(struct mutex *lock,
				   struct mutex_waiter *waiter,
				   struct task_struct *task);
extern void debug_mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
				      struct task_struct *task);
extern void debug_mutex_unlock(struct mutex *lock);
extern void debug_mutex_init(struct mutex *lock);
#else /* CONFIG_DEBUG_MUTEXES */
# define debug_mutex_lock_common(lock, waiter)		do { } while (0)
# define debug_mutex_wake_waiter(lock, waiter)		do { } while (0)
# define debug_mutex_free_waiter(waiter)		do { } while (0)
# define debug_mutex_add_waiter(lock, waiter, ti)	do { } while (0)
# define debug_mutex_remove_waiter(lock, waiter, ti)	do { } while (0)
# define debug_mutex_unlock(lock)			do { } while (0)
# define debug_mutex_init(lock)				do { } while (0)
#endif /* !CONFIG_DEBUG_MUTEXES */
#endif /* CONFIG_PREEMPT_RT */
