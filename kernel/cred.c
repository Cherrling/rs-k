// SPDX-License-Identifier: GPL-2.0-or-later
/* Task credentials management - see Documentation/security/credentials.rst
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#define pr_fmt(fmt) "CRED: " fmt

#include <linux/export.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/sched/coredump.h>
#include <linux/key.h>
#include <linux/keyctl.h>
#include <linux/init_task.h>
#include <linux/security.h>
#include <linux/binfmts.h>
#include <linux/cn_proc.h>
#include <linux/cred-rust.h>
#include <linux/uidgid.h>

#if 0
#define kdebug(FMT, ...)						\
	printk("[%-5.5s%5u] " FMT "\n",					\
	       current->comm, current->pid, ##__VA_ARGS__)
#else
#define kdebug(FMT, ...)						\
do {									\
	if (0)								\
		no_printk("[%-5.5s%5u] " FMT "\n",			\
			  current->comm, current->pid, ##__VA_ARGS__);	\
} while (0)
#endif

static struct kmem_cache *cred_jar;

#ifdef CONFIG_RUST_CRED_LIFECYCLE

struct cred *rust_helper_cred_alloc(gfp_t gfp)
{
	return kmem_cache_alloc(cred_jar, gfp);
}

struct cred *rust_helper_cred_alloc_zero(gfp_t gfp)
{
	return kmem_cache_zalloc(cred_jar, gfp);
}

void rust_helper_cred_free(struct cred *cred)
{
	kmem_cache_free(cred_jar, cred);
}

struct task_struct *rust_helper_cred_current(void)
{
	return current;
}

const struct cred *rust_helper_cred_task_objective(struct task_struct *task)
{
	return __task_cred(task);
}

void rust_helper_cred_rcu_read_lock(void)
{
	rcu_read_lock();
}

void rust_helper_cred_rcu_read_unlock(void)
{
	rcu_read_unlock();
}

void rust_helper_cred_publish_current(struct task_struct *task,
				      struct cred *new)
{
	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
}

void rust_helper_cred_set_task_creds(struct task_struct *task,
				     const struct cred *new)
{
	task->cred = new;
	task->real_cred = new;
}

long rust_helper_cred_usage_read(const struct cred *cred)
{
	return atomic_long_read(&cred->usage);
}

void rust_helper_cred_usage_set(struct cred *cred, long usage)
{
	atomic_long_set(&cred->usage, usage);
}

void rust_helper_cred_get_many(const struct cred *cred, long nr)
{
	struct cred *new = (struct cred *)cred;

	new->non_rcu = 0;
	atomic_long_add(nr, &new->usage);
}

bool rust_helper_cred_get_rcu(const struct cred *cred)
{
	struct cred *new = (struct cred *)cred;

	if (!atomic_long_inc_not_zero(&new->usage))
		return false;
	new->non_rcu = 0;
	return true;
}

bool rust_helper_cred_sub_and_test(struct cred *cred, long nr)
{
	return atomic_long_sub_and_test(nr, &cred->usage);
}

bool rust_helper_cred_non_rcu(const struct cred *cred)
{
	return cred->non_rcu;
}

void rust_helper_cred_clear_non_rcu(struct cred *cred)
{
	cred->non_rcu = 0;
}

struct group_info *rust_helper_cred_group_get(struct group_info *group)
{
	if (group)
		refcount_inc(&group->usage);
	return group;
}

void rust_helper_cred_group_put(struct group_info *group)
{
	if (group)
		put_group_info(group);
}

u32 rust_helper_cred_group_gid(const struct group_info *group, int index)
{
	return group->gid[index].val;
}

struct user_struct *rust_helper_cred_user_get(struct user_struct *user)
{
	if (user)
		refcount_inc(&user->__count);
	return user;
}

void rust_helper_cred_user_put(struct user_struct *user)
{
	free_uid(user);
}

struct user_namespace *
rust_helper_cred_user_ns_get(struct user_namespace *user_ns)
{
	return get_user_ns(user_ns);
}

void rust_helper_cred_user_ns_put(struct user_namespace *user_ns)
{
	put_user_ns(user_ns);
}

struct ucounts *rust_helper_cred_ucounts_get(struct ucounts *ucounts)
{
	if (!ucounts)
		return NULL;
	if (!rcuref_get(&ucounts->count))
		return NULL;
	return ucounts;
}

void rust_helper_cred_ucounts_put(struct ucounts *ucounts)
{
	if (ucounts)
		put_ucounts(ucounts);
}

struct key *rust_helper_cred_key_get(struct key *key)
{
	return key_get(key);
}

void rust_helper_cred_key_put(struct key *key)
{
	key_put(key);
}

unsigned char rust_helper_cred_default_jit_keyring(void)
{
#ifdef CONFIG_KEYS
	return KEY_REQKEY_DEFL_THREAD_KEYRING;
#else
	return 0;
#endif
}

int rust_helper_cred_security_alloc_blank(struct cred *cred)
{
	return security_cred_alloc_blank(cred, GFP_KERNEL_ACCOUNT);
}

int rust_helper_cred_security_prepare(struct cred *new,
				      const struct cred *old)
{
	return security_prepare_creds(new, old, GFP_KERNEL_ACCOUNT);
}

void rust_helper_cred_security_free(struct cred *cred)
{
	security_cred_free(cred);
}

int rust_helper_cred_security_act_as(struct cred *cred, u32 secid)
{
	return security_kernel_act_as(cred, secid);
}

int rust_helper_cred_security_create_files_as(struct cred *cred,
					      struct inode *inode)
{
	return security_kernel_create_files_as(cred, inode);
}

void rust_helper_cred_invariant_failed(void)
{
	panic("CRED: credential lifecycle invariant failed\n");
}

bool rust_helper_cred_warn_on_once(bool condition)
{
	return WARN_ON_ONCE(condition);
}

void rust_helper_cred_apply_dumpability(struct task_struct *task)
{
	/* mm-less tasks share init_task's exec_state. */
	if (task->mm)
		task_exec_state_set_dumpable(suid_dumpable);
}

void rust_helper_cred_apply_write_barrier(void)
{
	/*
	 * A privilege drop must become visible before the credential change.
	 * This pairs with the read barrier in __ptrace_may_access().
	 */
	smp_wmb();
}

void rust_helper_cred_key_fsuid_changed(struct cred *cred)
{
	key_fsuid_changed(cred);
}

void rust_helper_cred_key_fsgid_changed(struct cred *cred)
{
	key_fsgid_changed(cred);
}

void rust_helper_cred_proc_uid_changed(struct task_struct *task)
{
	proc_id_connector(task, PROC_EVENT_UID);
}

void rust_helper_cred_proc_gid_changed(struct task_struct *task)
{
	proc_id_connector(task, PROC_EVENT_GID);
}

void rust_helper_cred_switch_namespaces(const struct cred *old,
					const struct cred *new)
{
	switch_cred_namespaces(old, new);
}

void rust_helper_cred_exit_keys(struct task_struct *task)
{
#ifdef CONFIG_KEYS_REQUEST_CACHE
	key_put(task->cached_requested_key);
	task->cached_requested_key = NULL;
#endif
}

void rust_helper_cred_get_namespaces(struct task_struct *task)
{
	get_cred_namespaces(task);
}

void rust_helper_cred_inc_task_nproc(struct task_struct *task)
{
	inc_rlimit_ucounts(task_ucounts(task), UCOUNT_RLIMIT_NPROC, 1);
}

int rust_helper_cred_create_user_ns(struct cred *new)
{
	return create_user_ns(new);
}

int rust_helper_cred_install_thread_keyring(struct cred *new)
{
#ifdef CONFIG_KEYS
	return install_thread_keyring_to_cred(new);
#else
	return 0;
#endif
}

#endif /* CONFIG_RUST_CRED_LIFECYCLE */

#ifndef CONFIG_RUST_CRED_LIFECYCLE

/*
 * The RCU callback to actually dispose of a set of credentials
 */
static void put_cred_rcu(struct rcu_head *rcu)
{
	struct cred *cred = container_of(rcu, struct cred, rcu);

	kdebug("put_cred_rcu(%p)", cred);

	if (atomic_long_read(&cred->usage) != 0)
		panic("CRED: put_cred_rcu() sees %p with usage %ld\n",
		      cred, atomic_long_read(&cred->usage));

	security_cred_free(cred);
	key_put(cred->session_keyring);
	key_put(cred->process_keyring);
	key_put(cred->thread_keyring);
	key_put(cred->request_key_auth);
	if (cred->group_info)
		put_group_info(cred->group_info);
	free_uid(cred->user);
	if (cred->ucounts)
		put_ucounts(cred->ucounts);
	put_user_ns(cred->user_ns);
	kmem_cache_free(cred_jar, cred);
}

/**
 * __put_cred - Destroy a set of credentials
 * @cred: The record to release
 *
 * Destroy a set of credentials on which no references remain.
 */
void __put_cred(struct cred *cred)
{
	kdebug("__put_cred(%p{%ld})", cred,
	       atomic_long_read(&cred->usage));

	BUG_ON(atomic_long_read(&cred->usage) != 0);
	BUG_ON(cred == current->cred);
	BUG_ON(cred == current->real_cred);

	if (cred->non_rcu)
		put_cred_rcu(&cred->rcu);
	else
		call_rcu(&cred->rcu, put_cred_rcu);
}
EXPORT_SYMBOL(__put_cred);

/*
 * Clean up a task's credentials when it exits
 */
void exit_creds(struct task_struct *tsk)
{
	struct cred *real_cred, *cred;

	kdebug("exit_creds(%u,%p,%p,{%ld})", tsk->pid, tsk->real_cred, tsk->cred,
	       atomic_long_read(&tsk->cred->usage));

	real_cred = (struct cred *) tsk->real_cred;
	tsk->real_cred = NULL;

	cred = (struct cred *) tsk->cred;
	tsk->cred = NULL;

	if (real_cred == cred) {
		put_cred_many(cred, 2);
	} else {
		put_cred(real_cred);
		put_cred(cred);
	}

#ifdef CONFIG_KEYS_REQUEST_CACHE
	key_put(tsk->cached_requested_key);
	tsk->cached_requested_key = NULL;
#endif
}

/**
 * get_task_cred - Get another task's objective credentials
 * @task: The task to query
 *
 * Get the objective credentials of a task, pinning them so that they can't go
 * away.  Accessing a task's credentials directly is not permitted.
 *
 * The caller must also make sure task doesn't get deleted, either by holding a
 * ref on task or by holding tasklist_lock to prevent it from being unlinked.
 */
const struct cred *get_task_cred(struct task_struct *task)
{
	const struct cred *cred;

	rcu_read_lock();

	do {
		cred = __task_cred((task));
		BUG_ON(!cred);
	} while (!get_cred_rcu(cred));

	rcu_read_unlock();
	return cred;
}
EXPORT_SYMBOL(get_task_cred);

/*
 * Allocate blank credentials, such that the credentials can be filled in at a
 * later date without risk of ENOMEM.
 */
struct cred *cred_alloc_blank(void)
{
	struct cred *new;

	new = kmem_cache_zalloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	atomic_long_set(&new->usage, 1);
	if (security_cred_alloc_blank(new, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	return new;

error:
	abort_creds(new);
	return NULL;
}

/**
 * prepare_creds - Prepare a new set of credentials for modification
 *
 * Prepare a new set of task credentials for modification.  A task's creds
 * shouldn't generally be modified directly, therefore this function is used to
 * prepare a new copy, which the caller then modifies and then commits by
 * calling commit_creds().
 *
 * Preparation involves making a copy of the objective creds for modification.
 *
 * Returns a pointer to the new creds-to-be if successful, NULL otherwise.
 *
 * Call commit_creds() or abort_creds() to clean up.
 */
struct cred *prepare_creds(void)
{
	struct task_struct *task = current;
	const struct cred *old;
	struct cred *new;

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	kdebug("prepare_creds() alloc %p", new);

	old = task->cred;
	memcpy(new, old, sizeof(struct cred));

	new->non_rcu = 0;
	atomic_long_set(&new->usage, 1);
	get_group_info(new->group_info);
	get_uid(new->user);
	get_user_ns(new->user_ns);

#ifdef CONFIG_KEYS
	key_get(new->session_keyring);
	key_get(new->process_keyring);
	key_get(new->thread_keyring);
	key_get(new->request_key_auth);
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif

	new->ucounts = get_ucounts(new->ucounts);
	if (!new->ucounts)
		goto error;

	if (security_prepare_creds(new, old, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	return new;

error:
	abort_creds(new);
	return NULL;
}
EXPORT_SYMBOL(prepare_creds);

/*
 * Prepare credentials for current to perform an execve()
 * - The caller must hold ->cred_guard_mutex
 */
struct cred *prepare_exec_creds(void)
{
	struct cred *new;

	new = prepare_creds();
	if (!new)
		return new;

#ifdef CONFIG_KEYS
	/* newly exec'd tasks don't get a thread keyring */
	key_put(new->thread_keyring);
	new->thread_keyring = NULL;

	/* inherit the session keyring; new process keyring */
	key_put(new->process_keyring);
	new->process_keyring = NULL;
#endif

	new->suid = new->fsuid = new->euid;
	new->sgid = new->fsgid = new->egid;

	return new;
}

/*
 * Copy credentials for the new process created by fork()
 *
 * We share if we can, but under some circumstances we have to generate a new
 * set.
 *
 * The new process gets the current process's subjective credentials as its
 * objective and subjective credentials
 */
int copy_creds(struct task_struct *p, u64 clone_flags)
{
	struct cred *new;
	int ret;

#ifdef CONFIG_KEYS_REQUEST_CACHE
	p->cached_requested_key = NULL;
#endif

	if (
#ifdef CONFIG_KEYS
		!p->cred->thread_keyring &&
#endif
		clone_flags & CLONE_THREAD
	    ) {
		p->real_cred = get_cred_many(p->cred, 2);
		kdebug("share_creds(%p{%ld})",
		       p->cred, atomic_long_read(&p->cred->usage));
		inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
		get_cred_namespaces(p);
		return 0;
	}

	new = prepare_creds();
	if (!new)
		return -ENOMEM;

	if (clone_flags & CLONE_NEWUSER) {
		ret = create_user_ns(new);
		if (ret < 0)
			goto error_put;
		ret = set_cred_ucounts(new);
		if (ret < 0)
			goto error_put;
	}

#ifdef CONFIG_KEYS
	/* new threads get their own thread keyrings if their parent already
	 * had one */
	if (new->thread_keyring) {
		key_put(new->thread_keyring);
		new->thread_keyring = NULL;
		if (clone_flags & CLONE_THREAD)
			install_thread_keyring_to_cred(new);
	}

	/* The process keyring is only shared between the threads in a process;
	 * anything outside of those threads doesn't inherit.
	 */
	if (!(clone_flags & CLONE_THREAD)) {
		key_put(new->process_keyring);
		new->process_keyring = NULL;
	}
#endif

	p->cred = p->real_cred = get_cred(new);
	inc_rlimit_ucounts(task_ucounts(p), UCOUNT_RLIMIT_NPROC, 1);
	get_cred_namespaces(p);

	return 0;

error_put:
	put_cred(new);
	return ret;
}

static bool cred_cap_issubset(const struct cred *set, const struct cred *subset)
{
	const struct user_namespace *set_ns = set->user_ns;
	const struct user_namespace *subset_ns = subset->user_ns;

	/* If the two credentials are in the same user namespace see if
	 * the capabilities of subset are a subset of set.
	 */
	if (set_ns == subset_ns)
		return cap_issubset(subset->cap_permitted, set->cap_permitted);

	/* The credentials are in a different user namespaces
	 * therefore one is a subset of the other only if a set is an
	 * ancestor of subset and set->euid is owner of subset or one
	 * of subsets ancestors.
	 */
	for (;subset_ns != &init_user_ns; subset_ns = subset_ns->parent) {
		if ((set_ns == subset_ns->parent)  &&
		    uid_eq(subset_ns->owner, set->euid))
			return true;
	}

	return false;
}

/**
 * commit_creds - Install new credentials upon the current task
 * @new: The credentials to be assigned
 *
 * Install a new set of credentials to the current task, using RCU to replace
 * the old set.  Both the objective and the subjective credentials pointers are
 * updated.  This function may not be called if the subjective credentials are
 * in an overridden state.
 *
 * This function eats the caller's reference to the new credentials.
 *
 * Always returns 0 thus allowing this function to be tail-called at the end
 * of, say, sys_setgid().
 */
int commit_creds(struct cred *new)
{
	struct task_struct *task = current;
	const struct cred *old = task->real_cred;

	kdebug("commit_creds(%p{%ld})", new,
	       atomic_long_read(&new->usage));

	BUG_ON(task->cred != old);
	BUG_ON(atomic_long_read(&new->usage) < 1);

	get_cred(new); /* we will require a ref for the subj creds too */

	/* dumpability changes */
	if (!uid_eq(old->euid, new->euid) ||
	    !gid_eq(old->egid, new->egid) ||
	    !uid_eq(old->fsuid, new->fsuid) ||
	    !gid_eq(old->fsgid, new->fsgid) ||
	    !cred_cap_issubset(old, new)) {
		/* mm-less tasks share init_task's exec_state */
		if (task->mm)
			task_exec_state_set_dumpable(suid_dumpable);
		task->pdeath_signal = 0;
		/*
		 * If a task drops privileges and becomes nondumpable,
		 * the dumpability change must become visible before
		 * the credential change; otherwise, a __ptrace_may_access()
		 * racing with this change may be able to attach to a task it
		 * shouldn't be able to attach to (as if the task had dropped
		 * privileges without becoming nondumpable).
		 * Pairs with a read barrier in __ptrace_may_access().
		 */
		smp_wmb();
	}

	/* alter the thread keyring */
	if (!uid_eq(new->fsuid, old->fsuid))
		key_fsuid_changed(new);
	if (!gid_eq(new->fsgid, old->fsgid))
		key_fsgid_changed(new);

	/* do it
	 * RLIMIT_NPROC limits on user->processes have already been checked
	 * in set_user().
	 */
	if (new->user != old->user || new->user_ns != old->user_ns)
		inc_rlimit_ucounts(new->ucounts, UCOUNT_RLIMIT_NPROC, 1);

	rcu_assign_pointer(task->real_cred, new);
	rcu_assign_pointer(task->cred, new);
	if (new->user != old->user || new->user_ns != old->user_ns)
		dec_rlimit_ucounts(old->ucounts, UCOUNT_RLIMIT_NPROC, 1);
	if (new->user_ns != old->user_ns)
		switch_cred_namespaces(old, new);

	/* send notifications */
	if (!uid_eq(new->uid,   old->uid)  ||
	    !uid_eq(new->euid,  old->euid) ||
	    !uid_eq(new->suid,  old->suid) ||
	    !uid_eq(new->fsuid, old->fsuid))
		proc_id_connector(task, PROC_EVENT_UID);

	if (!gid_eq(new->gid,   old->gid)  ||
	    !gid_eq(new->egid,  old->egid) ||
	    !gid_eq(new->sgid,  old->sgid) ||
	    !gid_eq(new->fsgid, old->fsgid))
		proc_id_connector(task, PROC_EVENT_GID);

	/* release the old obj and subj refs both */
	put_cred_many(old, 2);
	return 0;
}
EXPORT_SYMBOL(commit_creds);

/**
 * abort_creds - Discard a set of credentials and unlock the current task
 * @new: The credentials that were going to be applied
 *
 * Discard a set of credentials that were under construction and unlock the
 * current task.
 */
void abort_creds(struct cred *new)
{
	kdebug("abort_creds(%p{%ld})", new,
	       atomic_long_read(&new->usage));

	BUG_ON(atomic_long_read(&new->usage) < 1);
	put_cred(new);
}
EXPORT_SYMBOL(abort_creds);

/**
 * cred_fscmp - Compare two credentials with respect to filesystem access.
 * @a: The first credential
 * @b: The second credential
 *
 * cred_cmp() will return zero if both credentials have the same
 * fsuid, fsgid, and supplementary groups.  That is, if they will both
 * provide the same access to files based on mode/uid/gid.
 * If the credentials are different, then either -1 or 1 will
 * be returned depending on whether @a comes before or after @b
 * respectively in an arbitrary, but stable, ordering of credentials.
 *
 * Return: -1, 0, or 1 depending on comparison
 */
int cred_fscmp(const struct cred *a, const struct cred *b)
{
	struct group_info *ga, *gb;
	int g;

	if (a == b)
		return 0;
	if (uid_lt(a->fsuid, b->fsuid))
		return -1;
	if (uid_gt(a->fsuid, b->fsuid))
		return 1;

	if (gid_lt(a->fsgid, b->fsgid))
		return -1;
	if (gid_gt(a->fsgid, b->fsgid))
		return 1;

	ga = a->group_info;
	gb = b->group_info;
	if (ga == gb)
		return 0;
	if (ga == NULL)
		return -1;
	if (gb == NULL)
		return 1;
	if (ga->ngroups < gb->ngroups)
		return -1;
	if (ga->ngroups > gb->ngroups)
		return 1;

	for (g = 0; g < ga->ngroups; g++) {
		if (gid_lt(ga->gid[g], gb->gid[g]))
			return -1;
		if (gid_gt(ga->gid[g], gb->gid[g]))
			return 1;
	}
	return 0;
}
EXPORT_SYMBOL(cred_fscmp);

int set_cred_ucounts(struct cred *new)
{
	struct ucounts *new_ucounts, *old_ucounts = new->ucounts;

	/*
	 * This optimization is needed because alloc_ucounts() uses locks
	 * for table lookups.
	 */
	if (old_ucounts->ns == new->user_ns && uid_eq(old_ucounts->uid, new->uid))
		return 0;

	if (!(new_ucounts = alloc_ucounts(new->user_ns, new->uid)))
		return -EAGAIN;

	new->ucounts = new_ucounts;
	put_ucounts(old_ucounts);

	return 0;
}

#endif /* !CONFIG_RUST_CRED_LIFECYCLE */

/*
 * initialise the credentials stuff
 */
void __init cred_init(void)
{
	/* allocate a slab in which we can store credentials */
	cred_jar = KMEM_CACHE(cred,
			      SLAB_HWCACHE_ALIGN | SLAB_PANIC | SLAB_ACCOUNT);
}

#ifndef CONFIG_RUST_CRED_LIFECYCLE

/**
 * prepare_kernel_cred - Prepare a set of credentials for a kernel service
 * @daemon: A userspace daemon to be used as a reference
 *
 * Prepare a set of credentials for a kernel service.  This can then be used to
 * override a task's own credentials so that work can be done on behalf of that
 * task that requires a different subjective context.
 *
 * @daemon is used to provide a base cred, with the security data derived from
 * that; if this is "&init_task", they'll be set to 0, no groups, full
 * capabilities, and no keys.
 *
 * The caller may change these controls afterwards if desired.
 *
 * Returns the new credentials or NULL if out of memory.
 */
struct cred *prepare_kernel_cred(struct task_struct *daemon)
{
	const struct cred *old;
	struct cred *new;

	if (WARN_ON_ONCE(!daemon))
		return NULL;

	new = kmem_cache_alloc(cred_jar, GFP_KERNEL);
	if (!new)
		return NULL;

	kdebug("prepare_kernel_cred() alloc %p", new);

	old = get_task_cred(daemon);

	*new = *old;
	new->non_rcu = 0;
	atomic_long_set(&new->usage, 1);
	get_uid(new->user);
	get_user_ns(new->user_ns);
	get_group_info(new->group_info);

#ifdef CONFIG_KEYS
	new->session_keyring = NULL;
	new->process_keyring = NULL;
	new->thread_keyring = NULL;
	new->request_key_auth = NULL;
	new->jit_keyring = KEY_REQKEY_DEFL_THREAD_KEYRING;
#endif

#ifdef CONFIG_SECURITY
	new->security = NULL;
#endif
	new->ucounts = get_ucounts(new->ucounts);
	if (!new->ucounts)
		goto error;

	if (security_prepare_creds(new, old, GFP_KERNEL_ACCOUNT) < 0)
		goto error;

	put_cred(old);
	return new;

error:
	put_cred(new);
	put_cred(old);
	return NULL;
}
EXPORT_SYMBOL(prepare_kernel_cred);

/**
 * set_security_override - Set the security ID in a set of credentials
 * @new: The credentials to alter
 * @secid: The LSM security ID to set
 *
 * Set the LSM security ID in a set of credentials so that the subjective
 * security is overridden when an alternative set of credentials is used.
 */
int set_security_override(struct cred *new, u32 secid)
{
	return security_kernel_act_as(new, secid);
}
EXPORT_SYMBOL(set_security_override);

/**
 * set_create_files_as - Set the LSM file create context in a set of credentials
 * @new: The credentials to alter
 * @inode: The inode to take the context from
 *
 * Change the LSM file creation context in a set of credentials to be the same
 * as the object context of the specified inode, so that the new inodes have
 * the same MAC context as that inode.
 */
int set_create_files_as(struct cred *new, struct inode *inode)
{
	if (!uid_valid(inode->i_uid) || !gid_valid(inode->i_gid))
		return -EINVAL;
	new->fsuid = inode->i_uid;
	new->fsgid = inode->i_gid;
	return security_kernel_create_files_as(new, inode);
}
EXPORT_SYMBOL(set_create_files_as);

#else /* CONFIG_RUST_CRED_LIFECYCLE */

/*
 * Rust defines these functions. This object contains only ordinary export
 * metadata so loadable modules can keep using the public credential API.
 */
#define EXPORT_CRED_ABI(symbol) EXPORT_SYMBOL(symbol)
EXPORT_CRED_ABI(__put_cred);
EXPORT_CRED_ABI(get_task_cred);
EXPORT_CRED_ABI(prepare_creds);
EXPORT_CRED_ABI(commit_creds);
EXPORT_CRED_ABI(abort_creds);
EXPORT_CRED_ABI(cred_fscmp);
EXPORT_CRED_ABI(prepare_kernel_cred);
EXPORT_CRED_ABI(set_security_override);
EXPORT_CRED_ABI(set_create_files_as);
#undef EXPORT_CRED_ABI

#endif /* CONFIG_RUST_CRED_LIFECYCLE */
