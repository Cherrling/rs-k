/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust completion implementation.
 */

#ifndef _LINUX_COMPLETION_RUST_H
#define _LINUX_COMPLETION_RUST_H

#include <linux/types.h>

struct completion;
struct swait_queue;

#ifdef CONFIG_RUST_COMPLETION

void rust_completion_complete(struct completion *x, bool current_cpu);
void rust_completion_complete_all(struct completion *x);
long rust_completion_wait(struct completion *x, long timeout,
			  unsigned int state, bool io_wait);
long rust_completion_wait_interruptible(struct completion *x, long timeout,
					unsigned int state, bool io_wait);
bool rust_completion_try_wait(struct completion *x);
bool rust_completion_done(struct completion *x);

void rust_completion_lock_irqsave(struct completion *x, unsigned long *flags);
void rust_completion_unlock_irqrestore(struct completion *x,
				       unsigned long flags);
void rust_completion_lock_irq(struct completion *x);
void rust_completion_unlock_irq(struct completion *x);
void rust_completion_init_swait_queue(struct swait_queue *wait);
void rust_completion_set_current_state(unsigned int state);
int rust_completion_signal_pending_state(unsigned int state);

unsigned int rust_completion_read_done(const struct completion *x);
void rust_completion_write_done(struct completion *x, unsigned int done);
int rust_completion_current_cpu_wake_flags(void);

void rust_completion_might_sleep(void);
void rust_completion_complete_acquire(struct completion *x);
void rust_completion_complete_release(struct completion *x);
long rust_completion_schedule_timeout(long timeout);
long rust_completion_io_schedule_timeout(long timeout);
void rust_completion_assert_rt_threaded_ctx(void);

#endif /* CONFIG_RUST_COMPLETION */

#endif /* _LINUX_COMPLETION_RUST_H */
