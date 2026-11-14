/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_DEQUEUE_RUST_H
#define _LINUX_SIGNAL_DEQUEUE_RUST_H

#include <linux/signal.h>
#include <linux/types.h>

struct kernel_siginfo;
struct sigqueue;
struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_DEQUEUE

int rust_signal_next_pending(struct sigpending *pending, sigset_t *mask);
int rust_signal_dequeue(struct task_struct *task, sigset_t *mask,
			kernel_siginfo_t *info, enum pid_type *type);
int rust_signal_dequeue_synchronous(struct task_struct *task,
				    kernel_siginfo_t *info);

struct sigqueue *
rust_helper_signal_dequeue_first(struct sigpending *pending);
struct sigqueue *
rust_helper_signal_dequeue_next(struct sigqueue *queue,
				struct sigpending *pending);
void rust_helper_signal_dequeue_collect(struct sigpending *pending, int sig,
					kernel_siginfo_t *info,
					struct sigqueue **timer_sigq);
void rust_helper_signal_dequeue_remove_sync(struct sigqueue *queue,
					    kernel_siginfo_t *info);
void rust_helper_signal_dequeue_recalc(void);
void rust_helper_signal_dequeue_rearm_itimer(struct task_struct *task);
bool rust_helper_signal_dequeue_deliver_timer(struct kernel_siginfo *info,
					      struct sigqueue *timer_sigq);
bool rust_helper_signal_dequeue_is_kernel_stop(int sig);

/*
 * These bridges expose static signal queue internals to the Rust helper file.
 * They are not part of the Rust production interface.
 */
void rust_signal_dequeue_collect_bridge(struct sigpending *pending, int sig,
					kernel_siginfo_t *info,
					struct sigqueue **timer_sigq);
void rust_signal_dequeue_remove_sync_bridge(struct sigqueue *queue,
					    kernel_siginfo_t *info);

#ifdef CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST
void rust_signal_dequeue_test_reset(void);
u32 rust_signal_dequeue_test_next_entry_count(void);
u32 rust_signal_dequeue_test_next_first_count(void);
u32 rust_signal_dequeue_test_next_synchronous_count(void);
u32 rust_signal_dequeue_test_dequeue_entry_count(void);
u32 rust_signal_dequeue_test_local_count(void);
u32 rust_signal_dequeue_test_shared_count(void);
u32 rust_signal_dequeue_test_empty_count(void);
u32 rust_signal_dequeue_test_stop_marker_count(void);
u32 rust_signal_dequeue_test_timer_retry_count(void);
u32 rust_signal_dequeue_test_deliver_count(void);
u32 rust_signal_dequeue_test_synchronous_entry_count(void);
u32 rust_signal_dequeue_test_synchronous_precheck_miss_count(void);
u32 rust_signal_dequeue_test_synchronous_scan_miss_count(void);
u32 rust_signal_dequeue_test_synchronous_duplicate_count(void);
u32 rust_signal_dequeue_test_synchronous_clear_count(void);
u32 rust_signal_dequeue_test_synchronous_deliver_count(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_DEQUEUE */

#ifdef CONFIG_SIGNAL_DEQUEUE_KUNIT_TEST
int signal_dequeue_test_call(sigset_t *mask, kernel_siginfo_t *info,
			     enum pid_type *type);
int signal_dequeue_synchronous_test_call(kernel_siginfo_t *info);
int signal_dequeue_test_queue_sync(int sig, int code);
#endif

#endif /* _LINUX_SIGNAL_DEQUEUE_RUST_H */
