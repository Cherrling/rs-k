// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray load traversal.
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

static void xarray_test_empty_and_direct_head(struct kunit *test)
{
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));

	KUNIT_EXPECT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, 1));
}

static void xarray_test_deep_lookup(struct kunit *test)
{
	const unsigned long indexes[] = {
		0, 1, XA_CHUNK_SIZE - 1, XA_CHUNK_SIZE,
		XA_CHUNK_SIZE * XA_CHUNK_SIZE,
		XARRAY_TEST_DEEP_INDEX,
	};
	void *values[ARRAY_SIZE(indexes)];
	struct xarray *xa = xarray_test_init(test);
	size_t i;

	KUNIT_ASSERT_NOT_NULL(test, xa);

	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		values[i] = xa_mk_value(i + 1);
		KUNIT_EXPECT_NULL(test,
				  xa_store(xa, indexes[i], values[i], GFP_KERNEL));
	}

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, indexes[i]), values[i]);

	KUNIT_EXPECT_NULL(test, xa_load(xa, indexes[ARRAY_SIZE(indexes) - 1] + 1));
}

static void xarray_test_advanced_load_and_reload(struct kunit *test)
{
	void *value = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);
	void *entry;
	XA_STATE(xas, NULL, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, XARRAY_TEST_DEEP_INDEX, value, GFP_KERNEL));

	xas.xa = xa;
	rcu_read_lock();
	entry = xas_load(&xas);
	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_NOT_NULL(test, xas.xa_node);
	KUNIT_EXPECT_PTR_EQ(test, xas_reload(&xas), value);

	xas.xa_index = 0;
	xas_reset(&xas);
	KUNIT_EXPECT_NULL(test, xas_load(&xas));
	rcu_read_unlock();
}

static void xarray_test_zero_entry_is_invisible_to_basic_load(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry;
	XA_STATE(xas, NULL, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, XARRAY_TEST_DEEP_INDEX, GFP_KERNEL),
			0);
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));

	xas.xa = xa;
	rcu_read_lock();
	entry = xas_load(&xas);
	KUNIT_EXPECT_TRUE(test, xa_is_zero(entry));
	rcu_read_unlock();
}

#if IS_ENABLED(CONFIG_XARRAY_MULTI)
static void xarray_test_multi_index_siblings(struct kunit *test)
{
	const unsigned long first = XA_CHUNK_SIZE;
	const unsigned long last = XA_CHUNK_SIZE * 2 - 1;
	void *value = xa_mk_value(0x9876);
	struct xarray *xa = xarray_test_init(test);
	unsigned long index;
	XA_STATE(xas, NULL, first + 3);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store_range(xa, first, last, value,
					       GFP_KERNEL));

	for (index = first; index <= last; index++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value);

	xas.xa = xa;
	rcu_read_lock();
	KUNIT_EXPECT_PTR_EQ(test, xas_load(&xas), value);
	KUNIT_EXPECT_PTR_EQ(test, xas_reload(&xas), value);
	rcu_read_unlock();
}
#endif

struct xarray_test_worker {
	struct xarray *xa;
	atomic_t *done_count;
	atomic_t *failed;
	atomic_t *values_seen;
	atomic_t *ready;
	atomic_t *start;
	struct completion *done;
	bool writer;
};

static int xarray_test_worker_fn(void *data)
{
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

			if (i & 1) {
				old = xa_erase(worker->xa,
					       XARRAY_TEST_DEEP_INDEX);
			} else {
				old = xa_store(worker->xa,
					       XARRAY_TEST_DEEP_INDEX,
					       (i & 2) ? value_a : value_b,
					       GFP_KERNEL);
			}
			if (xa_err(old) ||
			    (old && old != value_a && old != value_b))
				atomic_inc(worker->failed);
		} else {
			void *entry = xa_load(worker->xa, XARRAY_TEST_DEEP_INDEX);

			if (entry && entry != value_a && entry != value_b)
				atomic_inc(worker->failed);
			else if (entry)
				atomic_inc(worker->values_seen);
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

static void xarray_test_concurrent_load_and_store(struct kunit *test)
{
	struct xarray_test_worker workers[XARRAY_TEST_CONCURRENT_WORKERS];
	struct task_struct *tasks[XARRAY_TEST_CONCURRENT_WORKERS] = {};
	void *value_a = xa_mk_value(0x11);
	struct completion done;
	atomic_t done_count = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t values_seen = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned long timeout;
	struct xarray *xa;
	unsigned int started = 0;
	unsigned int i;

	xa = xarray_test_init(test);
	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value_a, GFP_KERNEL));

	init_completion(&done);
	timeout = msecs_to_jiffies(XARRAY_TEST_TIMEOUT_MS);

	for (i = 0; i < XARRAY_TEST_CONCURRENT_WORKERS; i++) {
		workers[i].xa = xa;
		workers[i].done_count = &done_count;
		workers[i].failed = &failed;
		workers[i].values_seen = &values_seen;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(xarray_test_worker_fn, &workers[i],
				       "xarray-rust-%u", i);
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
	KUNIT_EXPECT_GT(test, atomic_read(&values_seen), 0);
}

static struct kunit_case xarray_rust_test_cases[] = {
	KUNIT_CASE(xarray_test_empty_and_direct_head),
	KUNIT_CASE(xarray_test_deep_lookup),
	KUNIT_CASE(xarray_test_advanced_load_and_reload),
	KUNIT_CASE(xarray_test_zero_entry_is_invisible_to_basic_load),
#if IS_ENABLED(CONFIG_XARRAY_MULTI)
	KUNIT_CASE(xarray_test_multi_index_siblings),
#endif
	KUNIT_CASE(xarray_test_concurrent_load_and_store),
	{}
};

static struct kunit_suite xarray_rust_test_suite = {
	.name = "xarray_load",
	.test_cases = xarray_rust_test_cases,
};

kunit_test_suite(xarray_rust_test_suite);
