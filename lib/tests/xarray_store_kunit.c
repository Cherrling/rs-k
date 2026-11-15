// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray store mutation.
 */

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_DEEP_INDEX 12345UL
#define XARRAY_TEST_CONCURRENT_WORKERS 5
#define XARRAY_TEST_CONCURRENT_ITERATIONS 10000
#define XARRAY_TEST_TIMEOUT_MS 10000

static void xarray_test_destroy(void *data)
{
	xa_destroy(data);
}

static struct xarray *xarray_test_init(struct kunit *test)
{
	struct xarray *xa;

	xa = kunit_kzalloc(test, sizeof(*xa), GFP_KERNEL);
	if (!xa) {
		KUNIT_FAIL(test, "failed to allocate XArray");
		return NULL;
	}

	xa_init(xa);
	if (kunit_add_action_or_reset(test, xarray_test_destroy, xa)) {
		KUNIT_FAIL(test, "failed to register XArray cleanup");
		return NULL;
	}

	return xa;
}

static void xarray_test_store_replace_erase(struct kunit *test)
{
	const unsigned long indexes[] = {
		0, 1, XA_CHUNK_SIZE - 1, XA_CHUNK_SIZE,
		XA_CHUNK_SIZE * XA_CHUNK_SIZE, XARRAY_TEST_DEEP_INDEX,
	};
	void *value_a = xa_mk_value(0x1234);
	void *value_b = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);
	size_t i;

	KUNIT_ASSERT_NOT_NULL(test, xa);

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_NULL(test,
				  xa_store(xa, indexes[i], value_a, GFP_KERNEL));

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, indexes[i]), value_a);

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_PTR_EQ(test,
				    xa_store(xa, indexes[i], value_b, GFP_KERNEL),
				    value_a);

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, indexes[i]), value_b);

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, indexes[i]), value_b);

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_NULL(test, xa_load(xa, indexes[i]));
}

static void xarray_test_reserved_entry(struct kunit *test)
{
	void *value = xa_mk_value(0x2468);
	struct xarray *xa = xarray_test_init(test);
	void *entry;
	XA_STATE(xas, NULL, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, XARRAY_TEST_DEEP_INDEX, GFP_KERNEL),
			0);
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));

	xas.xa = xa;
	xa_lock(xa);
	entry = xas_store(&xas, value);
	xa_unlock(xa);
	KUNIT_EXPECT_TRUE(test, xa_is_zero(entry));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX), value);
}

static void xarray_test_cmpxchg_and_insert(struct kunit *test)
{
	void *value_a = xa_mk_value(0x1111);
	void *value_b = xa_mk_value(0x2222);
	void *value_c = xa_mk_value(0x3333);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value_a,
				   GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test,
			    xa_cmpxchg(xa, XARRAY_TEST_DEEP_INDEX, value_a,
				       value_b, GFP_KERNEL),
			    value_a);
	KUNIT_EXPECT_PTR_EQ(test,
			    xa_cmpxchg(xa, XARRAY_TEST_DEEP_INDEX, value_a,
				       value_c, GFP_KERNEL),
			    value_b);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX), value_b);
	KUNIT_EXPECT_EQ(test,
			xa_insert(xa, XARRAY_TEST_DEEP_INDEX, value_c,
				  GFP_KERNEL),
			-EBUSY);
	KUNIT_EXPECT_EQ(test,
			xa_insert(xa, XARRAY_TEST_DEEP_INDEX + 1, value_c,
				  GFP_KERNEL),
			0);
}

static void xarray_test_marks_follow_store_policy(struct kunit *test)
{
	void *value_a = xa_mk_value(0x4444);
	void *value_b = xa_mk_value(0x5555);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value_a,
				   GFP_KERNEL));
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1));

	KUNIT_EXPECT_PTR_EQ(test,
			    xa_store(xa, XARRAY_TEST_DEEP_INDEX, value_b,
				     GFP_KERNEL),
			    value_a);
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1));

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX), value_b);
	KUNIT_EXPECT_FALSE(test,
			   xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1));
}

#if IS_ENABLED(CONFIG_XARRAY_MULTI)
static void xarray_test_multi_index_replacement(struct kunit *test)
{
	const unsigned long first = XA_CHUNK_SIZE;
	const unsigned long last = XA_CHUNK_SIZE * 2 - 1;
	const unsigned long split = first + 7;
	void *value_a = xa_mk_value(0x6666);
	void *value_b = xa_mk_value(0x7777);
	struct xarray *xa = xarray_test_init(test);
	unsigned long index;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value_a, GFP_KERNEL));
	for (index = first; index <= last; index++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value_a);

	KUNIT_EXPECT_PTR_EQ(test,
			    xa_store(xa, split, value_b, GFP_KERNEL), value_a);
	for (index = first; index <= last; index++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value_b);

	xa_store_range(xa, first, last, NULL, GFP_KERNEL);
	for (index = first; index <= last; index++)
		KUNIT_EXPECT_NULL(test, xa_load(xa, index));
}
#endif

struct xarray_test_worker {
	struct xarray *xa;
	atomic_t *done_count;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *done;
	bool writer;
};

static int xarray_test_worker_fn(void *data)
{
	static const unsigned long indexes[] = {
		XA_CHUNK_SIZE,
		XA_CHUNK_SIZE + 1,
		XA_CHUNK_SIZE * XA_CHUNK_SIZE,
		XARRAY_TEST_DEEP_INDEX,
	};
	struct xarray_test_worker *worker = data;
	void *value_a = xa_mk_value(0x11);
	void *value_b = xa_mk_value(0x22);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < XARRAY_TEST_CONCURRENT_ITERATIONS; i++) {
		unsigned long index = indexes[i % ARRAY_SIZE(indexes)];

		if (worker->writer) {
			void *old;

			if (i & 1)
				old = xa_erase(worker->xa, index);
			else
				old = xa_store(worker->xa, index,
					       (i & 2) ? value_a : value_b,
					       GFP_KERNEL);
			if (xa_err(old) ||
			    (old && old != value_a && old != value_b))
				atomic_inc(worker->failed);
		} else {
			void *entry = xa_load(worker->xa, index);

			if (entry && entry != value_a && entry != value_b)
				atomic_inc(worker->failed);
		}
		if (!(i & 31))
			cond_resched();
	}

	atomic_inc(worker->done_count);
	complete(worker->done);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void xarray_test_concurrent_store(struct kunit *test)
{
	struct xarray_test_worker workers[XARRAY_TEST_CONCURRENT_WORKERS];
	struct task_struct *tasks[XARRAY_TEST_CONCURRENT_WORKERS] = {};
	struct completion done;
	atomic_t done_count = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct xarray *xa;
	unsigned int started = 0;
	unsigned int i;

	xa = xarray_test_init(test);
	KUNIT_ASSERT_NOT_NULL(test, xa);

	init_completion(&done);
	timeout = msecs_to_jiffies(XARRAY_TEST_TIMEOUT_MS);

	for (i = 0; i < XARRAY_TEST_CONCURRENT_WORKERS; i++) {
		workers[i].xa = xa;
		workers[i].done_count = &done_count;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(xarray_test_worker_fn, &workers[i],
				       "xarray-store-%u", i);
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
		if (!wait_for_completion_timeout(&done, timeout)) {
			KUNIT_FAIL(test, "XArray worker timed out");
			break;
		}
	}

	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, XARRAY_TEST_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done_count),
			XARRAY_TEST_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case xarray_store_test_cases[] = {
	KUNIT_CASE(xarray_test_store_replace_erase),
	KUNIT_CASE(xarray_test_reserved_entry),
	KUNIT_CASE(xarray_test_cmpxchg_and_insert),
	KUNIT_CASE(xarray_test_marks_follow_store_policy),
#if IS_ENABLED(CONFIG_XARRAY_MULTI)
	KUNIT_CASE(xarray_test_multi_index_replacement),
#endif
	KUNIT_CASE(xarray_test_concurrent_store),
	{}
};

static struct kunit_suite xarray_store_test_suite = {
	.name = "xarray_store",
	.test_cases = xarray_store_test_cases,
};

kunit_test_suite(xarray_store_test_suite);
