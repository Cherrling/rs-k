// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/capability.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/nsfs.h>
#include <linux/nsproxy.h>
#include <linux/nsproxy-rust.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/user_namespace.h>

#define NSPROXY_LIFECYCLE_TEST_TIMEOUT		(5 * HZ)
#define NSPROXY_LIFECYCLE_TEST_WORKERS		4
#define NSPROXY_LIFECYCLE_TEST_CREATE_LOOPS	32

struct nsproxy_wait_context {
	struct completion started;
};

struct nsproxy_switch_context {
	struct completion done;
	bool valid;
};

struct nsproxy_create_context {
	struct completion started;
	struct completion done;
	atomic_t errors;
};

struct nsproxy_exec_context {
	struct completion done;
	bool valid;
};

static bool nsproxy_test_wait(struct completion *completion)
{
	return wait_for_completion_timeout(completion,
					   NSPROXY_LIFECYCLE_TEST_TIMEOUT);
}

static int nsproxy_test_wait_worker(void *data)
{
	struct nsproxy_wait_context *context = data;

	complete(&context->started);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void nsproxy_lifecycle_test_create_free(struct kunit *test)
{
	struct nsproxy *current_ns = current->nsproxy;
	struct nsproxy *ns;

	KUNIT_ASSERT_NOT_NULL(test, current_ns);
	ns = nsproxy_test_create();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ns));

	KUNIT_EXPECT_EQ(test, 1, refcount_read(&ns->count));
	KUNIT_EXPECT_PTR_EQ(test, ns->mnt_ns, current_ns->mnt_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->uts_ns, current_ns->uts_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->ipc_ns, current_ns->ipc_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->pid_ns_for_children,
			    current_ns->pid_ns_for_children);
	KUNIT_EXPECT_PTR_EQ(test, ns->cgroup_ns, current_ns->cgroup_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->net_ns, current_ns->net_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->time_ns, current_ns->time_ns);
	KUNIT_EXPECT_PTR_EQ(test, ns->time_ns_for_children,
			    current_ns->time_ns_for_children);

	nsproxy_free(ns);
}

static void nsproxy_lifecycle_test_final_release(struct kunit *test)
{
	struct nsproxy *ns;
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	u32 free_count = rust_nsproxy_test_free_count();
	u32 release_count = rust_nsproxy_test_release_count();
#endif

	ns = nsproxy_test_create();
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(ns));

	nsproxy_ns_active_get(ns);
	put_nsproxy(ns);

#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_nsproxy_test_free_count(), free_count);
	KUNIT_EXPECT_GT(test, rust_nsproxy_test_release_count(), release_count);
#endif
}

static int nsproxy_lifecycle_test_switch_worker(void *data)
{
	struct nsproxy_switch_context *context = data;
	struct nsproxy *old;
	struct nsproxy *new;
	bool switched;

	old = current->nsproxy;
	if (!old) {
		context->valid = false;
		complete(&context->done);
		return 0;
	}
	get_nsproxy(old);

	new = nsproxy_test_create();
	if (IS_ERR_OR_NULL(new)) {
		put_nsproxy(old);
		context->valid = false;
		complete(&context->done);
		return 0;
	}

	switch_task_namespaces(current, new);
	switched = current->nsproxy == new;
	switch_task_namespaces(current, old);
	context->valid = switched && current->nsproxy == old;
	put_nsproxy(old);

	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void nsproxy_lifecycle_test_switch(struct kunit *test)
{
	struct nsproxy_switch_context context = {};
	struct task_struct *task;
	bool done;

	init_completion(&context.done);
	task = kthread_run(nsproxy_lifecycle_test_switch_worker, &context,
			   "nsproxy-switch");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	get_task_struct(task);

	done = nsproxy_test_wait(&context.done);
	KUNIT_EXPECT_TRUE(test, done);
	KUNIT_EXPECT_TRUE(test, context.valid);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
}

static void nsproxy_lifecycle_test_fork_exit(struct kunit *test)
{
	struct nsproxy_wait_context contexts[NSPROXY_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *tasks[NSPROXY_LIFECYCLE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	u32 copy_count = rust_nsproxy_test_copy_count();
	u32 switch_count = rust_nsproxy_test_switch_count();
#endif

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		init_completion(&contexts[index].started);
		tasks[index] = kthread_run(nsproxy_test_wait_worker,
					   &contexts[index],
					   "nsproxy-fork-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		if (!nsproxy_test_wait(&contexts[index].started))
			timed_out = true;
	}
	for (index = 0; index < ARRAY_SIZE(tasks); index++)
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));

	KUNIT_EXPECT_FALSE(test, timed_out);
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	KUNIT_EXPECT_GE(test, rust_nsproxy_test_copy_count() - copy_count,
			NSPROXY_LIFECYCLE_TEST_WORKERS);
	KUNIT_EXPECT_GT(test, rust_nsproxy_test_switch_count(), switch_count);
#endif
}

static int nsproxy_lifecycle_test_create_worker(void *data)
{
	struct nsproxy_create_context *context = data;
	unsigned int index;

	complete(&context->started);
	for (index = 0; index < NSPROXY_LIFECYCLE_TEST_CREATE_LOOPS; index++) {
		struct nsproxy *ns = nsproxy_test_create();

		if (IS_ERR_OR_NULL(ns)) {
			atomic_inc(&context->errors);
			break;
		}
		nsproxy_free(ns);
		cond_resched();
	}
	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void nsproxy_lifecycle_test_concurrent_create(struct kunit *test)
{
	struct nsproxy_create_context contexts[NSPROXY_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *tasks[NSPROXY_LIFECYCLE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	u32 create_count = rust_nsproxy_test_create_count();
#endif

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		init_completion(&contexts[index].started);
		init_completion(&contexts[index].done);
		atomic_set(&contexts[index].errors, 0);
		tasks[index] = kthread_run(nsproxy_lifecycle_test_create_worker,
					   &contexts[index],
					   "nsproxy-create-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		if (!nsproxy_test_wait(&contexts[index].started))
			timed_out = true;
		if (!nsproxy_test_wait(&contexts[index].done))
			timed_out = true;
	}
	for (index = 0; index < ARRAY_SIZE(tasks); index++)
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));

	KUNIT_EXPECT_FALSE(test, timed_out);
	for (index = 0; index < ARRAY_SIZE(contexts); index++)
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	KUNIT_EXPECT_GE(test, rust_nsproxy_test_create_count() - create_count,
			NSPROXY_LIFECYCLE_TEST_WORKERS *
			NSPROXY_LIFECYCLE_TEST_CREATE_LOOPS);
#endif
}

static void nsproxy_lifecycle_test_unshare(struct kunit *test)
{
	struct nsproxy *sentinel = ERR_PTR(-EINVAL);
	struct nsproxy *new = sentinel;
	int ret;

	ret = unshare_nsproxy_namespaces(0, &new, NULL, NULL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_PTR_EQ(test, sentinel, new);

	if (!capable(CAP_SYS_ADMIN))
		kunit_skip(test, "namespace creation needs CAP_SYS_ADMIN");

	new = NULL;
	ret = unshare_nsproxy_namespaces(CLONE_NEWNS, &new, NULL, NULL);
	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(new));
	nsproxy_free(new);
}

static void nsproxy_lifecycle_test_cred(struct kunit *test)
{
	const struct cred *cred = current->real_cred;

	KUNIT_ASSERT_PTR_EQ(test, cred, current->cred);
	get_cred_namespaces(current);
	switch_cred_namespaces(cred, cred);
	exit_cred_namespaces(current);
}

static int nsproxy_lifecycle_test_exec_worker(void *data)
{
	struct nsproxy_exec_context *context = data;
	struct nsproxy *pending = NULL;
	struct mm_struct *mm;
	struct nsproxy *old;
	struct nsproxy *ns;
	int ret;

	old = current->nsproxy;
	if (!old)
		goto out;

	mm = mm_alloc();
	if (!mm)
		goto out;
	kthread_use_mm(mm);

	get_nsproxy(old);
	ret = unshare_nsproxy_namespaces(CLONE_NEWTIME, &pending, NULL, NULL);
	if (ret || IS_ERR_OR_NULL(pending)) {
		if (!IS_ERR_OR_NULL(pending))
			put_nsproxy(pending);
		put_nsproxy(old);
		goto out_unuse_mm;
	}

	switch_task_namespaces(current, pending);
	pending = NULL;

	ns = current->nsproxy;
	context->valid = ns && ns->time_ns_for_children != ns->time_ns;

	ret = exec_task_namespaces();
	ns = current->nsproxy;
	context->valid = context->valid && ret == 0 && ns &&
			 ns->time_ns_for_children == ns->time_ns;

	switch_task_namespaces(current, old);
	put_nsproxy(old);
out_unuse_mm:
	kthread_unuse_mm(mm);
	mmput(mm);
out:
	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void nsproxy_lifecycle_test_exec(struct kunit *test)
{
	struct nsproxy *ns = current->nsproxy;

	KUNIT_ASSERT_NOT_NULL(test, ns);
	if (ns->time_ns_for_children != ns->time_ns)
		kunit_skip(test, "current task has a pending time namespace");

	KUNIT_EXPECT_EQ(test, 0, exec_task_namespaces());
	KUNIT_EXPECT_PTR_EQ(test, current->nsproxy, ns);
}

static void nsproxy_lifecycle_test_exec_transition(struct kunit *test)
{
	struct nsproxy_exec_context context = {};
	struct task_struct *task;
	bool done;

	if (!capable(CAP_SYS_ADMIN))
		kunit_skip(test, "time namespace creation needs CAP_SYS_ADMIN");

	init_completion(&context.done);
	task = kthread_run(nsproxy_lifecycle_test_exec_worker, &context,
			   "nsproxy-exec");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	get_task_struct(task);

	done = nsproxy_test_wait(&context.done);
	KUNIT_EXPECT_TRUE(test, done);
	KUNIT_EXPECT_TRUE(test, context.valid);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
}

#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
static void nsproxy_lifecycle_test_failure_injection(struct kunit *test)
{
	static const u32 stages[] = {
		RUST_NSPROXY_TEST_FAIL_ALLOC,
		RUST_NSPROXY_TEST_FAIL_MNT,
		RUST_NSPROXY_TEST_FAIL_UTS,
		RUST_NSPROXY_TEST_FAIL_IPC,
		RUST_NSPROXY_TEST_FAIL_PID,
		RUST_NSPROXY_TEST_FAIL_CGROUP,
		RUST_NSPROXY_TEST_FAIL_NET,
		RUST_NSPROXY_TEST_FAIL_TIME,
	};
	u32 failures = rust_nsproxy_test_failure_count();
	u32 releases = rust_nsproxy_test_release_count();
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(stages); index++) {
		struct nsproxy *ns;

		rust_nsproxy_test_fail_next(stages[index]);
		ns = nsproxy_test_create();
		KUNIT_EXPECT_TRUE(test, IS_ERR(ns));
		KUNIT_EXPECT_EQ(test, -ENOMEM, PTR_ERR(ns));
		if (!IS_ERR_OR_NULL(ns))
			nsproxy_free(ns);
	}

	rust_nsproxy_test_fail_next(0);
	KUNIT_EXPECT_EQ(test, failures + ARRAY_SIZE(stages),
			rust_nsproxy_test_failure_count());
	KUNIT_EXPECT_GT(test, rust_nsproxy_test_release_count(), releases);
}
#endif

static struct kunit_case nsproxy_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_NSPROXY_LIFECYCLE
	KUNIT_CASE(nsproxy_lifecycle_test_failure_injection),
#endif
	KUNIT_CASE(nsproxy_lifecycle_test_create_free),
	KUNIT_CASE(nsproxy_lifecycle_test_final_release),
	KUNIT_CASE(nsproxy_lifecycle_test_switch),
	KUNIT_CASE(nsproxy_lifecycle_test_fork_exit),
	KUNIT_CASE(nsproxy_lifecycle_test_concurrent_create),
	KUNIT_CASE(nsproxy_lifecycle_test_unshare),
	KUNIT_CASE(nsproxy_lifecycle_test_cred),
	KUNIT_CASE(nsproxy_lifecycle_test_exec),
	KUNIT_CASE(nsproxy_lifecycle_test_exec_transition),
	{}
};

static struct kunit_suite nsproxy_lifecycle_test_suite = {
	.name = "nsproxy_lifecycle",
	.test_cases = nsproxy_lifecycle_test_cases,
};

kunit_test_suite(nsproxy_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for namespace proxy lifecycle");
MODULE_LICENSE("GPL");
