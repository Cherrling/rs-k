// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/inode-rust.h>
#include <linux/kthread.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/shrinker.h>

#define INODE_TEST_TIMEOUT		(5 * HZ)
#define INODE_TEST_WORKERS		4
#define INODE_TEST_ITERATIONS		4096

#define INODE_COUNTER_ALLOC		0
#define INODE_COUNTER_INIT_FAILURE	1
#define INODE_COUNTER_DESTROY		2
#define INODE_COUNTER_FREE_SCHEDULED	3
#define INODE_COUNTER_FREE_NOW		4
#define INODE_COUNTER_FAILURE		5

#define INODE_FAIL_ALLOC		1
#define INODE_FAIL_INIT			2

struct inode_worker_context {
	struct completion done;
	struct inode *inode;
	atomic_t errors;
	atomic_t iterations;
};

static int inode_test_get_root(struct path *root)
{
	return kern_path("/", LOOKUP_DIRECTORY, root);
}

#ifdef CONFIG_RUST_INODE_LIFECYCLE
static unsigned int inode_test_counter(unsigned int index)
{
	return rust_inode_test_counter(index);
}
#endif

static void inode_test_reset_counters(void)
{
#ifdef CONFIG_RUST_INODE_LIFECYCLE
	rust_inode_test_reset();
#endif
}

static void inode_lifecycle_test_null_iput(struct kunit *test)
{
	iput(NULL);
	KUNIT_SUCCEED(test);
}

static void inode_lifecycle_test_new_inode_final(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode_test_reset_counters();
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}

	KUNIT_EXPECT_EQ(test, 1, atomic_read(&inode->i_count));
	iput(inode);
	rcu_barrier();
#ifdef CONFIG_RUST_INODE_LIFECYCLE
	KUNIT_EXPECT_EQ(test, 1,
			inode_test_counter(INODE_COUNTER_DESTROY));
#endif
	path_put(&root);
}

static void inode_lifecycle_test_shared_reference(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}

	ihold(inode);
	KUNIT_EXPECT_EQ(test, 2, atomic_read(&inode->i_count));
	iput(inode);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&inode->i_count));
	iput(inode);
	rcu_barrier();
	path_put(&root);
}

static void inode_lifecycle_test_iput_not_last(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}

	ihold(inode);
	iput_not_last(inode);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&inode->i_count));
	iput(inode);
	rcu_barrier();
	path_put(&root);
}

static void inode_lifecycle_test_lru_add(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}

	spin_lock(&inode->i_lock);
	atomic_set(&inode->i_count, 0);
	inode_lru_list_add(inode);
	KUNIT_EXPECT_FALSE(test, list_empty(&inode->i_lru));
	atomic_set(&inode->i_count, 1);
	spin_unlock(&inode->i_lock);

	iput(inode);
	rcu_barrier();
	path_put(&root);
}

static void inode_lifecycle_test_lru_prune(struct kunit *test)
{
	struct shrink_control sc = {
		.gfp_mask = GFP_KERNEL,
		.nr_to_scan = 4096,
	};
	struct path root;
	struct inode *inode;
	long freed;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}

	spin_lock(&inode->i_lock);
	atomic_set(&inode->i_count, 0);
	inode_lru_list_add(inode);
	KUNIT_EXPECT_FALSE(test, list_empty(&inode->i_lru));
	spin_unlock(&inode->i_lock);

	freed = prune_icache_sb(root.dentry->d_sb, &sc);
	KUNIT_EXPECT_GT(test, freed, 0);
	rcu_barrier();
	path_put(&root);
}

static int inode_worker_thread(void *data)
{
	struct inode_worker_context *ctx = data;
	int index;

	for (index = 0; index < INODE_TEST_ITERATIONS; index++) {
		ihold(ctx->inode);
		if (atomic_read(&ctx->inode->i_count) < 2) {
			atomic_inc(&ctx->errors);
			break;
		}
		iput(ctx->inode);
		atomic_inc(&ctx->iterations);
		cond_resched();
	}
	complete(&ctx->done);
	return 0;
}

static void inode_lifecycle_test_concurrent(struct kunit *test)
{
	struct inode_worker_context contexts[INODE_TEST_WORKERS];
	struct task_struct *tasks[INODE_TEST_WORKERS];
	struct path root;
	struct inode *inode;
	struct inode *held;
	bool timed_out = false;
	int started = 0;
	int index;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	inode = d_inode(root.dentry);
	held = igrab(inode);
	if (!held) {
		KUNIT_FAIL(test, "failed to acquire root inode reference");
		path_put(&root);
		return;
	}

	for (index = 0; index < INODE_TEST_WORKERS; index++) {
		init_completion(&contexts[index].done);
		contexts[index].inode = inode;
		atomic_set(&contexts[index].errors, 0);
		atomic_set(&contexts[index].iterations, 0);
		tasks[index] = kthread_run(inode_worker_thread, &contexts[index],
					   "inode-worker-%d", index);
		if (IS_ERR(tasks[index])) {
			KUNIT_FAIL(test, "failed to start worker %d", index);
			break;
		}
		get_task_struct(tasks[index]);
		started++;
	}

	for (index = 0; index < started; index++) {
		if (!wait_for_completion_timeout(&contexts[index].done,
						 INODE_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < started; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));
		KUNIT_EXPECT_EQ(test, INODE_TEST_ITERATIONS,
				atomic_read(&contexts[index].iterations));
	}
	KUNIT_EXPECT_EQ(test, INODE_TEST_WORKERS, started);
	KUNIT_EXPECT_FALSE(test, timed_out);

	iput(held);
	path_put(&root);
}

#ifdef CONFIG_RUST_INODE_LIFECYCLE
static void inode_lifecycle_test_alloc_failure(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	rust_inode_test_reset();
	rust_inode_test_fail(INODE_FAIL_ALLOC);
	inode = new_inode(root.dentry->d_sb);
	KUNIT_EXPECT_NULL(test, inode);
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_FAILURE));
	rust_inode_test_reset();
	path_put(&root);
}

static void inode_lifecycle_test_init_rollback(struct kunit *test)
{
	struct path root;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, inode_test_get_root(&root));
	rust_inode_test_reset();
	rust_inode_test_fail(INODE_FAIL_INIT);
	inode = new_inode(root.dentry->d_sb);
	KUNIT_EXPECT_NULL(test, inode);
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_ALLOC));
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_INIT_FAILURE));
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_FREE_NOW));
	KUNIT_EXPECT_EQ(test, 1,
			rust_inode_test_counter(INODE_COUNTER_FAILURE));
	rust_inode_test_reset();
	path_put(&root);
}
#endif

static struct kunit_case inode_lifecycle_test_cases[] = {
	KUNIT_CASE(inode_lifecycle_test_null_iput),
	KUNIT_CASE(inode_lifecycle_test_new_inode_final),
	KUNIT_CASE(inode_lifecycle_test_shared_reference),
	KUNIT_CASE(inode_lifecycle_test_iput_not_last),
	KUNIT_CASE(inode_lifecycle_test_lru_add),
	KUNIT_CASE(inode_lifecycle_test_lru_prune),
	KUNIT_CASE(inode_lifecycle_test_concurrent),
#ifdef CONFIG_RUST_INODE_LIFECYCLE
	KUNIT_CASE(inode_lifecycle_test_alloc_failure),
	KUNIT_CASE(inode_lifecycle_test_init_rollback),
#endif
	{}
};

static struct kunit_suite inode_lifecycle_test_suite = {
	.name = "inode_lifecycle",
	.test_cases = inode_lifecycle_test_cases,
};

kunit_test_suite(inode_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for inode lifecycle");
MODULE_LICENSE("GPL");
