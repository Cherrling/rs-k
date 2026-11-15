// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray mark lifecycle.
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

static void xarray_test_parent_propagation(struct kunit *test)
{
	const unsigned long first_branch = XA_CHUNK_SIZE * XA_CHUNK_SIZE + 1;
	const unsigned long second_branch = XARRAY_TEST_DEEP_INDEX;
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, first_branch, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, second_branch, value, GFP_KERNEL));

	xa_set_mark(xa, first_branch, XA_MARK_1);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first_branch, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, second_branch, XA_MARK_1));

	xa_set_mark(xa, second_branch, XA_MARK_1);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, second_branch, XA_MARK_1));

	xa_clear_mark(xa, first_branch, XA_MARK_1);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, first_branch, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, second_branch, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_1));

	xa_clear_mark(xa, second_branch, XA_MARK_1);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, second_branch, XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
}

static void xarray_test_mark_independence(struct kunit *test)
{
	void *value = xa_mk_value(0x2345);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));

	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2);
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0));
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2));

	xa_clear_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test,
			   xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test,
			  xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_2));
}

static void xarray_test_invalid_cursor(struct kunit *test)
{
	void *value = xa_mk_value(0x3456);
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, NULL, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2);

	xas.xa = xa;
	KUNIT_EXPECT_FALSE(test, xas_get_mark(&xas, XA_MARK_2));

	xas_set_mark(&xas, XA_MARK_1);
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));

	xas_clear_mark(&xas, XA_MARK_2);
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_2));
	xas_init_marks(&xas);
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_2));

	xas_set_err(&xas, -EINVAL);
	KUNIT_EXPECT_FALSE(test, xas_get_mark(&xas, XA_MARK_2));
	xas_set_mark(&xas, XA_MARK_1);
	xas_clear_mark(&xas, XA_MARK_2);
	xas_init_marks(&xas);
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_2));
}

static void xarray_test_marks_init(struct kunit *test)
{
	void *value = xa_mk_value(0x4567);
	struct xarray *xa = xarray_test_init(test);
	struct xarray *allocated;
	XA_STATE(xas, NULL, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2);

	xas.xa = xa;
	xa_lock(xa);
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	xas_init_marks(&xas);
	xa_unlock(xa);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_2));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_2));

	allocated = xarray_test_init_flags(test, XA_FLAGS_ALLOC);
	KUNIT_ASSERT_NOT_NULL(test, allocated);
	KUNIT_ASSERT_NULL(test,
			  xa_store(allocated, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));
	xa_set_mark(allocated, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	xa_set_mark(allocated, XARRAY_TEST_DEEP_INDEX, XA_MARK_2);

	xas_reset(&xas);
	xas.xa = allocated;
	xa_lock(allocated);
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	xas_init_marks(&xas);
	xa_unlock(allocated);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(allocated, XARRAY_TEST_DEEP_INDEX,
					    XA_FREE_MARK));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(allocated, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(allocated, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_2));
}

static void xarray_test_free_mark_lifecycle(struct kunit *test)
{
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC);
	void *value_a = xa_mk_value(0x5678);
	void *value_b = xa_mk_value(0x6789);
	u32 id_a;
	u32 id_b;
	u32 reused;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id_a, value_a, XA_LIMIT(0, 64),
				 GFP_KERNEL),
			0);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id_b, value_b, XA_LIMIT(0, 64),
				 GFP_KERNEL),
			0);
	KUNIT_EXPECT_NE(test, id_a, id_b);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, id_a, XA_FREE_MARK));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, id_b, XA_FREE_MARK));

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, id_a), value_a);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, id_a, XA_FREE_MARK));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, id_b, XA_FREE_MARK));

	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &reused, value_a, XA_LIMIT(0, 64),
				 GFP_KERNEL),
			0);
	KUNIT_EXPECT_EQ(test, reused, id_a);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, reused, XA_FREE_MARK));
}

static void xarray_test_erase_clears_marks(struct kunit *test)
{
	void *value = xa_mk_value(0x789a);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_1);
	xa_set_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_2);

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX), value);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX,
					     XA_MARK_2));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_2));
}

#if IS_ENABLED(CONFIG_XARRAY_MULTI)
static void xarray_test_multi_index_marks(struct kunit *test)
{
	const unsigned long first = XA_CHUNK_SIZE;
	const unsigned long last = XA_CHUNK_SIZE * 2 - 1;
	void *value = xa_mk_value(0x89ab);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));

	xa_set_mark(xa, first + 7, XA_MARK_2);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_2));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first + 7, XA_MARK_2));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, last, XA_MARK_2));

	xa_clear_mark(xa, last, XA_MARK_2);
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, first, XA_MARK_2));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, first + 7, XA_MARK_2));
	KUNIT_EXPECT_FALSE(test, xa_get_mark(xa, last, XA_MARK_2));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_2));
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
			if (i & 1)
				xa_clear_mark(worker->xa, index, XA_MARK_2);
			else
				xa_set_mark(worker->xa, index, XA_MARK_2);

			if (!(i & 15)) {
				void *old;

				if (i & 16)
					old = xa_erase(worker->xa, index);
				else
					old = xa_store(worker->xa, index,
						       (i & 2) ? value_a : value_b,
						       GFP_KERNEL);
				if (xa_err(old) ||
				    (old && old != value_a && old != value_b))
					atomic_inc(worker->failed);
			}
		} else {
			unsigned int mark;

			for (mark = XA_MARK_0; mark <= XA_MARK_2; mark++)
				(void)xa_get_mark(worker->xa, index, mark);
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

static void xarray_test_concurrent_marks(struct kunit *test)
{
	struct xarray_test_worker workers[XARRAY_TEST_CONCURRENT_WORKERS];
	struct task_struct *tasks[XARRAY_TEST_CONCURRENT_WORKERS] = {};
	static const unsigned long indexes[] = {
		XA_CHUNK_SIZE,
		XA_CHUNK_SIZE + 1,
		XA_CHUNK_SIZE * XA_CHUNK_SIZE,
		XARRAY_TEST_DEEP_INDEX,
	};
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
	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, indexes[i],
					   xa_mk_value((i & 1) ? 0x22 : 0x11),
					   GFP_KERNEL));

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
				       "xarray-marks-%u", i);
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
			KUNIT_FAIL(test, "XArray mark worker timed out");
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

static struct kunit_case xarray_marks_test_cases[] = {
	KUNIT_CASE(xarray_test_parent_propagation),
	KUNIT_CASE(xarray_test_mark_independence),
	KUNIT_CASE(xarray_test_invalid_cursor),
	KUNIT_CASE(xarray_test_marks_init),
	KUNIT_CASE(xarray_test_free_mark_lifecycle),
	KUNIT_CASE(xarray_test_erase_clears_marks),
#if IS_ENABLED(CONFIG_XARRAY_MULTI)
	KUNIT_CASE(xarray_test_multi_index_marks),
#endif
	KUNIT_CASE(xarray_test_concurrent_marks),
	{}
};

static struct kunit_suite xarray_marks_test_suite = {
	.name = "xarray_marks",
	.test_cases = xarray_marks_test_cases,
};

kunit_test_suite(xarray_marks_test_suite);
