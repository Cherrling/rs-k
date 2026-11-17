// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for IDR allocation.
 */

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/spinlock.h>

#define IDR_TEST_WORKERS 4
#define IDR_TEST_ALLOCS_PER_WORKER 32
#define IDR_TEST_CONCURRENT_IDS \
	(IDR_TEST_WORKERS * IDR_TEST_ALLOCS_PER_WORKER)
#define IDR_TEST_ALLOC_IDS 256
#define IDR_TEST_TIMEOUT_MS 10000

struct idr_test_context {
	struct idr idr;
	atomic_t markers[IDR_TEST_ALLOC_IDS];
};

struct idr_test_worker {
	struct idr_test_context *ctx;
	atomic_t *done;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *errors;
	unsigned int worker;
};

struct idr_test_threads {
	struct idr_test_context *ctx;
	struct idr_test_worker workers[IDR_TEST_WORKERS];
	struct task_struct *tasks[IDR_TEST_WORKERS];
	atomic_t done;
	atomic_t ready;
	atomic_t start;
	atomic_t errors;
};

static void idr_test_destroy(void *data)
{
	struct idr_test_context *ctx = data;

	idr_destroy(&ctx->idr);
}

static struct idr_test_context *idr_test_init(struct kunit *test, int base)
{
	struct idr_test_context *ctx;
	unsigned int i;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	idr_init_base(&ctx->idr, base);
	for (i = 0; i < ARRAY_SIZE(ctx->markers); i++)
		atomic_set(&ctx->markers[i], 0);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, idr_test_destroy, ctx), 0);

	return ctx;
}

static int idr_test_alloc(struct idr_test_context *ctx, void *ptr,
			  int start, int end)
{
	unsigned long flags;
	int id;

	idr_preload(GFP_KERNEL);
	idr_lock_irqsave(&ctx->idr, flags);
	id = idr_alloc(&ctx->idr, ptr, start, end, GFP_NOWAIT);
	idr_unlock_irqrestore(&ctx->idr, flags);
	idr_preload_end();

	return id;
}

static int idr_test_alloc_u32(struct idr_test_context *ctx, void *ptr,
			      u32 *id, unsigned long max)
{
	unsigned long flags;
	int ret;

	idr_preload(GFP_KERNEL);
	idr_lock_irqsave(&ctx->idr, flags);
	ret = idr_alloc_u32(&ctx->idr, ptr, id, max, GFP_NOWAIT);
	idr_unlock_irqrestore(&ctx->idr, flags);
	idr_preload_end();

	return ret;
}

static int idr_test_alloc_cyclic(struct idr_test_context *ctx, void *ptr,
				 int start, int end)
{
	unsigned long flags;
	int id;

	idr_preload(GFP_KERNEL);
	idr_lock_irqsave(&ctx->idr, flags);
	id = idr_alloc_cyclic(&ctx->idr, ptr, start, end, GFP_NOWAIT);
	idr_unlock_irqrestore(&ctx->idr, flags);
	idr_preload_end();

	return id;
}

static void *idr_test_remove(struct idr_test_context *ctx, unsigned long id)
{
	unsigned long flags;
	void *entry;

	idr_lock_irqsave(&ctx->idr, flags);
	entry = idr_remove(&ctx->idr, id);
	idr_unlock_irqrestore(&ctx->idr, flags);

	return entry;
}

static void *idr_test_find(struct idr_test_context *ctx, unsigned long id)
{
	unsigned long flags;
	void *entry;

	idr_lock_irqsave(&ctx->idr, flags);
	entry = idr_find(&ctx->idr, id);
	idr_unlock_irqrestore(&ctx->idr, flags);

	return entry;
}

static void idr_test_first_fit_holes(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 0);
	u32 entries[] = { 1, 2, 3 };

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test, idr_test_alloc(ctx, &entries[0], 0, 0), 0);
	KUNIT_ASSERT_EQ(test, idr_test_alloc(ctx, &entries[1], 0, 0), 1);
	KUNIT_ASSERT_EQ(test, idr_test_alloc(ctx, &entries[2], 0, 0), 2);

	KUNIT_EXPECT_PTR_EQ(test, idr_test_remove(ctx, 1), &entries[1]);
	KUNIT_EXPECT_NULL(test, idr_test_find(ctx, 1));
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, &entries[1], 0, 0), 1);
	KUNIT_EXPECT_PTR_EQ(test, idr_test_find(ctx, 0), &entries[0]);
	KUNIT_EXPECT_PTR_EQ(test, idr_test_find(ctx, 1), &entries[1]);
	KUNIT_EXPECT_PTR_EQ(test, idr_test_find(ctx, 2), &entries[2]);
}

static void idr_test_base_and_exhaustion(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 5);
	u32 id = 20;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, ctx, 0, 0), 5);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, ctx, 0, 0), 6);

	KUNIT_EXPECT_EQ(test, idr_test_alloc_u32(ctx, ctx, &id, 6), -ENOSPC);
	KUNIT_EXPECT_EQ(test, id, 20);
	id = 0;
	KUNIT_EXPECT_EQ(test, idr_test_alloc_u32(ctx, ctx, &id, 7), 0);
	KUNIT_EXPECT_EQ(test, id, 7);

	KUNIT_EXPECT_EQ(test, idr_test_alloc_u32(ctx, ctx, &id, 7), -ENOSPC);
	KUNIT_EXPECT_EQ(test, id, 7);
}

static void idr_test_null_entries(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 0);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, NULL, 0, 0), 0);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, NULL, 0, 0), 1);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, NULL, 0, 0), 2);

	KUNIT_EXPECT_NULL(test, idr_test_remove(ctx, 0));
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, ctx, 0, 0), 0);
	KUNIT_EXPECT_PTR_EQ(test, idr_test_find(ctx, 0), ctx);
}

static void idr_test_zero_end_range(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 5);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, ctx, 5, 0), 5);
	KUNIT_EXPECT_EQ(test, idr_test_alloc(ctx, ctx, 5, 0), 6);
}

static void idr_test_cyclic_wrap(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 2);

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_EXPECT_EQ(test, idr_test_alloc_cyclic(ctx, ctx, 2, 6), 2);
	KUNIT_EXPECT_EQ(test, idr_get_cursor(&ctx->idr), 3);
	KUNIT_EXPECT_EQ(test, idr_test_alloc_cyclic(ctx, ctx, 2, 6), 3);
	KUNIT_EXPECT_EQ(test, idr_get_cursor(&ctx->idr), 4);

	KUNIT_EXPECT_PTR_EQ(test, idr_test_remove(ctx, 2), ctx);
	KUNIT_EXPECT_EQ(test, idr_test_alloc_cyclic(ctx, ctx, 2, 6), 4);
	KUNIT_EXPECT_EQ(test, idr_test_alloc_cyclic(ctx, ctx, 2, 6), 5);
	KUNIT_EXPECT_EQ(test, idr_test_alloc_cyclic(ctx, ctx, 2, 6), 2);
	KUNIT_EXPECT_EQ(test, idr_get_cursor(&ctx->idr), 3);
}

static void idr_test_many_allocations(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 0);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	for (i = 0; i < IDR_TEST_ALLOC_IDS; i++) {
		int id;

		id = idr_test_alloc(ctx, ctx, 0, IDR_TEST_ALLOC_IDS);
		KUNIT_ASSERT_EQ(test, id, i);
		KUNIT_ASSERT_EQ(test, atomic_read(&ctx->markers[id]), 0);
		atomic_set(&ctx->markers[id], 1);
	}

	KUNIT_EXPECT_EQ(test,
			idr_test_alloc(ctx, ctx, 0, IDR_TEST_ALLOC_IDS), -ENOSPC);
}

static void idr_test_stop_threads(void *data)
{
	struct idr_test_threads *threads = data;
	unsigned int i;

	atomic_set(&threads->start, 1);
	for (i = 0; i < IDR_TEST_WORKERS; i++)
		if (threads->tasks[i])
			kthread_stop(threads->tasks[i]);
}

static int idr_test_worker_fn(void *data)
{
	struct idr_test_worker *worker = data;
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < IDR_TEST_ALLOCS_PER_WORKER; i++) {
		unsigned long flags;
		int id;

		idr_preload(GFP_KERNEL);
		idr_lock_irqsave(&worker->ctx->idr, flags);
		id = idr_alloc(&worker->ctx->idr, worker, 0,
			       IDR_TEST_CONCURRENT_IDS, GFP_NOWAIT);
		idr_unlock_irqrestore(&worker->ctx->idr, flags);
		idr_preload_end();
		if (id < 0 || id >= IDR_TEST_CONCURRENT_IDS ||
		    atomic_cmpxchg(&worker->ctx->markers[id], 0, 1))
			atomic_inc(worker->errors);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void idr_test_concurrent_alloc(struct kunit *test)
{
	struct idr_test_context *ctx = idr_test_init(test, 0);
	struct idr_test_threads *threads;
	unsigned long deadline;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	threads = kunit_kzalloc(test, sizeof(*threads), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, threads);

	threads->ctx = ctx;
	atomic_set(&threads->done, 0);
	atomic_set(&threads->ready, 0);
	atomic_set(&threads->start, 0);
	atomic_set(&threads->errors, 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, idr_test_stop_threads,
						  threads), 0);

	for (i = 0; i < IDR_TEST_WORKERS; i++) {
		threads->workers[i] = (struct idr_test_worker) {
			.ctx = ctx,
			.done = &threads->done,
			.ready = &threads->ready,
			.start = &threads->start,
			.errors = &threads->errors,
			.worker = i,
		};
		threads->tasks[i] = kthread_run(idr_test_worker_fn,
						&threads->workers[i],
						"idr-kunit-%u", i);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, threads->tasks[i]);
	}

	deadline = jiffies + msecs_to_jiffies(IDR_TEST_TIMEOUT_MS);
	while (atomic_read(&threads->ready) != IDR_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	atomic_set(&threads->start, 1);
	KUNIT_ASSERT_EQ(test, atomic_read(&threads->ready), IDR_TEST_WORKERS);

	deadline = jiffies + msecs_to_jiffies(IDR_TEST_TIMEOUT_MS);
	while (atomic_read(&threads->done) != IDR_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	for (i = 0; i < IDR_TEST_WORKERS; i++) {
		kthread_stop(threads->tasks[i]);
		threads->tasks[i] = NULL;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&threads->done), IDR_TEST_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&threads->errors), 0);
	for (i = 0; i < IDR_TEST_CONCURRENT_IDS; i++)
		KUNIT_EXPECT_EQ(test, atomic_read(&threads->ctx->markers[i]), 1);
}

static struct kunit_case idr_alloc_test_cases[] = {
	KUNIT_CASE(idr_test_first_fit_holes),
	KUNIT_CASE(idr_test_base_and_exhaustion),
	KUNIT_CASE(idr_test_null_entries),
	KUNIT_CASE(idr_test_zero_end_range),
	KUNIT_CASE(idr_test_cyclic_wrap),
	KUNIT_CASE(idr_test_many_allocations),
	KUNIT_CASE(idr_test_concurrent_alloc),
	{}
};

static struct kunit_suite idr_alloc_test_suite = {
	.name = "idr_alloc",
	.test_cases = idr_alloc_test_cases,
};

kunit_test_suite(idr_alloc_test_suite);
