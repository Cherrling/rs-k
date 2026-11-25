/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_EXIT_RUST_H
#define _LINUX_SIGNAL_EXIT_RUST_H

#include <linux/signal.h>
#include <linux/types.h>

struct list_head;
struct sigpending;
struct task_struct;

#ifdef CONFIG_RUST_EXIT_SIGNALS

void rust_helper_signal_exit_threadgroup_change_begin(struct task_struct *task);
void rust_helper_signal_exit_threadgroup_change_end(struct task_struct *task);
void rust_helper_signal_exit_lock(struct task_struct *task);
void rust_helper_signal_exit_unlock(struct task_struct *task);
void rust_helper_signal_exit_init_queue(struct list_head *queue);
void rust_helper_signal_exit_dequeue_pending(struct sigpending *pending,
					     struct list_head *queue);
void rust_helper_signal_exit_flush_queue(struct list_head *queue);
bool rust_helper_signal_exit_task_sigpending(struct task_struct *task);
bool rust_helper_signal_exit_thread_group_empty(struct task_struct *task);
void rust_helper_signal_exit_copy_unblocked(struct task_struct *task, sigset_t *unblocked);
bool rust_helper_signal_exit_participate_group_stop(struct task_struct *task);
void rust_helper_signal_exit_notify_group_stop(struct task_struct *task, int group_stop);

#ifdef CONFIG_SIGNAL_EXIT_KUNIT_TEST
void rust_signal_exit_test_reset(void);
void rust_signal_exit_test_arm(struct task_struct *task);
u32 rust_signal_exit_test_entry_count(void);
u32 rust_signal_exit_test_threadgroup_begin_count(void);
u32 rust_signal_exit_test_threadgroup_end_count(void);
u32 rust_signal_exit_test_pf_exiting_count(void);
u32 rust_signal_exit_test_dequeue_count(void);
u32 rust_signal_exit_test_retarget_count(void);
u32 rust_signal_exit_test_group_stop_count(void);
u32 rust_signal_exit_test_notify_count(void);
#endif

#endif /* CONFIG_RUST_EXIT_SIGNALS */

#endif /* _LINUX_SIGNAL_EXIT_RUST_H */
