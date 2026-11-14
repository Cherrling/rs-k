/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_RETARGET_RUST_H
#define _LINUX_SIGNAL_RETARGET_RUST_H

#include <linux/signal.h>
#include <linux/types.h>

struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_RETARGET

void rust_signal_retarget_pending(struct task_struct *task, sigset_t *which);
struct task_struct *
rust_helper_signal_retarget_next_thread(struct task_struct *task);
bool
rust_helper_signal_retarget_task_sigpending(struct task_struct *task);
void rust_helper_signal_retarget_wake_up(struct task_struct *task);

#ifdef CONFIG_SIGNAL_RETARGET_KUNIT_TEST
void rust_signal_retarget_test_reset(void);
u32 rust_signal_retarget_test_entry_count(void);
u32 rust_signal_retarget_test_group_exit_count(void);
u32 rust_signal_retarget_test_empty_count(void);
u32 rust_signal_retarget_test_thread_count(void);
u32 rust_signal_retarget_test_exiting_skip_count(void);
u32 rust_signal_retarget_test_blocked_skip_count(void);
u32 rust_signal_retarget_test_wake_count(void);
u32 rust_signal_retarget_test_complete_count(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_RETARGET */

#ifdef CONFIG_SIGNAL_RETARGET_KUNIT_TEST
void signal_retarget_test_call(struct task_struct *task, sigset_t *which);
#endif

#endif /* _LINUX_SIGNAL_RETARGET_RUST_H */
