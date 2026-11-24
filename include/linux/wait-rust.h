/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Internal helpers for the native Rust wait queue wake path.
 */

#ifndef _LINUX_WAIT_RUST_H
#define _LINUX_WAIT_RUST_H

#include <linux/wait.h>

#ifdef CONFIG_RUST_WAITQUEUE

void rust_waitqueue_lock_irqsave(struct wait_queue_head *wq_head,
				 unsigned long *flags);
void rust_waitqueue_unlock_irqrestore(struct wait_queue_head *wq_head,
				      unsigned long flags);
void rust_waitqueue_schedule_locked(struct wait_queue_head *wq_head);
void rust_waitqueue_schedule_locked_irq(struct wait_queue_head *wq_head);
void rust_waitqueue_assert_locked(struct wait_queue_head *wq_head);
void rust_waitqueue_list_add(struct list_head *head,
			     struct list_head *entry);
void rust_waitqueue_list_add_tail(struct list_head *head,
				  struct list_head *entry);
void rust_waitqueue_list_del(struct list_head *entry);
void rust_waitqueue_list_del_init(struct list_head *entry);
void rust_waitqueue_list_del_init_careful(struct list_head *entry);
void rust_waitqueue_list_init(struct list_head *entry);
bool rust_waitqueue_head_is_empty(struct wait_queue_head *wq_head);
bool rust_waitqueue_entry_is_empty(struct wait_queue_entry *wq_entry);
bool rust_waitqueue_entry_is_empty_careful(struct wait_queue_entry *wq_entry);
void rust_waitqueue_set_current_state(unsigned int state);
void rust_waitqueue_set_current_state_relaxed(unsigned int state);
bool rust_waitqueue_signal_pending(void);
int rust_waitqueue_signal_pending_state(unsigned int state);
struct wait_queue_entry *
rust_waitqueue_first_entry(struct wait_queue_head *wq_head);
struct wait_queue_entry *
rust_waitqueue_next_entry(struct wait_queue_entry *wq_entry);
bool rust_waitqueue_entry_is_head(struct wait_queue_head *wq_head,
				  struct wait_queue_entry *wq_entry);
int rust_waitqueue_current_cpu_wake_flags(void);
int rust_waitqueue_sync_wake_flags(void);
int rust_waitqueue_default_wake(struct wait_queue_entry *wq_entry,
				unsigned int mode, int sync, void *key);
void rust_waitqueue_store_flags_mb(struct wait_queue_entry *wq_entry,
				   unsigned int flags);
void *rust_waitqueue_pollfree_key(void);
void rust_waitqueue_assert_pollfree_empty(struct wait_queue_head *wq_head);

#endif /* CONFIG_RUST_WAITQUEUE */

#endif /* _LINUX_WAIT_RUST_H */
