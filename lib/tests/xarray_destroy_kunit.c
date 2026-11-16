// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray teardown.
 */

#include <kunit/test.h>

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_DEEP_INDEX (XA_CHUNK_SIZE * XA_CHUNK_SIZE + 17)
#define XARRAY_TEST_CONCURRENT_READERS 4
#define XARRAY_TEST_CONCURRENT_ITERATIONS 10000
#define XARRAY_TEST_TIMEOUT_MS 10000

struct xarray_test_worker {
	struct xarray *xa;
	void *value;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *failed;
};

static void xarray_test_destroy(void *data)
{
	xa_destroy(data);
}

static struct xarray *xarray_test_init_flags(struct kunit *test, gfp_t flags)
{
	struct xarray *xa;

	xa = kunit_kzalloc(test, sizeof(*xa), GFP_KERNEL);
	if (!xa) {
		KUNIT_FAIL(test, "failed to allocate XArray");
		return NULL;
	}

	xa_init_flags(xa, flags);
	if (kunit_add_action_or_reset(test, xarray_test_destroy, xa)) {
		KUNIT_FAIL(test, "failed to register XArray cleanup");
		return NULL;
	}

	return xa;
}

static struct xarray *xarray_test_init(struct kunit *test)
{
	return xarray_test_init_flags(test, 0);
}

static void *xarray_test_head(struct xarray *xa)
{
	void *entry;

	rcu_read_lock();
	entry = xa_head(xa);
	rcu_read_unlock();

	return entry;
}

static int xarray_test_reader_fn(void *data)
{
	struct xarray_test_worker *worker = data;
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < XARRAY_TEST_CONCURRENT_ITERATIONS; i++) {
		void *entry = xa_load(worker->xa, XARRAY_TEST_DEEP_INDEX);

		if (entry && entry != worker->value)
			atomic_inc(worker->failed);
		if (!(i & 31))
			cond_resched();
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void xarray_test_empty_clears_marks(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xa->xa_flags |= XA_FLAGS_MARK(XA_MARK_0);

	xa_destroy(xa);

	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
}

static void xarray_test_direct_root(struct kunit *test)
{
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_1);
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), value);

	xa_destroy(xa);

	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
}

static void xarray_test_deep_tree(struct kunit *test)
{
	static const unsigned long indexes[] = {
		0,
		XA_CHUNK_SIZE,
		XARRAY_TEST_DEEP_INDEX,
		2 * XARRAY_TEST_DEEP_INDEX,
	};
	void *value = xa_mk_value(0x2345);
	struct xarray *xa = xarray_test_init(test);
	size_t i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, indexes[i], value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, 4 * XA_CHUNK_SIZE,
					 5 * XA_CHUNK_SIZE - 1, value,
					 GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_0);
	xa_set_mark(xa, indexes[1], XA_MARK_1);
	xa_set_mark(xa, indexes[2], XA_MARK_2);
	KUNIT_ASSERT_TRUE(test, xa_is_node(xarray_test_head(xa)));
	KUNIT_ASSERT_TRUE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_ASSERT_TRUE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_ASSERT_TRUE(test, xa_marked(xa, XA_MARK_2));

	xa_destroy(xa);
	rcu_barrier();

	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_2));
	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_NULL(test, xa_load(xa, indexes[i]));
}

static void xarray_test_zero_busy_allocation(struct kunit *test)
{
	void *value = xa_mk_value(0x3456);
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC1);
	u32 id = U32_MAX;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, 0, GFP_KERNEL), 0);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));

	xa_destroy(xa);
	rcu_barrier();

	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_FALSE(test, xa->xa_flags & XA_FLAGS_MARK(XA_FREE_MARK));
	KUNIT_EXPECT_EQ(test,
			xa_alloc(xa, &id, value, XA_LIMIT(0, 0), GFP_KERNEL),
			-EBUSY);
	KUNIT_EXPECT_EQ(test, id, U32_MAX);
}

static void xarray_test_destroy_and_reuse(struct kunit *test)
{
	void *value_a = xa_mk_value(0x4567);
	void *value_b = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value_a,
				   GFP_KERNEL));
	xa_destroy(xa);
	rcu_barrier();

	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX + 1, value_b,
				   GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX + 1),
			    value_b);
	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX + 1),
			    value_b);
}

static void xarray_test_concurrent_rcu_readers(struct kunit *test)
{
	struct xarray_test_worker workers[XARRAY_TEST_CONCURRENT_READERS] = {};
	struct task_struct *tasks[XARRAY_TEST_CONCURRENT_READERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct xarray *xa;
	void *value = xa_mk_value(0x6789);
	unsigned int started = 0;
	unsigned int i;

	xa = xarray_test_init(test);
	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));

	timeout = msecs_to_jiffies(XARRAY_TEST_TIMEOUT_MS);
	for (i = 0; i < XARRAY_TEST_CONCURRENT_READERS; i++) {
		workers[i].xa = xa;
		workers[i].value = value;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].failed = &failed;
		tasks[i] = kthread_run(xarray_test_reader_fn, &workers[i],
				       "xarray-destroy-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	deadline = jiffies + timeout;
	while (atomic_read(&ready) < started && time_before(jiffies, deadline))
		cond_resched();
	atomic_set(&start, 1);
	if (started)
		msleep(20);

	xa_destroy(xa);
	rcu_barrier();

	deadline = jiffies + timeout;
	while (atomic_read(&done) < started && time_before(jiffies, deadline))
		cond_resched();

	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, XARRAY_TEST_CONCURRENT_READERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done), XARRAY_TEST_CONCURRENT_READERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
}

static struct kunit_case xarray_destroy_test_cases[] = {
	KUNIT_CASE(xarray_test_empty_clears_marks),
	KUNIT_CASE(xarray_test_direct_root),
	KUNIT_CASE(xarray_test_deep_tree),
	KUNIT_CASE(xarray_test_zero_busy_allocation),
	KUNIT_CASE(xarray_test_destroy_and_reuse),
	KUNIT_CASE(xarray_test_concurrent_rcu_readers),
	{}
};

static struct kunit_suite xarray_destroy_test_suite = {
	.name = "xarray_destroy",
	.test_cases = xarray_destroy_test_cases,
};

kunit_test_suite(xarray_destroy_test_suite);
