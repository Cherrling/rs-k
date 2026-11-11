/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust hrtimer implementation.
 */

#ifndef _LINUX_HRTIMER_RUST_H
#define _LINUX_HRTIMER_RUST_H

#include <linux/hrtimer_types.h>
#include <linux/types.h>

struct hrtimer_clock_base;

#ifdef CONFIG_RUST_HRTIMER

void rust_helper_hrtimer_debug_deactivate(struct hrtimer *timer);
void rust_helper_hrtimer_trace_start(struct hrtimer *timer, unsigned int mode,
				     bool was_armed);
void rust_helper_hrtimer_debug_activate(struct hrtimer *timer,
					unsigned int mode, bool was_armed);
void rust_helper_hrtimer_set_expires_range_ns(struct hrtimer *timer,
					      ktime_t expires, u64 delta_ns);
bool rust_helper_hrtimer_is_queued(struct hrtimer *timer);
void rust_helper_hrtimer_set_queued(struct hrtimer *timer, bool queued);

bool rust_hrtimer_requeue_same_base(struct hrtimer *timer,
				    struct hrtimer_clock_base *base,
				    unsigned int mode, ktime_t expires,
				    u64 delta_ns);

#endif /* CONFIG_RUST_HRTIMER */

#endif /* _LINUX_HRTIMER_RUST_H */
