/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SIGNAL_STRUCT_COPY_RUST_H
#define _LINUX_SIGNAL_STRUCT_COPY_RUST_H

#include <linux/types.h>

struct signal_struct;
struct task_struct;

#ifdef CONFIG_RUST_SIGNAL_STRUCT_COPY

int rust_signal_struct_copy(struct task_struct *task, u64 clone_flags);

struct signal_struct *
rust_helper_signal_struct_copy_alloc(struct task_struct *task);
void rust_helper_signal_struct_copy_counts(struct signal_struct *signal);
void rust_helper_signal_struct_copy_waitqueue(struct signal_struct *signal);
void rust_helper_signal_struct_copy_pending(struct signal_struct *signal);
void rust_helper_signal_struct_copy_stats_lock(struct signal_struct *signal);
void rust_helper_signal_struct_copy_prev_cputime(struct signal_struct *signal);
void rust_helper_signal_struct_copy_posix_timers(struct signal_struct *signal);
void rust_helper_signal_struct_copy_rlimits(struct signal_struct *signal);
void rust_helper_signal_struct_copy_cpu_timers(struct signal_struct *signal);
void rust_helper_signal_struct_copy_tty_audit(struct signal_struct *signal);
void rust_helper_signal_struct_copy_autogroup(struct signal_struct *signal);
void rust_helper_signal_struct_copy_cgroup_lock(struct signal_struct *signal);
void rust_helper_signal_struct_copy_oom_scores(struct signal_struct *signal);
void rust_helper_signal_struct_copy_cred_guard_lock(struct signal_struct *signal);
void rust_helper_signal_struct_copy_exec_update_lock(struct signal_struct *signal);

#ifdef CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST
bool rust_signal_struct_copy_test_should_copy(u64 clone_flags);
u32 rust_signal_struct_copy_test_entry_count(void);
u32 rust_signal_struct_copy_test_thread_return_count(void);
u32 rust_signal_struct_copy_test_alloc_count(void);
u32 rust_signal_struct_copy_test_complete_count(void);
u32 rust_signal_struct_copy_test_alloc_failure_count(void);
#endif

#endif /* CONFIG_RUST_SIGNAL_STRUCT_COPY */

#ifdef CONFIG_SIGNAL_STRUCT_COPY_KUNIT_TEST
bool signal_struct_copy_test_shared(void);
#endif

#endif /* _LINUX_SIGNAL_STRUCT_COPY_RUST_H */
