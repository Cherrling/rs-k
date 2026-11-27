// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>
#include <linux/string.h>
#include <linux/stringhash.h>

#define DENTRY_TEST_TIMEOUT		(5 * HZ)
#define DENTRY_TEST_WORKERS		4
#define DENTRY_TEST_LOOKUPS		4096

static atomic_t dentry_release_count;

struct dentry_reader_context {
	struct completion done;
	struct dentry *parent;
	const struct qstr *name;
	atomic_t errors;
	atomic_t lookups;
};

static void dentry_test_release(struct dentry *dentry)
{
	atomic_inc(&dentry_release_count);
}

static const struct dentry_operations dentry_test_ops = {
	.d_release = dentry_test_release,
};

static int dentry_test_get_root(struct path *root)
{
	return kern_path("/", LOOKUP_DIRECTORY, root);
}

static struct qstr dentry_test_name(const struct dentry *parent,
				    const char *name)
{
	struct qstr qstr;

	qstr.name = name;
	qstr.hash_len = hashlen_string(parent, name);
	return qstr;
}

static void dentry_lifecycle_test_null_drop(struct kunit *test)
{
	dput(NULL);
	KUNIT_SUCCEED(test);
}

static void dentry_lifecycle_test_final_drop(struct kunit *test)
{
	struct path root;
	struct dentry *dentry;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	dentry = d_alloc_anon(root.dentry->d_sb);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate anonymous dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	KUNIT_EXPECT_EQ(test, 1, d_count(dentry));
	dput(dentry);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static void dentry_lifecycle_test_shared_drop(struct kunit *test)
{
	struct path root;
	struct dentry *dentry;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	dentry = d_alloc_anon(root.dentry->d_sb);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate anonymous dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	dget(dentry);
	dput(dentry);
	KUNIT_EXPECT_EQ(test, 1, d_count(dentry));
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&dentry_release_count));

	dput(dentry);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static void dentry_lifecycle_test_external_name(struct kunit *test)
{
	const char *name_text =
		"dentry-external-name-that-does-not-fit-inline-storage-for-kunit";
	struct qstr name;
	struct path root;
	struct dentry *dentry;
	struct dentry *lookup;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	name = dentry_test_name(root.dentry, name_text);
	dentry = d_alloc_name(root.dentry, name.name);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate external-name dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	d_add(dentry, NULL);
	dput(dentry);

	lookup = d_lookup(root.dentry, &name);
	if (!lookup) {
		KUNIT_FAIL(test, "external-name dentry was not found");
		path_put(&root);
		return;
	}
	KUNIT_EXPECT_PTR_EQ(test, dentry, lookup);
	d_drop(lookup);
	dput(lookup);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static void dentry_lifecycle_test_shrink_list(struct kunit *test)
{
	LIST_HEAD(dispose);
	struct path root;
	struct dentry *dentry;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	dentry = d_alloc_anon(root.dentry->d_sb);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate anonymous dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	dput_to_list(dentry, &dispose);

	KUNIT_EXPECT_FALSE(test, list_empty(&dispose));
	KUNIT_EXPECT_EQ(test, 0, d_count(dentry));
	KUNIT_EXPECT_TRUE(test, dentry->d_flags & DCACHE_SHRINK_LIST);
	KUNIT_EXPECT_EQ(test, 0, atomic_read(&dentry_release_count));

	shrink_dentry_list(&dispose);
	KUNIT_EXPECT_TRUE(test, list_empty(&dispose));
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static void dentry_lifecycle_test_lru_retention(struct kunit *test)
{
	struct qstr name;
	struct path root;
	struct dentry *dentry;
	struct dentry *lookup;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	name = dentry_test_name(root.dentry, "dentry-lru-retention");
	dentry = d_alloc_name(root.dentry, name.name);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate named dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	d_add(dentry, NULL);
	dput(dentry);

	lookup = d_lookup(root.dentry, &name);
	if (!lookup) {
		KUNIT_FAIL(test, "retained negative dentry was not found");
		path_put(&root);
		return;
	}
	KUNIT_EXPECT_PTR_EQ(test, dentry, lookup);
	KUNIT_EXPECT_EQ(test, 1, d_count(lookup));
	KUNIT_EXPECT_TRUE(test, lookup->d_flags & DCACHE_LRU_LIST);

	spin_lock(&lookup->d_lock);
	lookup->d_flags |= DCACHE_REFERENCED;
	spin_unlock(&lookup->d_lock);
	dput(lookup);

	lookup = d_lookup(root.dentry, &name);
	if (!lookup) {
		KUNIT_FAIL(test, "referenced dentry was not retained");
		path_put(&root);
		return;
	}
	KUNIT_EXPECT_PTR_EQ(test, dentry, lookup);
	KUNIT_EXPECT_TRUE(test, lookup->d_flags & DCACHE_LRU_LIST);

	d_drop(lookup);
	dput(lookup);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static void dentry_lifecycle_test_lru_shrink(struct kunit *test)
{
	struct qstr name;
	struct path root;
	struct dentry *dentry;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	name = dentry_test_name(root.dentry, "dentry-lru-shrink");
	dentry = d_alloc_name(root.dentry, name.name);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate named dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	d_add(dentry, NULL);
	dput(dentry);

	shrink_dcache_sb(root.dentry->d_sb);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	KUNIT_EXPECT_NULL(test, d_lookup(root.dentry, &name));
	path_put(&root);
}

static void dentry_lifecycle_test_discardable(struct kunit *test)
{
	struct path root;
	struct dentry *dentry;
	struct inode *inode;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	inode = new_inode(root.dentry->d_sb);
	if (!inode) {
		KUNIT_FAIL(test, "failed to allocate inode");
		path_put(&root);
		return;
	}
	inode->i_mode = S_IFREG;
	set_nlink(inode, 1);

	dentry = d_alloc_name(root.dentry, "dentry-discardable");
	if (!dentry) {
		iput(inode);
		KUNIT_FAIL(test, "failed to allocate named dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	KUNIT_EXPECT_PTR_EQ(test, dentry, d_make_persistent(dentry, inode));
	dput(dentry);
	KUNIT_EXPECT_EQ(test, 1, d_count(dentry));
	KUNIT_EXPECT_TRUE(test, dentry->d_flags & DCACHE_PERSISTENT);

	d_make_discardable(dentry);
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static int dentry_reader_thread(void *data)
{
	struct dentry_reader_context *ctx = data;
	int index;

	for (index = 0; index < DENTRY_TEST_LOOKUPS; index++) {
		struct dentry *dentry;

		dentry = d_lookup(ctx->parent, ctx->name);
		if (!dentry) {
			atomic_inc(&ctx->errors);
			break;
		}
		atomic_inc(&ctx->lookups);
		dput(dentry);
	}
	complete(&ctx->done);
	return 0;
}

static void dentry_lifecycle_test_concurrent(struct kunit *test)
{
	struct qstr name;
	struct dentry_reader_context contexts[DENTRY_TEST_WORKERS];
	struct task_struct *tasks[DENTRY_TEST_WORKERS];
	struct path root;
	struct dentry *dentry;
	struct dentry *lookup;
	bool timed_out = false;
	int started = 0;
	int index;

	KUNIT_ASSERT_EQ(test, 0, dentry_test_get_root(&root));
	name = dentry_test_name(root.dentry, "dentry-concurrent");
	dentry = d_alloc_name(root.dentry, name.name);
	if (!dentry) {
		KUNIT_FAIL(test, "failed to allocate named dentry");
		path_put(&root);
		return;
	}

	atomic_set(&dentry_release_count, 0);
	dentry->d_op = &dentry_test_ops;
	d_add(dentry, NULL);
	dput(dentry);

	for (index = 0; index < DENTRY_TEST_WORKERS; index++) {
		init_completion(&contexts[index].done);
		contexts[index].parent = root.dentry;
		contexts[index].name = &name;
		atomic_set(&contexts[index].errors, 0);
		atomic_set(&contexts[index].lookups, 0);
		tasks[index] = kthread_run(dentry_reader_thread, &contexts[index],
					   "dentry-reader-%d", index);
		if (IS_ERR(tasks[index])) {
			KUNIT_FAIL(test, "failed to start reader %d", index);
			break;
		}
		get_task_struct(tasks[index]);
		started++;
	}

	for (index = 0; index < started; index++) {
		if (!wait_for_completion_timeout(&contexts[index].done,
						 DENTRY_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < started; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&contexts[index].errors));
		KUNIT_EXPECT_EQ(test, DENTRY_TEST_LOOKUPS,
				atomic_read(&contexts[index].lookups));
	}
	KUNIT_EXPECT_EQ(test, DENTRY_TEST_WORKERS, started);
	KUNIT_EXPECT_FALSE(test, timed_out);

	lookup = d_lookup(root.dentry, &name);
	if (lookup) {
		d_drop(lookup);
		dput(lookup);
	}
	rcu_barrier();
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&dentry_release_count));
	path_put(&root);
}

static struct kunit_case dentry_lifecycle_test_cases[] = {
	KUNIT_CASE(dentry_lifecycle_test_null_drop),
	KUNIT_CASE(dentry_lifecycle_test_final_drop),
	KUNIT_CASE(dentry_lifecycle_test_shared_drop),
	KUNIT_CASE(dentry_lifecycle_test_external_name),
	KUNIT_CASE(dentry_lifecycle_test_shrink_list),
	KUNIT_CASE(dentry_lifecycle_test_lru_shrink),
	KUNIT_CASE(dentry_lifecycle_test_lru_retention),
	KUNIT_CASE(dentry_lifecycle_test_discardable),
	KUNIT_CASE(dentry_lifecycle_test_concurrent),
	{}
};

static struct kunit_suite dentry_lifecycle_test_suite = {
	.name = "dentry_lifecycle",
	.test_cases = dentry_lifecycle_test_cases,
};

kunit_test_suite(dentry_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for dentry lifecycle");
MODULE_LICENSE("GPL");
