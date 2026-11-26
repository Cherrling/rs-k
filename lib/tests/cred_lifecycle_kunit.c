// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/cred.h>
#include <linux/cred-rust.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init_task.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/user.h>
#include <linux/user_namespace.h>

#define CRED_LIFECYCLE_TEST_TIMEOUT	(5 * HZ)
#define CRED_LIFECYCLE_TEST_PREPARE_WORKERS	2
#define CRED_LIFECYCLE_TEST_READER_WORKERS	2
#define CRED_LIFECYCLE_TEST_WORKERS		\
	(CRED_LIFECYCLE_TEST_PREPARE_WORKERS + \
	 CRED_LIFECYCLE_TEST_READER_WORKERS + 1)
#define CRED_LIFECYCLE_TEST_PREPARE_LOOPS	128
#define CRED_LIFECYCLE_TEST_READER_LOOPS		256
#define CRED_LIFECYCLE_TEST_COMMIT_LOOPS		128
#define CRED_LIFECYCLE_TEST_FORK_ROUNDS		8

static bool cred_lifecycle_test_wait(struct completion *completion)
{
	return wait_for_completion_timeout(completion,
					   CRED_LIFECYCLE_TEST_TIMEOUT);
}

struct cred_lifecycle_concurrency {
	struct completion started;
	struct completion go;
	struct completion done;
	struct task_struct *target;
	atomic_t errors;
};

struct cred_lifecycle_thread_context {
	struct completion leader_started;
	struct completion child_started;
	const struct cred *leader_cred;
	const struct cred *child_cred;
	const struct cred *child_real_cred;
	pid_t child_pid;
};

static void cred_lifecycle_test_blank(struct kunit *test)
{
	struct cred *cred;

	cred = cred_alloc_blank();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	abort_creds(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_prepare_abort(struct kunit *test)
{
	struct cred *cred;

	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	KUNIT_EXPECT_EQ(test, 1, atomic_long_read(&cred->usage));
	abort_creds(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_ucounts(struct kunit *test)
{
	struct ucounts *old;
	struct cred *cred;

	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	old = cred->ucounts;
	KUNIT_ASSERT_NOT_NULL(test, old);

	cred->uid.val++;
	KUNIT_EXPECT_EQ(test, 0, set_cred_ucounts(cred));
	KUNIT_ASSERT_NOT_NULL(test, cred->ucounts);
	KUNIT_EXPECT_PTR_NE(test, old, cred->ucounts);
	KUNIT_EXPECT_PTR_EQ(test, cred->user_ns, cred->ucounts->ns);
	KUNIT_EXPECT_EQ(test, cred->uid.val, cred->ucounts->uid.val);

	abort_creds(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_prepare_exec(struct kunit *test)
{
	struct cred *cred;

	cred = prepare_exec_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	KUNIT_EXPECT_TRUE(test, uid_eq(cred->suid, cred->euid));
	KUNIT_EXPECT_TRUE(test, uid_eq(cred->fsuid, cred->euid));
	KUNIT_EXPECT_TRUE(test, gid_eq(cred->sgid, cred->egid));
	KUNIT_EXPECT_TRUE(test, gid_eq(cred->fsgid, cred->egid));
	abort_creds(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_kernel(struct kunit *test)
{
	struct cred *cred;

	cred = prepare_kernel_cred(current);
	KUNIT_ASSERT_NOT_NULL(test, cred);
	KUNIT_EXPECT_EQ(test, 1, atomic_long_read(&cred->usage));
	abort_creds(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_get_task(struct kunit *test)
{
	const struct cred *cred;

	cred = get_task_cred(current);
	KUNIT_ASSERT_NOT_NULL(test, cred);
	put_cred(cred);
	rcu_barrier();
}

static void cred_lifecycle_test_commit_identical(struct kunit *test)
{
	struct cred *cred;
	const struct cred *before;

	before = current_cred();
	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	KUNIT_EXPECT_EQ(test, 0, commit_creds(cred));
	KUNIT_EXPECT_PTR_NE(test, before, current_cred());
	rcu_barrier();
}

static void cred_lifecycle_test_fscmp(struct kunit *test)
{
	struct group_info *left_groups;
	struct group_info *right_groups;
	struct cred group_left = {};
	struct cred group_right = {};
	struct cred *left;
	struct cred *right;

	left = prepare_creds();
	right = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, left);
	KUNIT_ASSERT_NOT_NULL(test, right);

	KUNIT_EXPECT_EQ(test, 0, cred_fscmp(left, left));
	KUNIT_EXPECT_EQ(test, 0, cred_fscmp(left, right));

	right->fsuid.val = left->fsuid.val + 1;
	KUNIT_EXPECT_EQ(test, -1, cred_fscmp(left, right));
	KUNIT_EXPECT_EQ(test, 1, cred_fscmp(right, left));

	abort_creds(left);
	abort_creds(right);

	left_groups = kunit_kzalloc(test, struct_size(left_groups, gid, 2),
				    GFP_KERNEL);
	right_groups = kunit_kzalloc(test, struct_size(right_groups, gid, 2),
				     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, left_groups);
	KUNIT_ASSERT_NOT_NULL(test, right_groups);

	left_groups->ngroups = 2;
	right_groups->ngroups = 2;
	left_groups->gid[0] = GLOBAL_ROOT_GID;
	right_groups->gid[0] = GLOBAL_ROOT_GID;
	left_groups->gid[1].val = 1000;
	right_groups->gid[1].val = 1000;
	group_left.group_info = left_groups;
	group_right.group_info = right_groups;

	KUNIT_EXPECT_EQ(test, 0, cred_fscmp(&group_left, &group_right));
	right_groups->gid[1].val++;
	KUNIT_EXPECT_EQ(test, -1, cred_fscmp(&group_left, &group_right));
	KUNIT_EXPECT_EQ(test, 1, cred_fscmp(&group_right, &group_left));
	right_groups->gid[1].val--;
	right_groups->ngroups = 1;
	KUNIT_EXPECT_EQ(test, 1, cred_fscmp(&group_left, &group_right));
	group_right.group_info = NULL;
	KUNIT_EXPECT_EQ(test, 1, cred_fscmp(&group_left, &group_right));
	KUNIT_EXPECT_EQ(test, -1, cred_fscmp(&group_right, &group_left));
}

static void cred_lifecycle_test_rcu_release(struct kunit *test)
{
	struct cred *cred;
	const struct cred *held;
#ifdef CONFIG_RUST_CRED_LIFECYCLE
	u32 release_count = rust_cred_test_release_count();
#endif

	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	held = get_cred(cred);
	KUNIT_EXPECT_EQ(test, 2, atomic_long_read(&cred->usage));

	abort_creds(cred);
	KUNIT_EXPECT_EQ(test, 1, atomic_long_read(&held->usage));

	rcu_read_lock();
	KUNIT_EXPECT_EQ(test, held->euid.val, current_cred()->euid.val);
	rcu_read_unlock();

	put_cred(held);
	rcu_barrier();

#ifdef CONFIG_RUST_CRED_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_cred_test_release_count(), release_count);
#endif
}

static void cred_lifecycle_test_direct_release(struct kunit *test)
{
	struct cred *cred;
#ifdef CONFIG_RUST_CRED_LIFECYCLE
	u32 release_count = rust_cred_test_release_count();
#endif

	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);
	cred->non_rcu = 1;
	abort_creds(cred);

#ifdef CONFIG_RUST_CRED_LIFECYCLE
	KUNIT_EXPECT_GT(test, rust_cred_test_release_count(), release_count);
#endif
	rcu_barrier();
}

static void cred_lifecycle_test_invalid_create_files_as(struct kunit *test)
{
	struct inode *inode;
	struct cred *cred;

	inode = kunit_kzalloc(test, sizeof(*inode), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, inode);
	cred = prepare_creds();
	KUNIT_ASSERT_NOT_NULL(test, cred);

	inode->i_uid = INVALID_UID;
	inode->i_gid = GLOBAL_ROOT_GID;
	KUNIT_EXPECT_EQ(test, -EINVAL, set_create_files_as(cred, inode));

	inode->i_uid = GLOBAL_ROOT_UID;
	inode->i_gid = INVALID_GID;
	KUNIT_EXPECT_EQ(test, -EINVAL, set_create_files_as(cred, inode));

	abort_creds(cred);
	rcu_barrier();
}

static int cred_lifecycle_test_prepare_worker(void *data)
{
	struct cred_lifecycle_concurrency *context = data;
	unsigned int index;

	complete(&context->started);
	wait_for_completion(&context->go);

	for (index = 0; index < CRED_LIFECYCLE_TEST_PREPARE_LOOPS; index++) {
		struct cred *cred = prepare_creds();

		if (!cred || atomic_long_read(&cred->usage) != 1) {
			atomic_inc(&context->errors);
			if (cred)
				abort_creds(cred);
			break;
		}
		abort_creds(cred);
		cond_resched();
	}

	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static int cred_lifecycle_test_reader_worker(void *data)
{
	struct cred_lifecycle_concurrency *context = data;
	unsigned int index;

	complete(&context->started);
	wait_for_completion(&context->go);

	for (index = 0; index < CRED_LIFECYCLE_TEST_READER_LOOPS; index++) {
		const struct cred *cred = get_task_cred(context->target);

		if (!cred || atomic_long_read(&cred->usage) < 1) {
			atomic_inc(&context->errors);
			if (cred)
				put_cred(cred);
			break;
		}
		put_cred(cred);
	}

	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static int cred_lifecycle_test_commit_worker(void *data)
{
	struct cred_lifecycle_concurrency *context = data;
	unsigned int index;

	complete(&context->started);
	wait_for_completion(&context->go);

	for (index = 0; index < CRED_LIFECYCLE_TEST_COMMIT_LOOPS; index++) {
		struct cred *cred = prepare_creds();

		if (!cred) {
			atomic_inc(&context->errors);
			break;
		}

		if (index & 1)
			cred->fsuid.val ^= 1;
		else
			cred->fsgid.val ^= 1;
		if (commit_creds(cred)) {
			atomic_inc(&context->errors);
			break;
		}
	}

	complete(&context->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void cred_lifecycle_test_concurrency(struct kunit *test)
{
	struct cred_lifecycle_concurrency contexts[CRED_LIFECYCLE_TEST_WORKERS];
	struct task_struct *tasks[CRED_LIFECYCLE_TEST_WORKERS] = {};
	struct task_struct *target = NULL;
	unsigned int created = 0;
	unsigned int index;
	bool timed_out = false;
#ifdef CONFIG_RUST_CRED_LIFECYCLE
	u32 commit_count = rust_cred_test_commit_count();
	u32 fork_count = rust_cred_test_fork_count();
	u32 prepare_count = rust_cred_test_prepare_count();
#endif

	memset(contexts, 0, sizeof(contexts));
	for (index = 0; index < ARRAY_SIZE(contexts); index++) {
		init_completion(&contexts[index].started);
		init_completion(&contexts[index].go);
		init_completion(&contexts[index].done);
		atomic_set(&contexts[index].errors, 0);
	}

	tasks[0] = kthread_run(cred_lifecycle_test_commit_worker,
			       &contexts[0], "cred-commit");
	if (!IS_ERR_OR_NULL(tasks[0])) {
		get_task_struct(tasks[0]);
		target = tasks[0];
		created++;
	}
	if (!target)
		target = current;

	for (index = 1; index < 1 + CRED_LIFECYCLE_TEST_PREPARE_WORKERS;
	     index++) {
		tasks[index] = kthread_run(cred_lifecycle_test_prepare_worker,
					   &contexts[index], "cred-prepare");
		if (!IS_ERR_OR_NULL(tasks[index])) {
			get_task_struct(tasks[index]);
			created++;
		}
	}

	for (; index < ARRAY_SIZE(tasks); index++) {
		contexts[index].target = target;
		tasks[index] = kthread_run(cred_lifecycle_test_reader_worker,
					   &contexts[index], "cred-reader");
		if (!IS_ERR_OR_NULL(tasks[index])) {
			get_task_struct(tasks[index]);
			created++;
		}
	}

	KUNIT_EXPECT_EQ(test, CRED_LIFECYCLE_TEST_WORKERS, created);
	if (target) {
		for (index = 0; index < ARRAY_SIZE(tasks); index++) {
			if (!IS_ERR_OR_NULL(tasks[index]) &&
			    !cred_lifecycle_test_wait(&contexts[index].started))
				timed_out = true;
		}
	}

	for (index = 0; index < ARRAY_SIZE(contexts); index++)
		complete_all(&contexts[index].go);

	if (target) {
		for (index = 0; index < ARRAY_SIZE(tasks); index++) {
			if (!IS_ERR_OR_NULL(tasks[index]) &&
			    !cred_lifecycle_test_wait(&contexts[index].done))
				timed_out = true;
		}
	}

	for (index = 0; index < ARRAY_SIZE(tasks); index++) {
		if (!IS_ERR_OR_NULL(tasks[index]))
			KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
	}

	KUNIT_EXPECT_FALSE(test, timed_out);
	for (index = 0; index < ARRAY_SIZE(contexts); index++)
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));

	rcu_barrier();

#ifdef CONFIG_RUST_CRED_LIFECYCLE
	KUNIT_EXPECT_GE(test, rust_cred_test_prepare_count(),
			prepare_count + CRED_LIFECYCLE_TEST_PREPARE_LOOPS);
	KUNIT_EXPECT_GT(test, rust_cred_test_fork_count(), fork_count);
	KUNIT_EXPECT_GT(test, rust_cred_test_commit_count(), commit_count);
#endif
}

struct cred_lifecycle_fork_context {
	struct completion started;
	bool valid;
};

static int cred_lifecycle_test_fork_worker(void *data)
{
	struct cred_lifecycle_fork_context *context = data;
	struct cred *cred;
	kuid_t expected;

	cred = prepare_creds();
	if (!cred) {
		context->valid = false;
		complete(&context->started);
	} else {
		expected.val = cred->fsuid.val ^ 1;
		cred->fsuid = expected;
		context->valid = !commit_creds(cred) &&
				 uid_eq(current_cred()->fsuid, expected) &&
				 current->cred == current->real_cred &&
				 atomic_long_read(&current_cred()->usage) >= 2;
		complete(&context->started);
	}

	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void cred_lifecycle_test_fork_exit(struct kunit *test)
{
	unsigned int round;
#ifdef CONFIG_RUST_CRED_LIFECYCLE
	u32 exit_count = rust_cred_test_exit_count();
	u32 fork_count = rust_cred_test_fork_count();
	u32 release_count = rust_cred_test_release_count();
#endif

	for (round = 0; round < CRED_LIFECYCLE_TEST_FORK_ROUNDS; round++) {
		struct cred_lifecycle_fork_context context = {};
		struct task_struct *task;
		bool started;

		init_completion(&context.started);
		task = kthread_run(cred_lifecycle_test_fork_worker, &context,
				   "cred-fork-%u", round);
		if (IS_ERR_OR_NULL(task)) {
			KUNIT_FAIL(test, "failed to create worker %u", round);
			continue;
		}
		get_task_struct(task);
		started = cred_lifecycle_test_wait(&context.started);
		KUNIT_EXPECT_TRUE(test, started);
		KUNIT_EXPECT_TRUE(test, context.valid);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();

#ifdef CONFIG_RUST_CRED_LIFECYCLE
	KUNIT_EXPECT_GE(test,
			rust_cred_test_fork_count() - fork_count,
			CRED_LIFECYCLE_TEST_FORK_ROUNDS);
	KUNIT_EXPECT_GT(test,
			rust_cred_test_exit_count() - exit_count, 0);
	KUNIT_EXPECT_GT(test, rust_cred_test_release_count(), release_count);
#endif
}

static int cred_lifecycle_test_thread_child(void *data)
{
	struct cred_lifecycle_thread_context *context = data;

	context->child_cred = current_cred();
	context->child_real_cred = current->real_cred;
	complete(&context->child_started);
	kthread_exit(0);
}

static int cred_lifecycle_test_thread_leader(void *data)
{
	struct cred_lifecycle_thread_context *context = data;
	unsigned long thread_flags = CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
				     CLONE_FILES | SIGCHLD;

	context->leader_cred = current_cred();
	context->child_pid = kernel_thread(cred_lifecycle_test_thread_child,
					   context, "cred-thread", thread_flags);
	complete(&context->leader_started);
	if (context->child_pid <= 0)
		complete(&context->child_started);

	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void cred_lifecycle_test_thread_copy(struct kunit *test)
{
	struct cred_lifecycle_thread_context context = {};
	struct task_struct *leader;
	bool started;

	init_completion(&context.leader_started);
	init_completion(&context.child_started);

	leader = kthread_run(cred_lifecycle_test_thread_leader, &context,
			     "cred-thread-leader");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, leader);
	get_task_struct(leader);

	started = cred_lifecycle_test_wait(&context.leader_started);
	KUNIT_EXPECT_TRUE(test, started);
	if (started)
		started = cred_lifecycle_test_wait(&context.child_started);
	KUNIT_EXPECT_TRUE(test, started);

	if (context.child_pid > 0) {
		KUNIT_EXPECT_PTR_EQ(test, context.leader_cred,
				    context.child_cred);
		KUNIT_EXPECT_PTR_EQ(test, context.child_cred,
				    context.child_real_cred);
	} else {
		KUNIT_FAIL(test, "thread creation failed: %d",
			   context.child_pid);
	}

	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(leader));
	rcu_barrier();
}

#ifdef CONFIG_RUST_CRED_LIFECYCLE
static int cred_lifecycle_test_wait_worker(void *data)
{
	while (!kthread_should_stop())
		schedule();
	return 0;
}

#ifdef CONFIG_USER_NS
static int cred_lifecycle_test_noop_worker(void *data)
{
	(void)data;
	return 0;
}
#endif

static void cred_lifecycle_test_failure_injection(struct kunit *test)
{
	struct cred *cred;
	struct ucounts *ucounts;
	struct task_struct *task;
	u32 failures = rust_cred_test_failure_count();
	u32 injected = 0;
#ifdef CONFIG_USER_NS
	pid_t pid;
#endif

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_BLANK_ALLOC);
	KUNIT_EXPECT_NULL(test, cred_alloc_blank());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_SECURITY_BLANK);
	KUNIT_EXPECT_NULL(test, cred_alloc_blank());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_CRED_ALLOC);
	KUNIT_EXPECT_NULL(test, prepare_creds());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_CRED_ALLOC);
	KUNIT_EXPECT_NULL(test, prepare_exec_creds());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_CRED_ALLOC);
	KUNIT_EXPECT_NULL(test, prepare_kernel_cred(current));
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_UCOUNTS_GET);
	KUNIT_EXPECT_NULL(test, prepare_creds());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_UCOUNTS_GET);
	KUNIT_EXPECT_NULL(test, prepare_kernel_cred(current));
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_SECURITY_PREPARE);
	KUNIT_EXPECT_NULL(test, prepare_creds());
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_SECURITY_PREPARE);
	KUNIT_EXPECT_NULL(test, prepare_kernel_cred(current));
	injected++;

	cred = prepare_creds();
	if (!cred) {
		rust_cred_test_fail_next(0);
		KUNIT_FAIL(test, "prepare_creds failed without injection");
		return;
	}
	ucounts = cred->ucounts;
	cred->uid.val++;
	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_UCOUNTS_ALLOC);
	KUNIT_EXPECT_EQ(test, -EAGAIN, set_cred_ucounts(cred));
	KUNIT_EXPECT_PTR_EQ(test, ucounts, cred->ucounts);
	abort_creds(cred);
	injected++;

	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_CRED_ALLOC);
	task = kthread_run(cred_lifecycle_test_wait_worker, NULL,
			   "cred-fail-copy");
	KUNIT_EXPECT_TRUE(test, IS_ERR(task));
	if (!IS_ERR_OR_NULL(task)) {
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}
	injected++;

#ifdef CONFIG_USER_NS
	rust_cred_test_fail_next(RUST_CRED_TEST_FAIL_USER_NS);
	pid = kernel_thread(cred_lifecycle_test_noop_worker, NULL,
			    "cred-fail-userns",
			    CLONE_NEWUSER | SIGCHLD);
	KUNIT_EXPECT_EQ(test, -ENOMEM, pid);
	injected++;
#endif

	rust_cred_test_fail_next(0);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, failures + injected,
			rust_cred_test_failure_count());
}
#endif

#ifdef CONFIG_RUST_CRED_LIFECYCLE
static void cred_lifecycle_test_policy(struct kunit *test)
{
	struct cred old = {};
	struct cred new = {};
#ifdef CONFIG_USER_NS
	struct user_namespace *child_ns;
#endif

	old.user_ns = &init_user_ns;
	new.user_ns = &init_user_ns;
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_dumpability(&old, &new));
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_uid(&old, &new));
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_gid(&old, &new));

	new.euid.val = 1;
	KUNIT_EXPECT_TRUE(test, rust_cred_test_changes_dumpability(&old, &new));
	KUNIT_EXPECT_TRUE(test, rust_cred_test_changes_uid(&old, &new));
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_gid(&old, &new));

	new = old;
	new.egid.val = 1;
	KUNIT_EXPECT_TRUE(test, rust_cred_test_changes_dumpability(&old, &new));
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_uid(&old, &new));
	KUNIT_EXPECT_TRUE(test, rust_cred_test_changes_gid(&old, &new));

	new = old;
	old.cap_permitted.val = 0x3;
	new.cap_permitted.val = 0x1;
	KUNIT_EXPECT_FALSE(test, rust_cred_test_changes_dumpability(&old, &new));
	new.cap_permitted.val = 0x4;
	KUNIT_EXPECT_TRUE(test, rust_cred_test_changes_dumpability(&old, &new));

#ifdef CONFIG_USER_NS
	child_ns = kunit_kzalloc(test, sizeof(*child_ns), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, child_ns);
	child_ns->parent = &init_user_ns;
	child_ns->owner = old.euid;
	new = old;
	new.user_ns = child_ns;
	KUNIT_EXPECT_FALSE(test,
			   rust_cred_test_changes_dumpability(&old, &new));
	child_ns->owner.val++;
	KUNIT_EXPECT_TRUE(test,
			  rust_cred_test_changes_dumpability(&old, &new));
#endif
}
#endif

static struct kunit_case cred_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_CRED_LIFECYCLE
	KUNIT_CASE(cred_lifecycle_test_policy),
	KUNIT_CASE(cred_lifecycle_test_failure_injection),
#endif
	KUNIT_CASE(cred_lifecycle_test_blank),
	KUNIT_CASE(cred_lifecycle_test_prepare_abort),
	KUNIT_CASE(cred_lifecycle_test_ucounts),
	KUNIT_CASE(cred_lifecycle_test_prepare_exec),
	KUNIT_CASE(cred_lifecycle_test_kernel),
	KUNIT_CASE(cred_lifecycle_test_get_task),
	KUNIT_CASE(cred_lifecycle_test_commit_identical),
	KUNIT_CASE(cred_lifecycle_test_fscmp),
	KUNIT_CASE(cred_lifecycle_test_rcu_release),
	KUNIT_CASE(cred_lifecycle_test_direct_release),
	KUNIT_CASE(cred_lifecycle_test_invalid_create_files_as),
	KUNIT_CASE(cred_lifecycle_test_concurrency),
	KUNIT_CASE(cred_lifecycle_test_fork_exit),
	KUNIT_CASE(cred_lifecycle_test_thread_copy),
	{}
};

static struct kunit_suite cred_lifecycle_test_suite = {
	.name = "cred_lifecycle",
	.test_cases = cred_lifecycle_test_cases,
};

kunit_test_suite(cred_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for credential lifecycle");
MODULE_LICENSE("GPL");
