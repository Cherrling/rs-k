/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust wait queue wake path.
 */

#ifndef _LINUX_WAIT_RUST_H
#define _LINUX_WAIT_RUST_H

#include <linux/wait.h>

#ifdef CONFIG_RUST_WAITQUEUE

int rust_waitqueue_wake(struct wait_queue_head *wq_head, unsigned int mode,
			int nr_exclusive, void *key);
void rust_waitqueue_wake_on_current_cpu(struct wait_queue_head *wq_head,
					unsigned int mode, void *key);
void rust_waitqueue_wake_locked(struct wait_queue_head *wq_head,
				unsigned int mode, int nr);
void rust_waitqueue_wake_locked_key(struct wait_queue_head *wq_head,
				    unsigned int mode, void *key);
void rust_waitqueue_wake_sync_key(struct wait_queue_head *wq_head,
				  unsigned int mode, void *key);
void rust_waitqueue_wake_locked_sync_key(struct wait_queue_head *wq_head,
					 unsigned int mode, void *key);
void rust_waitqueue_wake_sync(struct wait_queue_head *wq_head,
			      unsigned int mode);
void rust_waitqueue_wake_pollfree(struct wait_queue_head *wq_head);

void rust_waitqueue_lock_irqsave(struct wait_queue_head *wq_head,
				 unsigned long *flags);
void rust_waitqueue_unlock_irqrestore(struct wait_queue_head *wq_head,
				      unsigned long flags);
void rust_waitqueue_assert_locked(struct wait_queue_head *wq_head);
struct wait_queue_entry *
rust_waitqueue_first_entry(struct wait_queue_head *wq_head);
struct wait_queue_entry *
rust_waitqueue_next_entry(struct wait_queue_entry *wq_entry);
bool rust_waitqueue_entry_is_head(struct wait_queue_head *wq_head,
				  struct wait_queue_entry *wq_entry);
int rust_waitqueue_current_cpu_wake_flags(void);
int rust_waitqueue_sync_wake_flags(void);
void *rust_waitqueue_pollfree_key(void);
void rust_waitqueue_assert_pollfree_empty(struct wait_queue_head *wq_head);

#endif /* CONFIG_RUST_WAITQUEUE */

#endif /* _LINUX_WAIT_RUST_H */
