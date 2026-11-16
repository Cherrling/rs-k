// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray ID allocation.
 */

#include <kunit/test.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_ALLOC_WORKERS 4
#define XARRAY_TEST_ALLOCS_PER_WORKER 32
#define XARRAY_TEST_ALLOC_IDS \
	(XARRAY_TEST_ALLOC_WORKERS * XARRAY_TEST_ALLOCS_PER_WORKER)
#define XARRAY_TEST_TIMEOUT_MS 10000

struct xarray_test_alloc_worker {
	struct xarray *xa;
	atomic_t *claimed;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
};

static void xarray_test_destroy(void *data)
{
	xa_destroy(data);
}

static struct xarray *xarray_test_init(struct kunit *test, gfp_t flags)
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

static int xarray_test_alloc_cyclic_locked(struct xarray *xa, u32 *id,
					   void *entry, struct xa_limit limit,
					   u32 *next)
{
	int ret;

	xa_lock(xa);
	ret = __xa_alloc_cyclic(xa, id, entry, limit, next, GFP_KERNEL);
	xa_unlock(xa);

	return ret;
}

static void xarray_test_first_fit_and_holes(struct kunit *test)
{
	const struct xa_limit limit = XA_LIMIT(2, 9);
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	u32 id;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 2);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 3);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 4);

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, 3), value);
	KUNIT_EXPECT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 3);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 2), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 3), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 4), value);
}

static void xarray_test_reserved_entries(struct kunit *test)
{
	const struct xa_limit limit = XA_LIMIT(0, 3);
	struct xarray *xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	u32 id;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, NULL, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 0);
	KUNIT_EXPECT_FALSE(test, xa_empty(xa));
	KUNIT_EXPECT_NULL(test, xa_load(xa, id));

	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, NULL, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 1);
	KUNIT_EXPECT_NULL(test, xa_load(xa, id));

	KUNIT_EXPECT_NULL(test, xa_erase(xa, 0));
	KUNIT_ASSERT_EQ(test,
			xa_alloc(xa, &id, NULL, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 0);
}

static void xarray_test_range_exhaustion(struct kunit *test)
{
	const struct xa_limit limit = XA_LIMIT(8, 10);
	void *value = xa_mk_value(0x2345);
	struct xarray *xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	u32 id = U32_MAX;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < 3; i++) {
		KUNIT_ASSERT_EQ(test,
				xa_alloc(xa, &id, value, limit, GFP_KERNEL),
				0);
		KUNIT_EXPECT_EQ(test, id, 8 + i);
	}

	id = U32_MAX;
	KUNIT_EXPECT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL),
			-EBUSY);
	KUNIT_EXPECT_EQ(test, id, U32_MAX);

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, 9), value);
	KUNIT_EXPECT_EQ(test,
			xa_alloc(xa, &id, value, limit, GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, id, 9);
}

static void xarray_test_cyclic_retry_from_minimum(struct kunit *test)
{
	const struct xa_limit limit = XA_LIMIT(2, 5);
	void *value = xa_mk_value(0x3456);
	struct xarray *xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	u32 id;
	u32 next;
	unsigned int i;
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < 4; i++) {
		next = 2;
		ret = xarray_test_alloc_cyclic_locked(xa, &id, value, limit,
						      &next);
		KUNIT_ASSERT_EQ(test, ret, 0);
		KUNIT_EXPECT_EQ(test, id, 2 + i);
		KUNIT_EXPECT_EQ(test, next, 3 + i);
	}

	KUNIT_ASSERT_PTR_EQ(test, xa_erase(xa, 2), value);
	next = 3;
	ret = xarray_test_alloc_cyclic_locked(xa, &id, value, limit, &next);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, id, 2);
	KUNIT_EXPECT_EQ(test, next, 3);

	next = 3;
	id = U32_MAX;
	ret = xarray_test_alloc_cyclic_locked(xa, &id, value, limit, &next);
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	KUNIT_EXPECT_EQ(test, id, U32_MAX);
}

static void xarray_test_maximum_id_notification(struct kunit *test)
{
	const struct xa_limit max_limit = XA_LIMIT(U32_MAX, U32_MAX);
	const struct xa_limit low_limit = XA_LIMIT(1, 3);
	void *value = xa_mk_value(0x4567);
	struct xarray *xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	u32 id;
	u32 next = U32_MAX;
	int ret;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	ret = xarray_test_alloc_cyclic_locked(xa, &id, value, max_limit,
					      &next);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, id, U32_MAX);
	KUNIT_EXPECT_EQ(test, next, 0);
	KUNIT_EXPECT_TRUE(test, xa->xa_flags & XA_FLAGS_ALLOC_WRAPPED);

	next = 0;
	ret = xarray_test_alloc_cyclic_locked(xa, &id, value, low_limit,
					      &next);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, id, 1);
	KUNIT_EXPECT_EQ(test, next, 2);
	KUNIT_EXPECT_FALSE(test, xa->xa_flags & XA_FLAGS_ALLOC_WRAPPED);
}

static int xarray_test_alloc_worker_fn(void *data)
{
	struct xarray_test_alloc_worker *worker = data;
	const struct xa_limit limit = XA_LIMIT(0, XARRAY_TEST_ALLOC_IDS - 1);
	void *value = xa_mk_value(0x5678);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < XARRAY_TEST_ALLOCS_PER_WORKER; i++) {
		u32 id;

		if (xa_alloc(worker->xa, &id, value, limit, GFP_KERNEL)) {
			atomic_inc(worker->failed);
			continue;
		}
		if (atomic_cmpxchg(&worker->claimed[id], 0, 1))
			atomic_inc(worker->failed);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void xarray_test_concurrent_allocation(struct kunit *test)
{
	struct xarray_test_alloc_worker workers[XARRAY_TEST_ALLOC_WORKERS] = {};
	struct task_struct *tasks[XARRAY_TEST_ALLOC_WORKERS] = {};
	atomic_t claimed[XARRAY_TEST_ALLOC_IDS];
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	unsigned long deadline;
	struct xarray *xa;
	unsigned int started = 0;
	unsigned int i;

	xa = xarray_test_init(test, XA_FLAGS_ALLOC);
	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < ARRAY_SIZE(claimed); i++)
		atomic_set(&claimed[i], 0);

	for (i = 0; i < XARRAY_TEST_ALLOC_WORKERS; i++) {
		workers[i].xa = xa;
		workers[i].claimed = claimed;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		tasks[i] = kthread_run(xarray_test_alloc_worker_fn,
				       &workers[i], "xarray-alloc-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	if (started != XARRAY_TEST_ALLOC_WORKERS) {
		atomic_set(&start, 1);
		for (i = 0; i < started; i++)
			kthread_stop(tasks[i]);
		KUNIT_FAIL(test, "failed to start all allocation workers");
		return;
	}
	deadline = jiffies + msecs_to_jiffies(XARRAY_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) != XARRAY_TEST_ALLOC_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	atomic_set(&start, 1);
	if (atomic_read(&ready) != XARRAY_TEST_ALLOC_WORKERS) {
		for (i = 0; i < started; i++)
			kthread_stop(tasks[i]);
		KUNIT_FAIL(test, "allocation workers did not become ready");
		return;
	}

	while (atomic_read(&done) != XARRAY_TEST_ALLOC_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();

	KUNIT_EXPECT_EQ(test, atomic_read(&done), XARRAY_TEST_ALLOC_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
	for (i = 0; i < ARRAY_SIZE(claimed); i++)
		KUNIT_EXPECT_EQ(test, atomic_read(&claimed[i]), 1);

	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);
}

static struct kunit_case xarray_alloc_test_cases[] = {
	KUNIT_CASE(xarray_test_first_fit_and_holes),
	KUNIT_CASE(xarray_test_reserved_entries),
	KUNIT_CASE(xarray_test_range_exhaustion),
	KUNIT_CASE(xarray_test_cyclic_retry_from_minimum),
	KUNIT_CASE(xarray_test_maximum_id_notification),
	KUNIT_CASE(xarray_test_concurrent_allocation),
	{}
};

static struct kunit_suite xarray_alloc_test_suite = {
	.name = "xarray_alloc",
	.test_cases = xarray_alloc_test_cases,
};

kunit_test_suite(xarray_alloc_test_suite);
