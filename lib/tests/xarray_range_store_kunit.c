// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray range stores and order queries.
 */

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_CONCURRENT_WORKERS 4
#define XARRAY_TEST_CONCURRENT_ITERATIONS 2000
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

static void xarray_test_expect_range(struct kunit *test, struct xarray *xa,
				     unsigned long first, unsigned long last,
				     void *value)
{
	unsigned long index;

	for (index = first; index <= last; index++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value);
}

static void xarray_test_validation(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x1234);
	void *result;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	kunit_warning_suppress(test) {
		result = xa_store_range(xa, 1, 1, XA_RETRY_ENTRY, GFP_KERNEL);
	}
	KUNIT_EXPECT_EQ(test, xa_err(result), -EINVAL);
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));

	result = xa_store_range(xa, 2, 1, value, GFP_KERNEL);
	KUNIT_EXPECT_EQ(test, xa_err(result), -EINVAL);
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
}

static void xarray_test_single_and_direct_root(struct kunit *test)
{
	void *root_value = xa_mk_value(0x2345);
	void *deep_value = xa_mk_value(0x3456);
	const unsigned long deep = 12345;
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_EXPECT_NULL(test,
			  xa_store_range(xa, 0, 0, root_value, GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), root_value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, 1));
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, 0), 0);

	KUNIT_EXPECT_NULL(test,
			  xa_store_range(xa, deep, deep, deep_value, GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, deep), deep_value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, deep - 1));
	KUNIT_EXPECT_NULL(test, xa_load(xa, deep + 1));
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, deep), 0);
}

static void xarray_test_aligned_and_unaligned_ranges(struct kunit *test)
{
	const unsigned long aligned = XA_CHUNK_SIZE;
	const unsigned long unaligned = 3;
	const unsigned long unaligned_last = 10;
	void *aligned_value = xa_mk_value(0x4567);
	void *unaligned_value = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);
	void *result;
	unsigned long index;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	result = xa_store_range(xa, aligned, aligned + XA_CHUNK_SIZE - 1,
				aligned_value, GFP_KERNEL);
	KUNIT_ASSERT_NULL(test, result);
	xarray_test_expect_range(test, xa, aligned,
				 aligned + XA_CHUNK_SIZE - 1, aligned_value);
	for (index = aligned; index < aligned + XA_CHUNK_SIZE; index++)
		KUNIT_EXPECT_EQ(test, xa_get_order(xa, index), XA_CHUNK_SHIFT);

	result = xa_store_range(xa, unaligned, unaligned_last,
				unaligned_value, GFP_KERNEL);
	KUNIT_ASSERT_NULL(test, result);
	xarray_test_expect_range(test, xa, unaligned, unaligned_last,
				 unaligned_value);
	for (index = unaligned; index <= unaligned_last; index++)
		KUNIT_EXPECT_EQ(test, xa_get_order(xa, index), 3);

	KUNIT_EXPECT_NULL(test, xa_load(xa, unaligned_last + 1));
}

static void xarray_test_cross_chunk_and_erase(struct kunit *test)
{
	const unsigned long first = XA_CHUNK_SIZE - 4;
	const unsigned long last = 3 * XA_CHUNK_SIZE + 3;
	void *value = xa_mk_value(0x6789);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));
	xarray_test_expect_range(test, xa, first, last, value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, first - 1));
	KUNIT_EXPECT_NULL(test, xa_load(xa, last + 1));

	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, NULL, GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, first), 0);
}

static void xarray_test_deep_aligned_range(struct kunit *test)
{
	const unsigned int order = 2 * XA_CHUNK_SHIFT + 4;
	const unsigned long first = 1UL << order;
	const unsigned long last = first + (1UL << order) - 1;
	void *value = xa_mk_value(0x789a);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, first), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, first + 17), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, last), value);
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, first), order);
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, first + 17), order);
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, last), order);
}

static void xarray_test_overwrite_existing_multi_index(struct kunit *test)
{
	const unsigned long first = 2 * XA_CHUNK_SIZE;
	const unsigned long last = first + XA_CHUNK_SIZE - 1;
	const unsigned long replace_first = first + 13;
	const unsigned long replace_last = first + 50;
	void *old_value = xa_mk_value(0x89ab);
	void *new_value = xa_mk_value(0x9abc);
	struct xarray *xa = xarray_test_init(test);
	void *result;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, old_value,
					 GFP_KERNEL));
	result = xa_store_range(xa, replace_first, replace_last, new_value,
				GFP_KERNEL);
	KUNIT_ASSERT_NULL(test, result);

	/*
	 * Storing into an existing multi-index entry updates every index
	 * covered by that entry.
	 */
	xarray_test_expect_range(test, xa, first, last, new_value);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, replace_first, replace_last,
					 NULL, GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
}

static void xarray_test_cursor_order(struct kunit *test)
{
	const unsigned long first = 5;
	const unsigned long last = 12;
	void *value = xa_mk_value(0xabcd);
	struct xarray *xa = xarray_test_init(test);
	void *entry;
	XA_STATE(xas, xa, first + 3);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));

	rcu_read_lock();
	entry = xas_load(&xas);
	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, xas_get_order(&xas), 3);
	rcu_read_unlock();

	xas_set(&xas, last + 1);
	rcu_read_lock();
	KUNIT_EXPECT_NULL(test, xas_load(&xas));
	KUNIT_EXPECT_EQ(test, xas_get_order(&xas), 0);
	rcu_read_unlock();
}

static void xarray_test_near_maximum_index(struct kunit *test)
{
	const unsigned long first = ULONG_MAX - XA_CHUNK_SIZE + 1;
	const unsigned long last = ULONG_MAX - 1;
	void *value = xa_mk_value(0xbcde);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));
	xarray_test_expect_range(test, xa, first, last, value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, ULONG_MAX));
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, first), XA_CHUNK_SHIFT);
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, last), XA_CHUNK_SHIFT);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, NULL, GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
}

#if IS_ENABLED(CONFIG_RUST_XARRAY_RANGE_STORE)
static void xarray_test_maximum_single_index(struct kunit *test)
{
	void *value = xa_mk_value(0xcdef);
	struct xarray *xa = xarray_test_init(test);
	void *result;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	result = xa_store_range(xa, ULONG_MAX, ULONG_MAX, value, GFP_KERNEL);
	KUNIT_ASSERT_NULL(test, result);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, ULONG_MAX), value);
	KUNIT_EXPECT_EQ(test, xa_get_order(xa, ULONG_MAX), 0);
	result = xa_store_range(xa, ULONG_MAX, ULONG_MAX, NULL, GFP_KERNEL);
	KUNIT_ASSERT_NULL(test, result);
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
}
#endif

struct xarray_test_worker {
	struct xarray *xa;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int xarray_test_worker_fn(void *data)
{
	const unsigned long first = 4 * XA_CHUNK_SIZE;
	const unsigned long last = first + XA_CHUNK_SIZE - 1;
	struct xarray_test_worker *worker = data;
	void *value_a = xa_mk_value(0x11);
	void *value_b = xa_mk_value(0x22);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < XARRAY_TEST_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			void *old;

			if (!(i & 1))
				old = xa_store_range(worker->xa, first, last,
						     (i & 2) ? value_a : value_b,
						     GFP_KERNEL);
			else
				old = xa_store_range(worker->xa, first, last,
						     NULL, GFP_KERNEL);
			if (xa_err(old))
				atomic_inc(worker->failed);
		} else {
			unsigned long index = first + (i & XA_CHUNK_MASK);
			void *entry = xa_load(worker->xa, index);
			int order = xa_get_order(worker->xa, index);

			if (entry && entry != value_a && entry != value_b)
				atomic_inc(worker->failed);
			if (order < 0 || order > XA_CHUNK_SHIFT)
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

static void xarray_test_concurrent_readers_and_writer(struct kunit *test)
{
	struct xarray_test_worker workers[XARRAY_TEST_CONCURRENT_WORKERS];
	struct task_struct *tasks[XARRAY_TEST_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
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

	init_completion(&complete);
	timeout = msecs_to_jiffies(XARRAY_TEST_TIMEOUT_MS);
	for (i = 0; i < XARRAY_TEST_CONCURRENT_WORKERS; i++) {
		workers[i].xa = xa;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(xarray_test_worker_fn, &workers[i],
				       "xarray-range-%u", i);
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
			KUNIT_FAIL(test, "XArray range worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, XARRAY_TEST_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			XARRAY_TEST_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case xarray_range_store_test_cases[] = {
	KUNIT_CASE(xarray_test_validation),
	KUNIT_CASE(xarray_test_single_and_direct_root),
	KUNIT_CASE(xarray_test_aligned_and_unaligned_ranges),
	KUNIT_CASE(xarray_test_cross_chunk_and_erase),
	KUNIT_CASE(xarray_test_deep_aligned_range),
	KUNIT_CASE(xarray_test_overwrite_existing_multi_index),
	KUNIT_CASE(xarray_test_cursor_order),
	KUNIT_CASE(xarray_test_near_maximum_index),
#if IS_ENABLED(CONFIG_RUST_XARRAY_RANGE_STORE)
	KUNIT_CASE(xarray_test_maximum_single_index),
#endif
	KUNIT_CASE(xarray_test_concurrent_readers_and_writer),
	{}
};

static struct kunit_suite xarray_range_store_test_suite = {
	.name = "xarray_range_store",
	.test_cases = xarray_range_store_test_cases,
};

kunit_test_suite(xarray_range_store_test_suite);
