/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PREPARE_SIGNAL_RUST_H
#define _LINUX_PREPARE_SIGNAL_RUST_H

#include <linux/signal.h>
#include <linux/types.h>

struct signal_struct;
struct task_struct;

#ifdef CONFIG_RUST_PREPARE_SIGNAL

bool rust_prepare_signal(int sig, struct task_struct *task, bool force);

bool rust_helper_prepare_signal_is_kernel_stop(int sig);
void rust_helper_prepare_signal_init_cont_flush(sigset_t *set);
void rust_helper_prepare_signal_init_stop_flush(sigset_t *set);
void rust_helper_prepare_signal_flush(struct task_struct *task, sigset_t *mask,
				      struct sigpending *pending);
struct task_struct *
rust_helper_prepare_signal_first_thread(struct signal_struct *signal);
struct task_struct *
rust_helper_prepare_signal_next_group_thread(struct task_struct *task);
void rust_helper_prepare_signal_clear_stop_pending(struct task_struct *task);
void rust_helper_prepare_signal_clear_stopped(struct task_struct *task);
void rust_helper_prepare_signal_wake_stopped(struct task_struct *task);
bool rust_helper_prepare_signal_is_seized(struct task_struct *task);
void rust_helper_prepare_signal_ptrace_notify(struct task_struct *task);
void rust_helper_prepare_signal_set_stop_flags(struct signal_struct *signal,
					       unsigned int flags);
bool rust_helper_prepare_signal_ignored(struct task_struct *task, int sig,
					bool force);

/*
 * These bridges expose static signal internals to the Rust helper file.
 * They are not part of the Rust production interface.
 */
void rust_prepare_signal_flush_bridge(struct task_struct *task,
				      sigset_t *mask,
				      struct sigpending *pending);
void rust_prepare_signal_ptrace_notify_bridge(struct task_struct *task);
void rust_prepare_signal_set_stop_flags_bridge(struct signal_struct *signal,
					       unsigned int flags);
bool rust_prepare_signal_ignored_bridge(struct task_struct *task, int sig,
					bool force);

#ifdef CONFIG_PREPARE_SIGNAL_KUNIT_TEST
void rust_prepare_signal_test_reset(void);
u32 rust_prepare_signal_test_entry_count(void);
u32 rust_prepare_signal_test_group_exit_drop_count(void);
u32 rust_prepare_signal_test_coredump_kill_count(void);
u32 rust_prepare_signal_test_kernel_stop_count(void);
u32 rust_prepare_signal_test_continue_count(void);
u32 rust_prepare_signal_test_seized_count(void);
u32 rust_prepare_signal_test_stopped_notify_count(void);
u32 rust_prepare_signal_test_group_stop_notify_count(void);
u32 rust_prepare_signal_test_ignore_decision_count(void);
u32 rust_prepare_signal_test_ignore_drop_count(void);
u32 rust_prepare_signal_test_deliver_count(void);
#endif

#endif /* CONFIG_RUST_PREPARE_SIGNAL */

#ifdef CONFIG_PREPARE_SIGNAL_KUNIT_TEST
bool prepare_signal_test_call(int sig, struct task_struct *task, bool force);
#endif

#endif /* _LINUX_PREPARE_SIGNAL_RUST_H */
