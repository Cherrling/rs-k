/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TASK_EXIT_NOTIFY_RUST_H
#define _LINUX_TASK_EXIT_NOTIFY_RUST_H

#include <linux/types.h>

struct list_head;
struct pid_namespace;
struct task_struct;

#ifdef CONFIG_RUST_TASK_EXIT_NOTIFY

#define RUST_TASK_EXIT_NOTIFY_PTRACE	1
#define RUST_TASK_EXIT_NOTIFY_LEADER	2
#define RUST_TASK_EXIT_NOTIFY_THREAD	3

void rust_task_exit_notify(struct task_struct *task, bool group_dead);
void
rust_helper_task_exit_notify_kill_orphaned_pgrp(struct task_struct *task,
						struct task_struct *parent);

struct list_head *
rust_helper_task_exit_notify_thread_head(struct task_struct *task);
struct list_head *
rust_helper_task_exit_notify_children(struct task_struct *task);
struct list_head *
rust_helper_task_exit_notify_ptraced(struct task_struct *task);

struct list_head *
rust_helper_task_exit_notify_list_first(struct list_head *head);
struct list_head *
rust_helper_task_exit_notify_list_next(struct list_head *head,
				       struct list_head *node);
bool rust_helper_task_exit_notify_list_empty(struct list_head *head);
void rust_helper_task_exit_notify_list_init(struct list_head *head);
void rust_helper_task_exit_notify_list_add(struct list_head *node,
					   struct list_head *head);
void rust_helper_task_exit_notify_list_del_init(struct list_head *node);
void rust_helper_task_exit_notify_list_splice_tail(struct list_head *list,
						   struct list_head *head);

struct pid_namespace *
rust_helper_task_exit_notify_active_pid_ns(struct task_struct *task);
struct task_struct *
rust_helper_task_exit_notify_child_reaper(struct pid_namespace *pid_ns);
void
rust_helper_task_exit_notify_set_child_reaper(struct pid_namespace *pid_ns,
					      struct task_struct *reaper);
void rust_helper_task_exit_notify_zap_pid_ns(struct pid_namespace *pid_ns);

void rust_helper_task_exit_notify_set_real_parent(struct task_struct *task,
						  struct task_struct *parent);
void rust_helper_task_exit_notify_set_parent(struct task_struct *task);
bool
rust_helper_task_exit_notify_parent_inconsistent(struct task_struct *task,
						 struct task_struct *parent);
void rust_helper_task_exit_notify_bug(void);
void rust_helper_task_exit_notify_send_pdeath(struct task_struct *task,
					      int signal);

bool
rust_helper_task_exit_notify_thread_group_empty(struct task_struct *task);
bool
rust_helper_task_exit_notify_ptrace_reparented(struct task_struct *task);
bool rust_helper_task_exit_notify_notify_parent(struct task_struct *task,
						int signal);
void rust_helper_task_exit_notify_notify_pidfd(struct task_struct *task);
void rust_helper_task_exit_notify_wake_process(struct task_struct *task);
struct task_struct *rust_helper_task_exit_notify_init_task(void);

#ifdef CONFIG_TASK_EXIT_NOTIFY_KUNIT_TEST
u32 rust_task_exit_notify_test_autoreap_mode(bool ptraced, bool leader);
int rust_task_exit_notify_test_ptrace_signal(bool group_empty,
					     bool reparented,
					     int exit_signal);
u32 rust_task_exit_notify_test_count(void);
u32 rust_task_exit_notify_test_reparent_count(void);
u32 rust_task_exit_notify_test_child_reaper_count(void);
u32 rust_task_exit_notify_test_pid_ns_zap_count(void);
#endif

#endif /* CONFIG_RUST_TASK_EXIT_NOTIFY */

#endif /* _LINUX_TASK_EXIT_NOTIFY_RUST_H */
