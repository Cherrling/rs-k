// SPDX-License-Identifier: GPL-2.0

#include <linux/list.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal.h>
#include <linux/task-exit-notify-rust.h>

__rust_helper struct list_head *
rust_helper_task_exit_notify_thread_head(struct task_struct *task)
{
	return &task->signal->thread_head;
}

__rust_helper struct list_head *
rust_helper_task_exit_notify_children(struct task_struct *task)
{
	return &task->children;
}

__rust_helper struct list_head *
rust_helper_task_exit_notify_ptraced(struct task_struct *task)
{
	return &task->ptraced;
}

__rust_helper struct list_head *
rust_helper_task_exit_notify_list_first(struct list_head *head)
{
	return list_empty(head) ? NULL : head->next;
}

__rust_helper struct list_head *
rust_helper_task_exit_notify_list_next(struct list_head *head,
				       struct list_head *node)
{
	return node->next == head ? NULL : node->next;
}

__rust_helper bool
rust_helper_task_exit_notify_list_empty(struct list_head *head)
{
	return list_empty(head);
}

__rust_helper void
rust_helper_task_exit_notify_list_init(struct list_head *head)
{
	INIT_LIST_HEAD(head);
}

__rust_helper void
rust_helper_task_exit_notify_list_add(struct list_head *node,
				      struct list_head *head)
{
	list_add(node, head);
}

__rust_helper void
rust_helper_task_exit_notify_list_del_init(struct list_head *node)
{
	list_del_init(node);
}

__rust_helper void
rust_helper_task_exit_notify_list_splice_tail(struct list_head *list,
					      struct list_head *head)
{
	list_splice_tail_init(list, head);
}

__rust_helper struct pid_namespace *
rust_helper_task_exit_notify_active_pid_ns(struct task_struct *task)
{
	return task_active_pid_ns(task);
}

__rust_helper struct task_struct *
rust_helper_task_exit_notify_child_reaper(struct pid_namespace *pid_ns)
{
	return pid_ns->child_reaper;
}

__rust_helper void
rust_helper_task_exit_notify_set_child_reaper(struct pid_namespace *pid_ns,
					      struct task_struct *reaper)
{
	ASSERT_EXCLUSIVE_WRITER(pid_ns->child_reaper);
	WRITE_ONCE(pid_ns->child_reaper, reaper);
}

__rust_helper void
rust_helper_task_exit_notify_zap_pid_ns(struct pid_namespace *pid_ns)
{
	if (IS_ENABLED(CONFIG_PID_NS))
		zap_pid_ns_processes(pid_ns);
}

__rust_helper void
rust_helper_task_exit_notify_set_real_parent(struct task_struct *task,
					     struct task_struct *parent)
{
	rcu_assign_pointer(task->real_parent, parent);
}

__rust_helper void
rust_helper_task_exit_notify_set_parent(struct task_struct *task)
{
	task->parent = task->real_parent;
}

__rust_helper bool
rust_helper_task_exit_notify_parent_inconsistent(struct task_struct *task,
						 struct task_struct *parent)
{
	return (!task->ptrace) !=
	       (rcu_access_pointer(task->parent) == parent);
}

__rust_helper void rust_helper_task_exit_notify_bug(void)
{
	BUG();
}

__rust_helper void
rust_helper_task_exit_notify_send_pdeath(struct task_struct *task, int signal)
{
	group_send_sig_info(signal, SEND_SIG_NOINFO, task, PIDTYPE_TGID);
}

__rust_helper bool
rust_helper_task_exit_notify_thread_group_empty(struct task_struct *task)
{
	return thread_group_empty(task);
}

__rust_helper bool
rust_helper_task_exit_notify_ptrace_reparented(struct task_struct *task)
{
	return ptrace_reparented(task);
}

__rust_helper bool
rust_helper_task_exit_notify_notify_parent(struct task_struct *task,
					   int signal)
{
	return do_notify_parent(task, signal);
}

__rust_helper void
rust_helper_task_exit_notify_notify_pidfd(struct task_struct *task)
{
	do_notify_pidfd(task);
}

__rust_helper void
rust_helper_task_exit_notify_wake_process(struct task_struct *task)
{
	wake_up_process(task);
}

__rust_helper struct task_struct *rust_helper_task_exit_notify_init_task(void)
{
	return &init_task;
}
