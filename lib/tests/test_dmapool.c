// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-map-ops.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/slab.h>

struct dmapool_test_ctx {
	struct device *dev;
	u64 dma_mask;
};

struct dmapool_worker {
	struct dma_pool *pool;
	atomic_t failures;
	atomic_t *started;
	int participants;
	struct completion done;
};

static void dmapool_test_device_unregister(void *data)
{
	root_device_unregister(data);
}

static void dmapool_test_destroy_pool(void *data)
{
	dma_pool_destroy(data);
}

static int dmapool_test_init(struct kunit *test)
{
	struct dmapool_test_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);
	test->priv = ctx;

	ctx->dev = root_device_register("dmapool-kunit");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->dev);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_device_unregister,
						  ctx->dev),
			0);

	set_dma_ops(ctx->dev, NULL);
	ctx->dev->dma_mask = &ctx->dma_mask;
	ret = dma_set_mask_and_coherent(ctx->dev, DMA_BIT_MASK(64));
	KUNIT_ASSERT_EQ(test, ret, 0);

	return 0;
}

static void dmapool_test_create_validation(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dma_pool *pool;

	KUNIT_EXPECT_NULL(test, dma_pool_create(NULL, NULL, 16, 16, 0));
	KUNIT_EXPECT_NULL(test, dma_pool_create("pool", ctx->dev, 0, 16, 0));
	KUNIT_EXPECT_NULL(test, dma_pool_create("pool", ctx->dev,
						(size_t)INT_MAX + 1, 16, 0));
	KUNIT_EXPECT_NULL(test, dma_pool_create("pool", ctx->dev, 16, 3, 0));
	KUNIT_EXPECT_NULL(test, dma_pool_create("pool", ctx->dev, 68, 32,
						PAGE_SIZE / 2 + 1));
	KUNIT_EXPECT_NULL(test, dma_pool_create("pool", ctx->dev, 128, 32, 64));

	pool = dma_pool_create("align-zero", ctx->dev, 16, 0, 0);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_destroy_pool,
						  pool),
			0);
}

static void dmapool_test_alloc_free(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dma_pool *pool;
	dma_addr_t dma;
	void *block;

	pool = dma_pool_create("alloc-free", ctx->dev, 16, 16, 0);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_destroy_pool,
						  pool),
			0);

	block = dma_pool_alloc(pool, GFP_KERNEL, &dma);
	KUNIT_ASSERT_NOT_NULL(test, block);
	KUNIT_EXPECT_EQ_MSG(test, (unsigned long)block & 15, 0,
			    "pool block is not aligned");
	KUNIT_EXPECT_EQ_MSG(test, dma & 15, 0,
			    "DMA address is not aligned");
	memset(block, 0xa5, 16);
	dma_pool_free(pool, block, dma);

	block = dma_pool_zalloc(pool, GFP_KERNEL, &dma);
	KUNIT_ASSERT_NOT_NULL(test, block);
	KUNIT_EXPECT_EQ(test, ((u8 *)block)[0], 0);
	KUNIT_EXPECT_EQ(test, ((u8 *)block)[15], 0);
	dma_pool_free(pool, block, dma);
}

static void dmapool_test_boundaries(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dma_pool *pool;
	dma_addr_t *dmas;
	void **blocks;
	int i;

	dmas = kunit_kcalloc(test, 64, sizeof(*dmas), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmas);
	blocks = kunit_kcalloc(test, 64, sizeof(*blocks), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, blocks);

	pool = dma_pool_create("boundary", ctx->dev, 68, 32, PAGE_SIZE);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_destroy_pool,
						  pool),
			0);

	for (i = 0; i < 64; i++) {
		blocks[i] = dma_pool_alloc(pool, GFP_KERNEL, &dmas[i]);
		KUNIT_ASSERT_NOT_NULL(test, blocks[i]);
		KUNIT_EXPECT_EQ(test, (unsigned long)blocks[i] & 31, 0);
		KUNIT_EXPECT_LE(test, (dmas[i] & (PAGE_SIZE - 1)) + 68,
				PAGE_SIZE);
	}

	for (i = 0; i < 64; i++)
		dma_pool_free(pool, blocks[i], dmas[i]);
}

static void dmapool_test_page_growth(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dma_pool *pool;
	dma_addr_t *dmas;
	void **blocks;
	int i;

	dmas = kunit_kcalloc(test, 100, sizeof(*dmas), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dmas);
	blocks = kunit_kcalloc(test, 100, sizeof(*blocks), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, blocks);

	pool = dma_pool_create("growth", ctx->dev, 256, 256, 0);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_destroy_pool,
						  pool),
			0);

	for (i = 0; i < 100; i++) {
		blocks[i] = dma_pool_alloc(pool, GFP_KERNEL, &dmas[i]);
		KUNIT_ASSERT_NOT_NULL(test, blocks[i]);
	}

	for (i = 0; i < 100; i++)
		dma_pool_free(pool, blocks[i], dmas[i]);
}

static void dmapool_test_managed(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dma_pool *pool;
	dma_addr_t dma;
	void *block;

	pool = dmam_pool_create("managed", ctx->dev, 64, 64, 0);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	block = dma_pool_alloc(pool, GFP_KERNEL, &dma);
	KUNIT_ASSERT_NOT_NULL(test, block);
	dma_pool_free(pool, block, dma);
	dmam_pool_destroy(pool);
}

static int dmapool_worker_thread(void *data)
{
	struct dmapool_worker *worker = data;
	int i;

	atomic_inc(worker->started);
	while (atomic_read(worker->started) < worker->participants &&
	       !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < 1000; i++) {
		dma_addr_t dma;
		void *block;

		block = dma_pool_alloc(worker->pool, GFP_KERNEL, &dma);
		if (!block) {
			atomic_inc(&worker->failures);
			break;
		}
		memset(block, i, 32);
		dma_pool_free(worker->pool, block, dma);
		cond_resched();
	}

	complete(&worker->done);
	while (!kthread_should_stop())
		schedule_timeout_uninterruptible(1);
	return 0;
}

KUNIT_DEFINE_ACTION_WRAPPER(dmapool_test_stop_thread, kthread_stop,
			    struct task_struct *);

static void dmapool_test_concurrent(struct kunit *test)
{
	struct dmapool_test_ctx *ctx = test->priv;
	struct dmapool_worker workers[4];
	struct dma_pool *pool;
	struct task_struct *tasks[ARRAY_SIZE(workers)];
	atomic_t started;
	unsigned long timeout;
	int i;

	pool = dma_pool_create("concurrent", ctx->dev, 32, 32, 0);
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, dmapool_test_destroy_pool,
						  pool),
			0);

	atomic_set(&started, 0);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		workers[i].pool = pool;
		atomic_set(&workers[i].failures, 0);
		workers[i].started = &started;
		workers[i].participants = ARRAY_SIZE(workers);
		init_completion(&workers[i].done);
		tasks[i] = kthread_run(dmapool_worker_thread, &workers[i],
				       "dmapool-%d", i);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, tasks[i]);
		KUNIT_ASSERT_EQ(test,
				kunit_add_action_or_reset(test,
							  dmapool_test_stop_thread,
							  tasks[i]),
				0);
	}

	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		timeout = wait_for_completion_timeout(&workers[i].done, 10 * HZ);
		KUNIT_ASSERT_NE(test, timeout, 0);
		KUNIT_EXPECT_EQ(test, atomic_read(&workers[i].failures), 0);
	}
}

static struct kunit_case dmapool_test_cases[] = {
	KUNIT_CASE(dmapool_test_create_validation),
	KUNIT_CASE(dmapool_test_alloc_free),
	KUNIT_CASE(dmapool_test_boundaries),
	KUNIT_CASE(dmapool_test_page_growth),
	KUNIT_CASE(dmapool_test_managed),
	KUNIT_CASE(dmapool_test_concurrent),
	{}
};

static struct kunit_suite dmapool_test_suite = {
	.name = "dmapool",
	.init = dmapool_test_init,
	.test_cases = dmapool_test_cases,
};

kunit_test_suite(dmapool_test_suite);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KUnit tests for DMA coherent pools");
