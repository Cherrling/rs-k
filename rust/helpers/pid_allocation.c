// SPDX-License-Identifier: GPL-2.0

#include <linux/capability.h>
#include <linux/gfp.h>
#include <linux/idr.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/pidfs.h>
#include <linux/sched.h>
#include <linux/slab.h>

__rust_helper struct pid *
rust_helper_pid_alloc_cache_alloc(struct pid_namespace *ns)
{
	return kmem_cache_alloc(ns->pid_cachep, GFP_KERNEL);
}

__rust_helper void
rust_helper_pid_alloc_cache_free(struct pid *pid, struct pid_namespace *ns)
{
	kmem_cache_free(ns->pid_cachep, pid);
}

__rust_helper void rust_helper_pid_alloc_get_ns(struct pid_namespace *ns)
{
	get_pid_ns(ns);
}

__rust_helper void rust_helper_pid_alloc_put_ns(struct pid_namespace *ns)
{
	put_pid_ns(ns);
}

__rust_helper void rust_helper_pid_alloc_init(struct pid *pid)
{
	enum pid_type type;

	spin_lock_init(&pid->lock);
	for (type = 0; type < PIDTYPE_MAX; type++)
		INIT_HLIST_HEAD(&pid->tasks[type]);
	init_waitqueue_head(&pid->wait_pidfd);
	INIT_HLIST_HEAD(&pid->inodes);
	pidfs_prepare_pid(pid);
}

__rust_helper int rust_helper_pid_alloc_pid_max(struct pid_namespace *ns)
{
	return READ_ONCE(ns->pid_max);
}

__rust_helper bool
rust_helper_pid_alloc_capable(struct pid_namespace *ns)
{
	return checkpoint_restore_ns_capable(ns->user_ns);
}

__rust_helper bool
rust_helper_pid_alloc_child_reaper_missing(struct pid_namespace *ns)
{
	return !READ_ONCE(ns->child_reaper);
}

__rust_helper unsigned int
rust_helper_pid_alloc_idr_cursor(struct pid_namespace *ns)
{
	return idr_get_cursor(&ns->idr);
}

__rust_helper void
rust_helper_pid_alloc_idr_set_cursor(struct pid_namespace *ns,
				     unsigned int cursor)
{
	idr_set_cursor(&ns->idr, cursor);
}

__rust_helper void rust_helper_pid_alloc_idr_preload(void)
{
	idr_preload(GFP_KERNEL);
}

__rust_helper void rust_helper_pid_alloc_idr_preload_end(void)
{
	idr_preload_end();
}

__rust_helper int
rust_helper_pid_alloc_idr_alloc(struct pid_namespace *ns, int start, int end)
{
	return idr_alloc(&ns->idr, NULL, start, end, GFP_ATOMIC);
}

__rust_helper int
rust_helper_pid_alloc_idr_alloc_cyclic(struct pid_namespace *ns,
				       int start, int end)
{
	return idr_alloc_cyclic(&ns->idr, NULL, start, end, GFP_ATOMIC);
}

__rust_helper void
rust_helper_pid_alloc_idr_remove(struct pid_namespace *ns, int nr)
{
	idr_remove(&ns->idr, nr);
}

__rust_helper void
rust_helper_pid_alloc_idr_replace(struct pid_namespace *ns,
				  struct pid *pid, int nr)
{
	idr_replace(&ns->idr, pid, nr);
}

__rust_helper void
rust_helper_pid_alloc_number_set(struct pid *pid, unsigned int index,
				 int nr, struct pid_namespace *ns)
{
	pid->numbers[index].nr = nr;
	pid->numbers[index].ns = ns;
}

__rust_helper struct upid *
rust_helper_pid_alloc_number(struct pid *pid, unsigned int index)
{
	return &pid->numbers[index];
}

__rust_helper unsigned int
rust_helper_pid_alloc_allocated(struct pid_namespace *ns)
{
	return ns->pid_allocated;
}

__rust_helper bool
rust_helper_pid_alloc_disabled(struct pid_namespace *ns)
{
	return !(ns->pid_allocated & PIDNS_ADDING);
}

__rust_helper void
rust_helper_pid_alloc_count_inc(struct pid_namespace *ns)
{
	ns->pid_allocated++;
}

__rust_helper void
rust_helper_pid_alloc_active_get(struct pid_namespace *ns)
{
	ns_ref_active_get(ns);
}

__rust_helper int rust_helper_pid_alloc_pidfs_add(struct pid *pid)
{
	return pidfs_add_pid(pid);
}
