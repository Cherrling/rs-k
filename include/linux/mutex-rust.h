/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MUTEX_RUST_H
#define _LINUX_MUTEX_RUST_H

#include <linux/list.h>
#include <linux/mutex_types.h>
#include <linux/types.h>

struct task_struct;
struct ww_acquire_ctx;
struct ww_mutex;
struct lock_class_key;
struct lockdep_map;

#ifndef CONFIG_PREEMPT_RT

/*
 * This is the control structure for tasks blocked on mutex, which resides
 * on the blocked task's kernel stack:
 */
struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
	struct ww_acquire_ctx	*ww_ctx;
#ifdef CONFIG_DEBUG_MUTEXES
	void			*magic;
#endif
};

/*
 * @owner contains a task pointer with state in the low alignment bits.
 *
 * Bit 0 indicates a non-empty waiter list, bit 1 requests direct handoff,
 * and bit 2 indicates that a handoff is waiting for pickup.
 */
#define MUTEX_FLAG_WAITERS	0x01
#define MUTEX_FLAG_HANDOFF	0x02
#define MUTEX_FLAG_PICKUP	0x04

#define MUTEX_FLAGS		0x07

#ifdef CONFIG_RUST_MUTEX

void rust_mutex_init_generic(struct mutex *lock);
void rust_mutex_init_lockdep(struct mutex *lock, const char *name,
			     struct lock_class_key *key);
void rust_mutex_might_sleep(void);
void rust_mutex_preempt_disable(void);
void rust_mutex_preempt_enable(void);
void rust_mutex_warn_bad_lock(struct mutex *lock);
void rust_mutex_warn_bad_ww_lock(struct ww_mutex *lock);
#ifdef CONFIG_DEBUG_LOCK_ALLOC
void rust_mutex_lockdep_acquire(struct mutex *lock, unsigned int subclass,
				bool trylock, struct lockdep_map *nest);
#endif
void rust_mutex_contention_begin_spin(struct mutex *lock);
void rust_mutex_contention_end(struct mutex *lock, int ret);
void rust_mutex_lock_acquired(struct mutex *lock);
void rust_ww_mutex_set_context_fastpath(struct ww_mutex *lock,
					struct ww_acquire_ctx *ctx);
void rust_ww_mutex_unlock_context(struct ww_mutex *lock);
int rust_ww_mutex_deadlock_injection(struct ww_mutex *lock,
				     struct ww_acquire_ctx *ctx);
int rust_mutex_lock_slow(struct mutex *lock, unsigned int state,
			 struct ww_acquire_ctx *ww_ctx, bool use_ww_ctx);
void rust_mutex_unlock_slow(struct mutex *lock);

bool rust_mutex_is_locked(struct mutex *lock);
unsigned long rust_mutex_get_owner(struct mutex *lock);
bool rust_mutex_trylock_fast(struct mutex *lock);
bool rust_mutex_unlock_fast(struct mutex *lock);
struct task_struct *rust_mutex_trylock_common(struct mutex *lock,
					      bool handoff);
bool rust_mutex_unlock_prepare(struct mutex *lock, unsigned long *owner);
bool rust_mutex_handoff_if_requested(struct mutex *lock,
				     unsigned long owner,
				     struct task_struct *task);

unsigned long rust_mutex_read_owner(struct mutex *lock);
bool rust_mutex_try_cmpxchg_owner_acquire(struct mutex *lock,
					  unsigned long *owner,
					  unsigned long new);
bool rust_mutex_try_cmpxchg_owner_release(struct mutex *lock,
					  unsigned long *owner,
					  unsigned long new);
unsigned long rust_mutex_current(void);
void rust_mutex_warn_bad_trylock_flags(struct mutex *lock,
				       unsigned long flags);
void rust_mutex_warn_bad_unlock(struct mutex *lock, unsigned long owner);
bool rust_mutex_force_handoff(void);

#endif /* CONFIG_RUST_MUTEX */

#endif /* !CONFIG_PREEMPT_RT */

#endif /* _LINUX_MUTEX_RUST_H */
