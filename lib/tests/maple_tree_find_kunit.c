// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the active Maple Tree forward find implementation.
 */

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/maple_tree.h>
#include <linux/sched.h>
#include <linux/xarray.h>

#define MAPLE_FIND_CONCURRENT_WORKERS 4
#define MAPLE_FIND_CONCURRENT_ITERATIONS 3000
#define MAPLE_FIND_TIMEOUT_MS 10000

static void maple_find_test_destroy(void *data)
{
	mtree_destroy(data);
}

static struct maple_tree *maple_find_test_init(struct kunit *test)
{
	struct maple_tree *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		KUNIT_FAIL(test, "failed to allocate Maple tree");
		return NULL;
	}

	mt_init(tree);
	if (kunit_add_action_or_reset(test, maple_find_test_destroy, tree)) {
		KUNIT_FAIL(test, "failed to register Maple tree cleanup");
		return NULL;
	}

	return tree;
}

static void maple_find_test_empty_and_root(struct kunit *test)
{
	void *value = xa_mk_value(0x5678);
	struct maple_tree *tree = maple_find_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);

	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 0, value, GFP_KERNEL), 0);
	mas_set(&mas, 0);
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value);
	KUNIT_EXPECT_EQ(test, mas.status, ma_root);
	KUNIT_EXPECT_EQ(test, mas.index, 0);
	KUNIT_EXPECT_EQ(test, mas.last, 0);

	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);
	KUNIT_EXPECT_EQ(test, mas.index, 1);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);

	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);

	mas_set(&mas, 10);
	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_none);
	KUNIT_EXPECT_EQ(test, mas.index, 1);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
	rcu_read_unlock();
}

static void maple_find_test_repeated_find(struct kunit *test)
{
	void *value_a = xa_mk_value(0x1001);
	void *value_b = xa_mk_value(0x1002);
	void *value_c = xa_mk_value(0x1003);
	struct maple_tree *tree = maple_find_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 12, value_a, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 105, value_b, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 1000, 1000, value_c, GFP_KERNEL), 0);

	mas_set(&mas, 11);
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_a);
	KUNIT_EXPECT_EQ(test, mas.index, 10);
	KUNIT_EXPECT_EQ(test, mas.last, 12);

	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_b);
	KUNIT_EXPECT_EQ(test, mas.index, 100);
	KUNIT_EXPECT_EQ(test, mas.last, 105);

	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_c);
	KUNIT_EXPECT_EQ(test, mas.index, 1000);
	KUNIT_EXPECT_EQ(test, mas.last, 1000);

	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 1001);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
	rcu_read_unlock();

	mas_set(&mas, 50);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_find(&mas, 50));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 13);
	KUNIT_EXPECT_EQ(test, mas.last, 99);
	rcu_read_unlock();

	mas_set(&mas, 100);
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, 105), value_b);
	KUNIT_EXPECT_NULL(test, mas_find(&mas, 105));
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_c);
	rcu_read_unlock();
}

static void maple_find_test_range_and_pause(struct kunit *test)
{
	void *value_a = xa_mk_value(0x2001);
	void *value_b = xa_mk_value(0x2002);
	void *value_c = xa_mk_value(0x2003);
	struct maple_tree *tree = maple_find_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 12, value_a, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 105, value_b, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 1000, 1000, value_c, GFP_KERNEL), 0);

	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, mas_find_range(&mas, ULONG_MAX), value_a);
	KUNIT_EXPECT_EQ(test, mas.index, 10);
	KUNIT_EXPECT_EQ(test, mas.last, 12);

	KUNIT_EXPECT_NULL(test, mas_find_range(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.index, 13);
	KUNIT_EXPECT_EQ(test, mas.last, 99);

	KUNIT_EXPECT_PTR_EQ(test, mas_find_range(&mas, ULONG_MAX), value_b);
	KUNIT_EXPECT_EQ(test, mas.index, 100);
	KUNIT_EXPECT_EQ(test, mas.last, 105);

	mas_pause(&mas);
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_c);
	KUNIT_EXPECT_EQ(test, mas.index, 1000);
	KUNIT_EXPECT_EQ(test, mas.last, 1000);

	KUNIT_EXPECT_NULL(test, mas_find_range(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 1001);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
	rcu_read_unlock();
}

static void maple_find_test_underflow_and_recovery(struct kunit *test)
{
	void *value_a = xa_mk_value(0x3001);
	void *value_b = xa_mk_value(0x3002);
	struct maple_tree *tree = maple_find_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0x1000, 0x1500, value_a,
					  GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0x3000, 0x3500, value_b,
					  GFP_KERNEL), 0);

	mas_set(&mas, 0x1001);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_prev(&mas, 0x1000));
	KUNIT_EXPECT_EQ(test, mas.status, ma_underflow);

	KUNIT_EXPECT_NULL(test, mas_find(&mas, 0x1000));
	KUNIT_EXPECT_EQ(test, mas.status, ma_overflow);
	KUNIT_EXPECT_NULL(test, mas_find(&mas, 0x1000));
	KUNIT_EXPECT_EQ(test, mas.status, ma_overflow);

	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_a);
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 0x1000);
	KUNIT_EXPECT_EQ(test, mas.last, 0x1500);

	KUNIT_EXPECT_NULL(test, mas_find(&mas, 0x2700));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 0x1501);
	KUNIT_EXPECT_EQ(test, mas.last, 0x2fff);

	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value_b);
	KUNIT_EXPECT_EQ(test, mas.index, 0x3000);
	KUNIT_EXPECT_EQ(test, mas.last, 0x3500);
	rcu_read_unlock();
}

static void maple_find_test_bounds_and_none(struct kunit *test)
{
	void *value = xa_mk_value(0x3457);
	struct maple_tree *tree = maple_find_test_init(test);

	MA_STATE(mas, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 100, 200,
					  xa_mk_value(0x3456), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, ULONG_MAX - 1, ULONG_MAX,
					  value, GFP_KERNEL), 0);

	mas_set(&mas, 201);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_find(&mas, 201));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 201);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX - 2);
	rcu_read_unlock();

	mas_set(&mas, 250);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_find(&mas, 250));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	KUNIT_EXPECT_EQ(test, mas.index, 201);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX - 2);
	rcu_read_unlock();

	mas_set(&mas, ULONG_MAX);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX - 1));
	KUNIT_EXPECT_EQ(test, mas.status, ma_start);
	KUNIT_EXPECT_PTR_EQ(test, mas_find(&mas, ULONG_MAX), value);
	KUNIT_EXPECT_EQ(test, mas.index, ULONG_MAX - 1);
	KUNIT_EXPECT_EQ(test, mas.last, ULONG_MAX);
	KUNIT_EXPECT_NULL(test, mas_find(&mas, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, mas.status, ma_active);
	rcu_read_unlock();
}

struct maple_find_worker {
	struct maple_tree *tree;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int maple_find_worker_fn(void *data)
{
	const unsigned long stable_first = 4096;
	const unsigned long stable_last = stable_first + 255;
	const unsigned long moving_first = 8192;
	const unsigned long moving_last = moving_first + 255;
	struct maple_find_worker *worker = data;
	void *stable_value = xa_mk_value(0x4567);
	void *moving_value = xa_mk_value(0x4568);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < MAPLE_FIND_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			int ret = mtree_store_range(worker->tree, moving_first,
						    moving_last,
						    (i & 1) ? NULL : moving_value,
						    GFP_KERNEL);

			if (ret)
				atomic_inc(worker->failed);
		} else {
			MA_STATE(mas, worker->tree, 0, 0);
			void *entry;

			rcu_read_lock();
			entry = mas_find(&mas, ULONG_MAX);
			if (entry != stable_value ||
			    mas.index != stable_first ||
			    mas.last != stable_last)
				atomic_inc(worker->failed);

			entry = mas_find(&mas, ULONG_MAX);
			if (entry == moving_value) {
				if (mas.index < moving_first ||
				    mas.last > moving_last)
					atomic_inc(worker->failed);
			} else if (entry) {
				atomic_inc(worker->failed);
			}
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

static void maple_find_test_concurrent_rcu(struct kunit *test)
{
	struct maple_find_worker workers[MAPLE_FIND_CONCURRENT_WORKERS];
	struct task_struct *tasks[MAPLE_FIND_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct maple_tree *tree = maple_find_test_init(test);
	unsigned int started = 0;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 4096, 4351,
					  xa_mk_value(0x4567), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 8192, 8447,
					  xa_mk_value(0x4568), GFP_KERNEL), 0);
	mt_set_in_rcu(tree);
	init_completion(&complete);
	timeout = msecs_to_jiffies(MAPLE_FIND_TIMEOUT_MS);

	for (i = 0; i < MAPLE_FIND_CONCURRENT_WORKERS; i++) {
		workers[i].tree = tree;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(maple_find_worker_fn, &workers[i],
				       "maple-find-%u", i);
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
			KUNIT_FAIL(test, "Maple find worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, MAPLE_FIND_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			MAPLE_FIND_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case maple_find_test_cases[] = {
	KUNIT_CASE(maple_find_test_empty_and_root),
	KUNIT_CASE(maple_find_test_repeated_find),
	KUNIT_CASE(maple_find_test_range_and_pause),
	KUNIT_CASE(maple_find_test_underflow_and_recovery),
	KUNIT_CASE(maple_find_test_bounds_and_none),
	KUNIT_CASE(maple_find_test_concurrent_rcu),
	{}
};

static struct kunit_suite maple_find_test_suite = {
	.name = "maple_tree_find",
	.test_cases = maple_find_test_cases,
};

kunit_test_suite(maple_find_test_suite);
