/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PID_ASSOCIATION_RUST_H
#define _LINUX_PID_ASSOCIATION_RUST_H

#include <linux/pid_types.h>
#include <linux/types.h>

struct hlist_head;
struct hlist_node;
struct pid;
struct task_struct;

#ifdef CONFIG_RUST_PID_ASSOCIATION

struct pid **
rust_helper_pid_assoc_task_pid_ptr(struct task_struct *task, enum pid_type type);
struct hlist_node *
rust_helper_pid_assoc_task_link(struct task_struct *task, enum pid_type type);
struct hlist_head *
rust_helper_pid_assoc_pid_tasks(struct pid *pid, enum pid_type type);
bool rust_helper_pid_assoc_has_task(struct pid *pid, enum pid_type type);

void rust_helper_pid_assoc_add(struct hlist_node *link,
			       struct hlist_head *head);
void rust_helper_pid_assoc_del(struct hlist_node *link);
void rust_helper_pid_assoc_replace(struct hlist_node *old,
				   struct hlist_node *new);
void rust_helper_pid_assoc_swap_heads(struct hlist_head *left,
				      struct hlist_head *right);

void rust_helper_pid_assoc_set_pid(struct pid **pid_ptr, struct pid *pid);
void rust_helper_pid_assoc_set_rcu(struct pid **pid_ptr, struct pid *pid);
pid_t rust_helper_pid_assoc_pid_nr(struct pid *pid);
void rust_helper_pid_assoc_set_cached_pid(struct task_struct *task, pid_t pid);

bool rust_helper_pid_assoc_stash_present(struct pid **pids,
					 enum pid_type type);
void rust_helper_pid_assoc_stash_warn(struct pid **pids, enum pid_type type);
void rust_helper_pid_assoc_stash_set(struct pid **pids, enum pid_type type,
				     struct pid *pid);
void rust_helper_pid_assoc_warn_transfer_pid_type(enum pid_type type);
void rust_helper_pid_assoc_assert_tasklist_write(void);

void rust_pid_attach(struct task_struct *task, enum pid_type type);
void rust_pid_detach(struct pid **pids, struct task_struct *task,
		     enum pid_type type);
void rust_pid_change(struct pid **pids, struct task_struct *task,
		     enum pid_type type, struct pid *pid);
void rust_pid_exchange_tids(struct task_struct *left,
			    struct task_struct *right);
void rust_pid_transfer(struct task_struct *old, struct task_struct *new,
		       enum pid_type type);

#endif /* CONFIG_RUST_PID_ASSOCIATION */

#endif /* _LINUX_PID_ASSOCIATION_RUST_H */
