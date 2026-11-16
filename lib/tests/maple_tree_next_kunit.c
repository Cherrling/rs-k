// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the active Maple Tree forward iterator.
 */

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/maple_tree.h>
#include <linux/sched.h>
#include <linux/xarray.h>

#define MAPLE_NEXT_CONCURRENT_WORKERS 4
#define MAPLE_NEXT_CONCURRENT_ITERATIONS 3000
#define MAPLE_NEXT_TIMEOUT_MS 10000
#define MAPLE_NEXT_MULTI_ENTRIES 256

static void maple_next_test_destroy(void *data)
{
	mtree_destroy(data);
}

static struct maple_tree *maple_next_test_init(struct kunit *test)
{
	struct maple_tree *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		KUNIT_FAIL(test, "failed to allocate Maple tree");
		return NULL;
	}

	mt_init(tree);
	if (kunit_add_action_or_reset(test, maple_next_test_destroy, tree)) {
		KUNIT_FAIL(test, "failed to register Maple tree cleanup");
		return NULL;
	}

	return tree;
}

static void maple_next_test_empty_and_root(struct kunit *test)
{
	void *value = xa_mk_value(0x5678);
	struct maple_tree *tree = maple_next_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_next(&mas, ULONG_MAX));
	rcu_read_unlock();
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);
	KUNIT_EXPECT_EQ(test, mas.index, 0);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);

	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 0, value, GFP_KERNEL), 0);
	mas_set(&mas, 0);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_next(&mas, ULONG_MAX));
	rcu_read_unlock();
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);
	KUNIT_EXPECT_EQ(test, mas.index, 1);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
}

static void maple_next_test_sparse_forward(struct kunit *test)
{
	void *value_a = xa_mk_value(0x1001);
	void *value_b = xa_mk_value(0x1002);
	void *value_c = xa_mk_value(0x1003);
	struct maple_tree *tree = maple_next_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 12, value_a, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 105, value_b, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 1000, 1000, value_c, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 2000, 2000, XA_ZERO_ENTRY,
					  GFP_KERNEL), 0);

	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value_a);
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 10);
	KUNIT_EXPECT_EQ(test, mas.last, 12);

	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value_b);
	KUNIT_EXPECT_EQ(test, mas.index, 100);
	KUNIT_EXPECT_EQ(test, mas.last, 105);

	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value_c);
	KUNIT_EXPECT_EQ(test, mas.index, 1000);
	KUNIT_EXPECT_EQ(test, mas.last, 1000);

	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), XA_ZERO_ENTRY);
	KUNIT_EXPECT_EQ(test, mas.index, 2000);
	KUNIT_EXPECT_EQ(test, mas.last, 2000);

	KUNIT_EXPECT_NULL(test, mas_next(&mas, ULONG_MAX));
	rcu_read_unlock();
	KUNIT_EXPECT_EQ(test, mas.status, ma_overflow);
}

static void maple_next_test_multi_node_forward(struct kunit *test)
{
	struct maple_tree *tree = maple_next_test_init(test);
	void *entry;
	unsigned long i;

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	for (i = 0; i < MAPLE_NEXT_MULTI_ENTRIES; i++) {
		KUNIT_ASSERT_EQ(test,
				mtree_store_range(tree, i * 4, i * 4,
						  xa_mk_value(0x6000 + i),
						  GFP_KERNEL),
				0);
	}

	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX),
			    xa_mk_value(0x6000));
	KUNIT_EXPECT_EQ(test, mas.index, 0);
	KUNIT_EXPECT_EQ(test, mas.last, 0);

	for (i = 1; i < MAPLE_NEXT_MULTI_ENTRIES; i++) {
		entry = mas_next(&mas, ULONG_MAX);
		KUNIT_EXPECT_PTR_EQ(test, entry, xa_mk_value(0x6000 + i));
		KUNIT_EXPECT_EQ(test, mas.index, i * 4);
		KUNIT_EXPECT_EQ(test, mas.last, i * 4);
	}
	KUNIT_EXPECT_NULL(test, mas_next(&mas, ULONG_MAX));
	rcu_read_unlock();

	mas_set(&mas, 0);
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX),
			    xa_mk_value(0x6000));

	for (i = 1; i < MAPLE_NEXT_MULTI_ENTRIES; i++) {
		KUNIT_EXPECT_NULL(test, mas_next_range(&mas, ULONG_MAX));
		KUNIT_EXPECT_EQ(test, mas.index, i * 4 - 3);
		KUNIT_EXPECT_EQ(test, mas.last, i * 4 - 1);

		entry = mas_next_range(&mas, ULONG_MAX);
		KUNIT_EXPECT_PTR_EQ(test, entry, xa_mk_value(0x6000 + i));
		KUNIT_EXPECT_EQ(test, mas.index, i * 4);
		KUNIT_EXPECT_EQ(test, mas.last, i * 4);
	}
	rcu_read_unlock();
}

static void maple_next_test_range_and_pause(struct kunit *test)
{
	void *value_a = xa_mk_value(0x2001);
	void *value_b = xa_mk_value(0x2002);
	void *value_c = xa_mk_value(0x2003);
	void *value_d = xa_mk_value(0x2004);
	struct maple_tree *tree = maple_next_test_init(test);
	void *entry;

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 12, value_a, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 105, value_b, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 1000, 1000, value_c, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 2000, 2000, value_d, GFP_KERNEL), 0);

	rcu_read_lock();
	entry = mas_find(&mas, ULONG_MAX);
	KUNIT_EXPECT_PTR_EQ(test, entry, value_a);
	KUNIT_EXPECT_EQ(test, mas.index, 10);
	KUNIT_EXPECT_EQ(test, mas.last, 12);

	entry = mas_next_range(&mas, ULONG_MAX);
	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, mas.index, 13);
	KUNIT_EXPECT_EQ(test, mas.last, 99);

	entry = mas_next_range(&mas, ULONG_MAX);
	KUNIT_EXPECT_PTR_EQ(test, entry, value_b);
	KUNIT_EXPECT_EQ(test, mas.index, 100);
	KUNIT_EXPECT_EQ(test, mas.last, 105);
	rcu_read_unlock();

	mas_pause(&mas);
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value_c);
	KUNIT_EXPECT_EQ(test, mas.index, 1000);
	KUNIT_EXPECT_EQ(test, mas.last, 1000);
	rcu_read_unlock();

	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_next(&mas, 1500));
	KUNIT_EXPECT_EQ(test, mas.status, ma_overflow);
	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value_d);
	KUNIT_EXPECT_EQ(test, mas.index, 2000);
	KUNIT_EXPECT_EQ(test, mas.last, 2000);
	rcu_read_unlock();
}

static void maple_next_test_underflow_and_maximum(struct kunit *test)
{
	void *value = xa_mk_value(0x3456);
	struct maple_tree *tree = maple_next_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 10, value, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, ULONG_MAX - 1, ULONG_MAX,
					  xa_mk_value(0x3457), GFP_KERNEL), 0);

	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_prev(&mas, 0));
	rcu_read_unlock();
	KUNIT_EXPECT_EQ(test, mas.status, ma_underflow);

	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_next(&mas, ULONG_MAX), value);
	rcu_read_unlock();

	mas_set(&mas, ULONG_MAX - 1);
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_walk(&mas), xa_mk_value(0x3457));
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
	KUNIT_EXPECT_NULL(test, mas_next(&mas, ULONG_MAX));
	rcu_read_unlock();
	KUNIT_EXPECT_EQ(test, mas.status, ma_overflow);
}

struct maple_next_worker {
	struct maple_tree *tree;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int maple_next_worker_fn(void *data)
{
	const unsigned long stable_first = 4096;
	const unsigned long stable_last = stable_first + 255;
	const unsigned long moving_first = 8192;
	const unsigned long moving_last = moving_first + 255;
	struct maple_next_worker *worker = data;
	void *value = xa_mk_value(0x4567);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < MAPLE_NEXT_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			int ret = mtree_store_range(worker->tree, moving_first,
						    moving_last,
						    (i & 1) ? NULL : value,
						    GFP_KERNEL);

			if (ret)
				atomic_inc(worker->failed);
		} else {
			MA_STATE(mas, worker->tree, 0, 0);
			void *entry;

			rcu_read_lock();
			entry = mas_find(&mas, ULONG_MAX);
			if (entry != value || mas.index != stable_first ||
			    mas.last != stable_last)
				atomic_inc(worker->failed);
			entry = mas_next(&mas, ULONG_MAX);
			if (entry && entry != value)
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

static void maple_next_test_concurrent_rcu(struct kunit *test)
{
	struct maple_next_worker workers[MAPLE_NEXT_CONCURRENT_WORKERS];
	struct task_struct *tasks[MAPLE_NEXT_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct maple_tree *tree = maple_next_test_init(test);
	unsigned int started = 0;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 4096, 4351,
					  xa_mk_value(0x4567), GFP_KERNEL), 0);
	mt_set_in_rcu(tree);
	init_completion(&complete);
	timeout = msecs_to_jiffies(MAPLE_NEXT_TIMEOUT_MS);

	for (i = 0; i < MAPLE_NEXT_CONCURRENT_WORKERS; i++) {
		workers[i].tree = tree;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(maple_next_worker_fn, &workers[i],
				       "maple-next-%u", i);
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
			KUNIT_FAIL(test, "Maple next worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, MAPLE_NEXT_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			MAPLE_NEXT_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case maple_next_test_cases[] = {
	KUNIT_CASE(maple_next_test_empty_and_root),
	KUNIT_CASE(maple_next_test_sparse_forward),
	KUNIT_CASE(maple_next_test_multi_node_forward),
	KUNIT_CASE(maple_next_test_range_and_pause),
	KUNIT_CASE(maple_next_test_underflow_and_maximum),
	KUNIT_CASE(maple_next_test_concurrent_rcu),
	{}
};

static struct kunit_suite maple_next_test_suite = {
	.name = "maple_tree_next",
	.test_cases = maple_next_test_cases,
};

kunit_test_suite(maple_next_test_suite);
