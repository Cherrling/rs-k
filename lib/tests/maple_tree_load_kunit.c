// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the active Maple Tree load implementation.
 */

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/maple_tree.h>
#include <linux/sched.h>
#include <linux/xarray.h>

#define MAPLE_LOAD_CONCURRENT_WORKERS 4
#define MAPLE_LOAD_CONCURRENT_ITERATIONS 2000
#define MAPLE_LOAD_TIMEOUT_MS 10000

static void maple_load_test_destroy(void *data)
{
	mtree_destroy(data);
}

static struct maple_tree *maple_load_test_init(struct kunit *test)
{
	struct maple_tree *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		KUNIT_FAIL(test, "failed to allocate Maple tree");
		return NULL;
	}

	mt_init(tree);
	if (kunit_add_action_or_reset(test, maple_load_test_destroy, tree)) {
		KUNIT_FAIL(test, "failed to register Maple tree cleanup");
		return NULL;
	}

	return tree;
}

static void maple_load_test_empty(struct kunit *test)
{
	struct maple_tree *tree = maple_load_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 0));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 1));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, ULONG_MAX));
}

static void maple_load_test_single_root(struct kunit *test)
{
	void *value = xa_mk_value(0x1234);
	struct maple_tree *tree = maple_load_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 0, value, GFP_KERNEL), 0);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), value);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 1));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, ULONG_MAX));
}

static void maple_load_test_deep_nodes(struct kunit *test)
{
	const unsigned long range_size = 16;
	const unsigned int ranges = 64;
	void *value = xa_mk_value(0x2345);
	struct maple_tree *tree = maple_load_test_init(test);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	for (i = 0; i < ranges; i++) {
		unsigned long first = i * 64;

		KUNIT_ASSERT_EQ(test,
				mtree_store_range(tree, first,
						  first + range_size - 1,
						  value, GFP_KERNEL), 0);
	}

	for (i = 0; i < ranges; i++) {
		unsigned long first = i * 64;

		KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, first), value);
		KUNIT_EXPECT_PTR_EQ(test,
				    mtree_load(tree, first + range_size - 1),
				    value);
		KUNIT_EXPECT_NULL(test,
				  mtree_load(tree, first + range_size));
	}
}

static void maple_load_test_zero_and_maximum_index(struct kunit *test)
{
	void *value = xa_mk_value(0x3456);
	struct maple_tree *tree = maple_load_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 199, XA_ZERO_ENTRY,
					  GFP_KERNEL), 0);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 100));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 150));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 199));

	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, ULONG_MAX, ULONG_MAX, value,
					  GFP_KERNEL), 0);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, ULONG_MAX), value);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, ULONG_MAX - 1));
}

struct maple_load_worker {
	struct maple_tree *tree;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int maple_load_worker_fn(void *data)
{
	const unsigned long first = 4096;
	const unsigned long last = first + 255;
	struct maple_load_worker *worker = data;
	void *value = xa_mk_value(0x4567);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < MAPLE_LOAD_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			int ret = mtree_store_range(worker->tree, first, last,
						    (i & 1) ? NULL : value,
						    GFP_KERNEL);

			if (ret)
				atomic_inc(worker->failed);
		} else {
			unsigned long index = first + (i & 255);
			void *entry = mtree_load(worker->tree, index);

			if (entry && entry != value)
				atomic_inc(worker->failed);
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

static void maple_load_test_concurrent_rcu(struct kunit *test)
{
	struct maple_load_worker workers[MAPLE_LOAD_CONCURRENT_WORKERS];
	struct task_struct *tasks[MAPLE_LOAD_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct maple_tree *tree = maple_load_test_init(test);
	unsigned int started = 0;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	mt_set_in_rcu(tree);
	init_completion(&complete);
	timeout = msecs_to_jiffies(MAPLE_LOAD_TIMEOUT_MS);

	for (i = 0; i < MAPLE_LOAD_CONCURRENT_WORKERS; i++) {
		workers[i].tree = tree;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(maple_load_worker_fn, &workers[i],
				       "maple-load-%u", i);
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
			KUNIT_FAIL(test, "Maple load worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, MAPLE_LOAD_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			MAPLE_LOAD_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case maple_load_test_cases[] = {
	KUNIT_CASE(maple_load_test_empty),
	KUNIT_CASE(maple_load_test_single_root),
	KUNIT_CASE(maple_load_test_deep_nodes),
	KUNIT_CASE(maple_load_test_zero_and_maximum_index),
	KUNIT_CASE(maple_load_test_concurrent_rcu),
	{}
};

static struct kunit_suite maple_load_test_suite = {
	.name = "maple_tree_load",
	.test_cases = maple_load_test_cases,
};

kunit_test_suite(maple_load_test_suite);
