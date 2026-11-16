// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the active Maple Tree preallocation implementation.
 */

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/maple_tree.h>
#include <linux/sched.h>
#include <linux/xarray.h>

#define MAPLE_PREALLOC_FLAG 1
#define MAPLE_PREALLOC_CONCURRENT_WORKERS 4
#define MAPLE_PREALLOC_CONCURRENT_ITERATIONS 1000
#define MAPLE_PREALLOC_TIMEOUT_MS 10000

static void maple_prealloc_test_destroy(void *data)
{
	mtree_destroy(data);
}

static struct maple_tree *maple_prealloc_test_init(struct kunit *test)
{
	struct maple_tree *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		KUNIT_FAIL(test, "failed to allocate Maple tree");
		return NULL;
	}

	mt_init(tree);
	if (kunit_add_action_or_reset(test, maple_prealloc_test_destroy, tree)) {
		KUNIT_FAIL(test, "failed to register Maple tree cleanup");
		return NULL;
	}

	return tree;
}

static bool maple_prealloc_has_nodes(const struct ma_state *mas)
{
	return mas->alloc || mas->sheaf;
}

static int maple_prealloc_locked(struct maple_tree *tree, struct ma_state *mas,
				 void *entry)
{
	int ret;

	mtree_lock(tree);
	ret = mas_preallocate(mas, entry, GFP_KERNEL);
	if (ret)
		mtree_unlock(tree);

	return ret;
}

static void maple_prealloc_test_root_and_range(struct kunit *test)
{
	void *root_value = xa_mk_value(0x1001);
	void *range_value = xa_mk_value(0x1002);
	struct maple_tree *tree = maple_prealloc_test_init(test);
	MA_STATE(root, tree, 0, 0);
	MA_STATE(range, tree, 10, 20);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			maple_prealloc_locked(tree, &root, root_value), 0);
	KUNIT_EXPECT_EQ(test, root.store_type, wr_store_root);
	KUNIT_EXPECT_EQ(test, root.node_request, 0);
	KUNIT_EXPECT_FALSE(test, maple_prealloc_has_nodes(&root));
	KUNIT_EXPECT_EQ(test, root.mas_flags & MAPLE_PREALLOC_FLAG,
			MAPLE_PREALLOC_FLAG);
	mas_store_prealloc(&root, root_value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), root_value);
	KUNIT_EXPECT_FALSE(test, root.mas_flags & MAPLE_PREALLOC_FLAG);

	KUNIT_ASSERT_EQ(test,
			maple_prealloc_locked(tree, &range, range_value), 0);
	KUNIT_EXPECT_EQ(test, range.store_type, wr_store_root);
	KUNIT_EXPECT_EQ(test, range.node_request, 0);
	KUNIT_EXPECT_TRUE(test, maple_prealloc_has_nodes(&range));
	mas_store_prealloc(&range, range_value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 10), range_value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 20), range_value);
	KUNIT_EXPECT_FALSE(test, maple_prealloc_has_nodes(&range));
}

static int maple_prealloc_store_many(struct maple_tree *tree)
{
	unsigned long i;

	for (i = 0; i <= 100; i++) {
		int ret = mtree_store_range(tree, i * 10, i * 10 + 5,
					    xa_mk_value(i + 1), GFP_KERNEL);

		if (ret)
			return ret;
	}

	return 0;
}

static void maple_prealloc_test_spanning_and_split(struct kunit *test)
{
	void *value = xa_mk_value(0x2001);
	struct maple_tree *tree = maple_prealloc_test_init(test);
	MA_STATE(span, tree, 470, 500);
	MA_STATE(split, tree, 54, 54);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_prealloc_store_many(tree), 0);
	KUNIT_ASSERT_EQ(test,
			maple_prealloc_locked(tree, &span, value), 0);
	KUNIT_EXPECT_EQ(test, span.store_type, wr_spanning_store);
	KUNIT_EXPECT_TRUE(test, maple_prealloc_has_nodes(&span));
	mas_store_prealloc(&span, value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 470), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 500), value);

	KUNIT_ASSERT_EQ(test,
			maple_prealloc_locked(tree, &split, value), 0);
	KUNIT_EXPECT_EQ(test, split.store_type, wr_split_store);
	KUNIT_EXPECT_TRUE(test, maple_prealloc_has_nodes(&split));
	mas_store_prealloc(&split, value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 54), value);
}

static void maple_prealloc_test_slot_and_rcu(struct kunit *test)
{
	void *value = xa_mk_value(0x3001);
	struct maple_tree *tree = maple_prealloc_test_init(test);
	MA_STATE(mas, tree, 6, 10);
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_prealloc_store_many(tree), 0);
	mt_set_in_rcu(tree);

	KUNIT_ASSERT_EQ(test,
			maple_prealloc_locked(tree, &mas, value), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_slot_store);
	KUNIT_EXPECT_FALSE(test, maple_prealloc_has_nodes(&mas));
	mas.last = 20;
	ret = mas_preallocate(&mas, value, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, ret, 0);
	if (ret) {
		mtree_unlock(tree);
		KUNIT_FAIL(test, "second preallocation failed");
		return;
	}
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_node_store);
	KUNIT_EXPECT_TRUE(test, maple_prealloc_has_nodes(&mas));
	mas_store_prealloc(&mas, value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 20), value);
}

struct maple_prealloc_worker {
	struct maple_tree *tree;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int maple_prealloc_worker_fn(void *data)
{
	const unsigned long stable_first = 4096;
	const unsigned long moving_first = 8192;
	const unsigned long moving_last = moving_first + 255;
	struct maple_prealloc_worker *worker = data;
	void *stable_value = xa_mk_value(0x4001);
	void *moving_value = xa_mk_value(0x4002);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < MAPLE_PREALLOC_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			MA_STATE(mas, worker->tree, moving_first, moving_last);
			void *entry = (i & 1) ? NULL : moving_value;
			int ret;

			mtree_lock(worker->tree);
			ret = mas_preallocate(&mas, entry, GFP_KERNEL);
			if (!ret)
				mas_store_prealloc(&mas, entry);
			else
				mas_destroy(&mas);
			mtree_unlock(worker->tree);
			if (ret)
				atomic_inc(worker->failed);
		} else {
			void *entry;

			rcu_read_lock();
			entry = mtree_load(worker->tree, stable_first);
			if (entry != stable_value)
				atomic_inc(worker->failed);
			entry = mtree_load(worker->tree, moving_first);
			if (entry && entry != moving_value)
				atomic_inc(worker->failed);
			rcu_read_unlock();
		}
		if (!(i & 31))
			cond_resched();
	}

	atomic_inc(worker->done);
	complete(worker->complete);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void maple_prealloc_test_concurrent_rcu(struct kunit *test)
{
	struct maple_prealloc_worker workers[MAPLE_PREALLOC_CONCURRENT_WORKERS];
	struct task_struct *tasks[MAPLE_PREALLOC_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct maple_tree *tree = maple_prealloc_test_init(test);
	unsigned int started = 0;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 4096, 4351,
					  xa_mk_value(0x4001), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 8192, 8447,
					  xa_mk_value(0x4002), GFP_KERNEL), 0);
	mt_set_in_rcu(tree);
	init_completion(&complete);
	timeout = msecs_to_jiffies(MAPLE_PREALLOC_TIMEOUT_MS);

	for (i = 0; i < MAPLE_PREALLOC_CONCURRENT_WORKERS; i++) {
		workers[i].tree = tree;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(maple_prealloc_worker_fn, &workers[i],
				       "maple-prealloc-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	deadline = jiffies + timeout;
	while (atomic_read(&ready) < started && time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) == started)
		atomic_set(&start, 1);

	for (i = 0; i < started; i++) {
		if (!wait_for_completion_timeout(&complete, timeout)) {
			KUNIT_FAIL(test, "Maple preallocation worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, MAPLE_PREALLOC_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			MAPLE_PREALLOC_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case maple_prealloc_test_cases[] = {
	KUNIT_CASE(maple_prealloc_test_root_and_range),
	KUNIT_CASE(maple_prealloc_test_spanning_and_split),
	KUNIT_CASE(maple_prealloc_test_slot_and_rcu),
	KUNIT_CASE(maple_prealloc_test_concurrent_rcu),
	{}
};

static struct kunit_suite maple_prealloc_test_suite = {
	.name = "maple_tree_prealloc",
	.test_cases = maple_prealloc_test_cases,
};

kunit_test_suite(maple_prealloc_test_suite);
