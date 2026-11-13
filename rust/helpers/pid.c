// SPDX-License-Identifier: GPL-2.0

#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/pidfs.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/slab.h>

__rust_helper struct upid *rust_helper_pid_upid(struct pid *pid,
						unsigned int level)
{
	return &pid->numbers[level];
}

__rust_helper struct pid_namespace *
rust_helper_pid_active_namespace(struct pid *pid)
{
	return pid->numbers[pid->level].ns;
}

__rust_helper void rust_helper_pid_ns_active_put(struct pid_namespace *ns)
{
	ns_ref_active_put(ns);
}

__rust_helper void
rust_helper_pid_wake_child_reaper(struct pid_namespace *ns)
{
	wake_up_process(READ_ONCE(ns->child_reaper));
}

__rust_helper void
rust_helper_pid_warn_child_reaper(struct pid_namespace *ns)
{
	WARN_ON(READ_ONCE(ns->child_reaper));
}

__rust_helper void rust_helper_pid_idr_remove(struct pid_namespace *ns, int nr)
{
	idr_remove(&ns->idr, nr);
}

__rust_helper void rust_helper_pidfs_remove(struct pid *pid)
{
	pidfs_remove_pid(pid);
}

__rust_helper void rust_helper_pidfs_free(struct pid *pid)
{
	pidfs_free_pid(pid);
}

__rust_helper void rust_helper_pid_cache_free(struct pid *pid,
					      struct pid_namespace *ns)
{
	kmem_cache_free(ns->pid_cachep, pid);
}

__rust_helper void rust_helper_pid_call_rcu(struct pid *pid,
					    rcu_callback_t callback)
{
	call_rcu(&pid->rcu, callback);
}
