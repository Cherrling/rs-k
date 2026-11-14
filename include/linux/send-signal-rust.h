/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SEND_SIGNAL_RUST_H
#define _LINUX_SEND_SIGNAL_RUST_H

#include <linux/signal.h>
#include <linux/signal_types.h>
#include <linux/types.h>

struct multiprocess_signals;
struct task_struct;

#ifdef CONFIG_RUST_SEND_SIGNAL

int rust_send_signal_locked(int sig, struct kernel_siginfo *info,
			    struct task_struct *task, unsigned int type,
			    bool force);

void rust_helper_send_signal_assert_locked(struct task_struct *task);
bool rust_helper_send_signal_is_noinfo(const struct kernel_siginfo *info);
bool rust_helper_send_signal_is_priv(const struct kernel_siginfo *info);
bool rust_helper_send_signal_is_special(const struct kernel_siginfo *info);
int rust_helper_send_signal_info_code(const struct kernel_siginfo *info);
bool rust_helper_send_signal_pending_member(const struct sigpending *pending,
					    int sig);
bool rust_helper_send_signal_kernel_stop(int sig);
struct sigqueue *
rust_helper_send_signal_alloc_queue(int sig, struct task_struct *task,
				    int override_rlimit);
void rust_helper_send_signal_append_queue(struct sigqueue *queue,
					  struct sigpending *pending);
void rust_helper_send_signal_init_noinfo(struct sigqueue *queue, int sig,
					 struct task_struct *task);
void rust_helper_send_signal_init_priv(struct sigqueue *queue, int sig);
void rust_helper_send_signal_copy_info(struct sigqueue *queue,
				       const struct kernel_siginfo *info);
void rust_helper_send_signal_signalfd_notify(struct task_struct *task, int sig);
void rust_helper_send_signal_add_pending(sigset_t *set, int sig);
void rust_helper_send_signal_delete_stop_mask(sigset_t *set);
void rust_helper_send_signal_delete_cont(sigset_t *set);
struct multiprocess_signals *
rust_helper_send_signal_first_delayed(struct signal_struct *signal);
struct multiprocess_signals *
rust_helper_send_signal_next_delayed(struct multiprocess_signals *delayed);
bool rust_helper_send_signal_prepare(int sig, struct task_struct *task,
				     bool force);
void rust_helper_send_signal_complete(int sig, struct task_struct *task,
				      unsigned int type);
void rust_helper_send_signal_trace(int sig, struct kernel_siginfo *info,
				   struct task_struct *task, bool group,
				   unsigned int result);

/*
 * These bridges expose static signal-queue internals to the Rust helper file.
 * They are not part of the Rust production interface.
 */
struct sigqueue *
rust_send_signal_alloc_queue_bridge(int sig, struct task_struct *task,
				    int override_rlimit);
bool rust_send_signal_prepare_bridge(int sig, struct task_struct *task,
				     bool force);
void rust_send_signal_complete_bridge(int sig, struct task_struct *task,
				      unsigned int type);

#ifdef CONFIG_SEND_SIGNAL_KUNIT_TEST
void rust_send_signal_test_reset(void);
u32 rust_send_signal_test_entry_count(void);
u32 rust_send_signal_test_exit_drop_count(void);
u32 rust_send_signal_test_prepare_drop_count(void);
u32 rust_send_signal_test_legacy_drop_count(void);
u32 rust_send_signal_test_alloc_skip_count(void);
u32 rust_send_signal_test_pending_count(void);
u32 rust_send_signal_test_multiprocess_count(void);
u32 rust_send_signal_test_complete_count(void);
u32 rust_send_signal_test_trace_count(void);
#endif

#endif /* CONFIG_RUST_SEND_SIGNAL */

#ifdef CONFIG_SEND_SIGNAL_KUNIT_TEST
int send_signal_test_call(int sig, struct kernel_siginfo *info,
			  struct task_struct *task, unsigned int type,
			  bool force);
#endif

#endif /* _LINUX_SEND_SIGNAL_RUST_H */
