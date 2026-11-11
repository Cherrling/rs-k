/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust interval timer implementation.
 */

#ifndef _LINUX_ITIMER_RUST_H
#define _LINUX_ITIMER_RUST_H

#include <linux/ktime.h>
#include <linux/types.h>

struct hrtimer;
struct itimerspec64;
struct task_struct;

#ifdef CONFIG_RUST_ITIMER

int rust_itimer_get(struct task_struct *task, unsigned int which,
		    struct itimerspec64 *value);
int rust_itimer_set(struct task_struct *task, unsigned int which,
		    const struct itimerspec64 *value,
		    struct itimerspec64 *old_value);
void rust_itimer_rearm(struct task_struct *task);
unsigned int rust_itimer_alarm(struct task_struct *task, unsigned int seconds);

void rust_helper_itimer_start(struct hrtimer *timer, ktime_t expires);
void rust_helper_itimer_lock(struct task_struct *task);
void rust_helper_itimer_unlock(struct task_struct *task);
void rust_helper_itimer_cancel_wait_running(struct hrtimer *timer);
bool rust_helper_itimer_is_queued(struct hrtimer *timer);
void rust_helper_itimer_forward_now(struct hrtimer *timer, ktime_t interval);
void rust_helper_itimer_restart(struct hrtimer *timer);
u64 rust_helper_itimer_sample_cputime(struct task_struct *task,
				      unsigned int clock_id);
void rust_helper_itimer_set_process_cpu_timer(struct task_struct *task,
					      unsigned int clock_id, u64 *new_value,
					      u64 *old_value);
void rust_helper_itimer_trace_state(unsigned int which,
				    const struct itimerspec64 *value, u64 new_value);

#endif /* CONFIG_RUST_ITIMER */

#ifdef CONFIG_ITIMER_KUNIT_TEST
int itimer_test_get(int which, struct itimerspec64 *value);
int itimer_test_set(int which, const struct itimerspec64 *value,
		    struct itimerspec64 *old_value);
void itimer_test_rearm(void);
unsigned int itimer_test_alarm(unsigned int seconds);
#endif

#endif /* _LINUX_ITIMER_RUST_H */
