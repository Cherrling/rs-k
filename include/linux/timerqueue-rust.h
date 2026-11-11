/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust timerqueue implementation.
 */

#ifndef _LINUX_TIMERQUEUE_RUST_H
#define _LINUX_TIMERQUEUE_RUST_H

#include <linux/timerqueue_types.h>

#ifdef CONFIG_RUST_TIMERQUEUE

bool rust_timerqueue_add(struct timerqueue_head *head,
			 struct timerqueue_node *node);
bool rust_timerqueue_del(struct timerqueue_head *head,
			 struct timerqueue_node *node);
struct timerqueue_node *
rust_timerqueue_iterate_next(struct timerqueue_node *node);
bool rust_timerqueue_linked_add(struct timerqueue_linked_head *head,
				struct timerqueue_linked_node *node);

#endif /* CONFIG_RUST_TIMERQUEUE */

#endif /* _LINUX_TIMERQUEUE_RUST_H */
