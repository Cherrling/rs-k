// SPDX-License-Identifier: GPL-2.0

#include <linux/list.h>
#include <linux/pid.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

__rust_helper struct hlist_node *
rust_helper_pid_assoc_task_link(struct task_struct *task, enum pid_type type)
{
	return &task->pid_links[type];
}

__rust_helper struct hlist_head *
rust_helper_pid_assoc_pid_tasks(struct pid *pid, enum pid_type type)
{
	return &pid->tasks[type];
}

__rust_helper bool
rust_helper_pid_assoc_has_task(struct pid *pid, enum pid_type type)
{
	return pid_has_task(pid, type);
}

__rust_helper void
rust_helper_pid_assoc_add(struct hlist_node *link, struct hlist_head *head)
{
	hlist_add_head_rcu(link, head);
}

__rust_helper void rust_helper_pid_assoc_del(struct hlist_node *link)
{
	hlist_del_rcu(link);
}

__rust_helper void
rust_helper_pid_assoc_replace(struct hlist_node *old, struct hlist_node *new)
{
	hlist_replace_rcu(old, new);
}

__rust_helper void
rust_helper_pid_assoc_swap_heads(struct hlist_head *left,
				 struct hlist_head *right)
{
	hlists_swap_heads_rcu(left, right);
}

__rust_helper void
rust_helper_pid_assoc_set_pid(struct pid **pid_ptr, struct pid *pid)
{
	*pid_ptr = pid;
}

__rust_helper void
rust_helper_pid_assoc_set_rcu(struct pid **pid_ptr, struct pid *pid)
{
	rcu_assign_pointer(*pid_ptr, pid);
}

__rust_helper pid_t rust_helper_pid_assoc_pid_nr(struct pid *pid)
{
	return pid_nr(pid);
}

__rust_helper void
rust_helper_pid_assoc_set_cached_pid(struct task_struct *task, pid_t pid)
{
	WRITE_ONCE(task->pid, pid);
}

__rust_helper bool
rust_helper_pid_assoc_stash_present(struct pid **pids, enum pid_type type)
{
	return pids[type];
}

__rust_helper void
rust_helper_pid_assoc_stash_warn(struct pid **pids, enum pid_type type)
{
	WARN_ON(pids[type]);
}

__rust_helper void
rust_helper_pid_assoc_stash_set(struct pid **pids, enum pid_type type,
				struct pid *pid)
{
	pids[type] = pid;
}

__rust_helper void
rust_helper_pid_assoc_warn_transfer_pid_type(enum pid_type type)
{
	WARN_ON_ONCE(type == PIDTYPE_PID);
}
