/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_COMPLETE_SIGNAL_RUST_H
#define _LINUX_COMPLETE_SIGNAL_RUST_H

#include <linux/signal.h>
#include <linux/types.h>

struct signal_struct;
struct task_struct;

#ifdef CONFIG_RUST_COMPLETE_SIGNAL

void rust_complete_signal(int sig, struct task_struct *task, unsigned int type);

bool rust_helper_complete_signal_member(const sigset_t *set, int sig);
bool rust_helper_complete_signal_fatal(struct task_struct *task, int sig);
bool rust_helper_complete_signal_kernel_coredump(int sig);
bool rust_helper_complete_signal_thread_group_empty(struct task_struct *task);
bool rust_helper_complete_signal_stopped_or_traced(struct task_struct *task);
bool rust_helper_complete_signal_task_curr(struct task_struct *task);
bool rust_helper_complete_signal_task_pending(struct task_struct *task);
struct task_struct *
rust_helper_complete_signal_next_thread(struct task_struct *task);
struct task_struct *
rust_helper_complete_signal_first_thread(struct signal_struct *signal);
struct task_struct *
rust_helper_complete_signal_next_group_thread(struct task_struct *task);
void rust_helper_complete_signal_clear_jobctl(struct task_struct *task,
					      unsigned long mask);
void rust_helper_complete_signal_add_pending(sigset_t *set, int sig);

#ifdef CONFIG_COMPLETE_SIGNAL_KUNIT_TEST
void rust_complete_signal_test_reset(void);
u32 rust_complete_signal_test_entry_count(void);
u32 rust_complete_signal_test_suggested_count(void);
u32 rust_complete_signal_test_single_return_count(void);
u32 rust_complete_signal_test_scan_count(void);
u32 rust_complete_signal_test_no_candidate_count(void);
u32 rust_complete_signal_test_target_update_count(void);
u32 rust_complete_signal_test_group_exit_count(void);
u32 rust_complete_signal_test_coredump_skip_count(void);
u32 rust_complete_signal_test_final_wake_count(void);
#endif

#endif /* CONFIG_RUST_COMPLETE_SIGNAL */

#ifdef CONFIG_COMPLETE_SIGNAL_KUNIT_TEST
void complete_signal_test_call(int sig, struct task_struct *task, int type);
#endif

#endif /* _LINUX_COMPLETE_SIGNAL_RUST_H */
