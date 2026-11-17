/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RCUWAIT_H_
#define _LINUX_RCUWAIT_H_

#include <linux/rcuwait-rust.h>
#include <linux/rcupdate.h>
#include <linux/sched/signal.h>
#include <linux/types.h>

#define __RCUWAIT_INITIALIZER(name)		\
	{ .task = NULL, }

#ifdef CONFIG_RUST_RCUWAIT

static inline void rcuwait_init(struct rcuwait *w)
{
	rust_rcuwait_init(w);
}

static inline int rcuwait_active(struct rcuwait *w)
{
	return rust_rcuwait_active(w);
}

#else /* !CONFIG_RUST_RCUWAIT */

static inline void rcuwait_init(struct rcuwait *w)
{
	w->task = NULL;
}

/*
 * Note: this provides no serialization and, just as with waitqueues,
 * requires care to estimate as to whether or not the wait is active.
 */
static inline int rcuwait_active(struct rcuwait *w)
{
	return !!rcu_access_pointer(w->task);
}

#endif /* CONFIG_RUST_RCUWAIT */

extern int rcuwait_wake_up(struct rcuwait *w);

/*
 * The caller is responsible for locking around rcuwait_wait_event(),
 * and [prepare_to/finish]_rcuwait() such that writes to @task are
 * properly serialized.
 */

#ifdef CONFIG_RUST_RCUWAIT

static inline void prepare_to_rcuwait(struct rcuwait *w)
{
	rust_rcuwait_prepare(w);
}

#else /* !CONFIG_RUST_RCUWAIT */

static inline void prepare_to_rcuwait(struct rcuwait *w)
{
	rcu_assign_pointer(w->task, current);
}

#endif /* CONFIG_RUST_RCUWAIT */

extern void finish_rcuwait(struct rcuwait *w);

#ifdef CONFIG_RUST_RCUWAIT

/*
 * The Rust wait engine owns the wait state machine.  The C loop is only a
 * trampoline that evaluates the caller-provided condition after Rust sets the
 * task state for each iteration.
 */

#define ___rcuwait_wait_event(w, condition, state, ret)			\
({									\
	long __ret = (ret);						\
	typeof(w) __wait = (w);						\
	typeof(state) __state = (state);				\
	rust_rcuwait_prepare(__wait);					\
	do {								\
		rust_rcuwait_wait_begin(__state);			\
	} while (!rust_rcuwait_wait_step(__wait, (condition), __state,	\
					 &__ret, false));		\
	finish_rcuwait(__wait);					\
	__ret;								\
})

#define rcuwait_wait_event(w, condition, state)				\
	___rcuwait_wait_event(w, condition, state, 0)

#define __rcuwait_wait_event_timeout(w, condition, state, timeout)	\
({									\
	long __ret = (timeout);						\
	typeof(w) __wait = (w);						\
	typeof(state) __state = (state);				\
	rust_rcuwait_prepare(__wait);					\
	do {								\
		rust_rcuwait_wait_begin(__state);			\
	} while (!rust_rcuwait_wait_step(__wait, (condition), __state,	\
					 &__ret, true));			\
	finish_rcuwait(__wait);					\
	__ret;								\
})

#define rcuwait_wait_event_timeout(w, condition, state, timeout)	\
({									\
	long __timeout = (timeout);					\
	long __ret = __timeout;					\
	if (!rust_rcuwait_wait_initial(&__ret, (condition)))		\
		__ret = __rcuwait_wait_event_timeout(w, condition,	\
						     state, __timeout);	\
	__ret;								\
})

#else /* !CONFIG_RUST_RCUWAIT */

#define ___rcuwait_wait_event(w, condition, state, ret, cmd)		\
({									\
	long __ret = ret;						\
	prepare_to_rcuwait(w);						\
	for (;;) {							\
		/*							\
		 * Implicit barrier (A) pairs with (B) in		\
		 * rcuwait_wake_up().					\
		 */							\
		set_current_state(state);				\
		if (condition)						\
			break;						\
									\
		if (signal_pending_state(state, current)) {		\
			__ret = -EINTR;					\
			break;						\
		}							\
									\
		cmd;							\
	}								\
	finish_rcuwait(w);						\
	__ret;								\
})

#define rcuwait_wait_event(w, condition, state)				\
	___rcuwait_wait_event(w, condition, state, 0, schedule())

#define __rcuwait_wait_event_timeout(w, condition, state, timeout)	\
	___rcuwait_wait_event(w, ___wait_cond_timeout(condition),	\
			      state, timeout,				\
			      __ret = schedule_timeout(__ret))

#define rcuwait_wait_event_timeout(w, condition, state, timeout)	\
({									\
	long __ret = timeout;						\
	if (!___wait_cond_timeout(condition))				\
		__ret = __rcuwait_wait_event_timeout(w, condition,	\
						     state, timeout);	\
	__ret;								\
})

#endif /* CONFIG_RUST_RCUWAIT */

#endif /* _LINUX_RCUWAIT_H_ */
