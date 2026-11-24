/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Native Rust rcuwait entry points.
 */

#ifndef _LINUX_RCUWAIT_RUST_H
#define _LINUX_RCUWAIT_RUST_H

#include <linux/types.h>

struct rcuwait;

#ifdef CONFIG_RUST_RCUWAIT

void rust_rcuwait_wait_begin(unsigned int state);
bool rust_rcuwait_wait_initial(long *ret, bool condition);
bool rust_rcuwait_wait_step(struct rcuwait *w, bool condition,
			    unsigned int state, long *ret, bool timed);

#endif /* CONFIG_RUST_RCUWAIT */

#endif /* _LINUX_RCUWAIT_RUST_H */
