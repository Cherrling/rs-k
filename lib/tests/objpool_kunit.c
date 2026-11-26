// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/module.h>
#include <linux/objpool.h>
#include <linux/refcount.h>
#include <linux/slab.h>

#define OBJPOOL_TEST_OBJECT_SIZE	16
#define OBJPOOL_TEST_MARKER		0x6f626a706f6f6cUL

struct objpool_test_context {
	struct objpool_head pool;
	atomic_t init_calls;
	atomic_t release_calls;
	int fail_after;
	bool active;
};

static int objpool_test_init_object(void *object, void *context)
{
	struct objpool_test_context *ctx = context;
	unsigned long *marker = object;
	int calls;

	calls = atomic_inc_return(&ctx->init_calls);
	if (ctx->fail_after >= 0 && calls > ctx->fail_after)
		return -EIO;

	if (*marker) {
		pr_err("objpool KUnit object was not zeroed\n");
		return -EUCLEAN;
	}

	*marker = OBJPOOL_TEST_MARKER;
	return 0;
}

static int objpool_test_release(struct objpool_head *head, void *context)
{
	struct objpool_test_context *ctx = context;

	if (head != &ctx->pool)
		return -EINVAL;

	atomic_inc(&ctx->release_calls);
	return 0;
}

static void objpool_test_cleanup(void *data)
{
	struct objpool_test_context *ctx = data;

	if (ctx->active)
		objpool_free(&ctx->pool);
}

static struct objpool_test_context *objpool_test_context_alloc(struct kunit *test)
{
	struct objpool_test_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	ctx->fail_after = -1;
	return ctx;
}

static int objpool_test_init(struct objpool_test_context *ctx,
			     int nr_objs, gfp_t gfp)
{
	return objpool_init(&ctx->pool, nr_objs, OBJPOOL_TEST_OBJECT_SIZE, gfp,
			    ctx, objpool_test_init_object, objpool_test_release);
}

static void objpool_test_parameters(struct kunit *test)
{
	struct objpool_head pool = {
		.cpu_slots = (void *)-1UL,
	};

	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, 0, OBJPOOL_TEST_OBJECT_SIZE, GFP_KERNEL,
				     NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, -1, OBJPOOL_TEST_OBJECT_SIZE, GFP_KERNEL,
				     NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, OBJPOOL_NR_OBJECT_MAX + 1,
				     OBJPOOL_TEST_OBJECT_SIZE, GFP_KERNEL,
				     NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, 1, 0, GFP_KERNEL, NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, 1, -1, GFP_KERNEL, NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			objpool_init(&pool, 1, OBJPOOL_OBJECT_SIZE_MAX + 1,
				     GFP_KERNEL, NULL, NULL, NULL),
			-EINVAL);
	KUNIT_EXPECT_PTR_EQ(test, pool.cpu_slots, (void *)-1UL);
}

static void objpool_test_init_layout(struct kunit *test)
{
	struct objpool_test_context *ctx;
	int ret;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ret = objpool_test_init(ctx, 6, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->active = true;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, objpool_test_cleanup, ctx),
			0);

	KUNIT_EXPECT_EQ(test, ctx->pool.nr_objs, 6);
	KUNIT_EXPECT_EQ(test, ctx->pool.nr_possible_cpus, num_possible_cpus());
	KUNIT_EXPECT_EQ(test, ctx->pool.obj_size, OBJPOOL_TEST_OBJECT_SIZE);
	KUNIT_EXPECT_EQ(test, ctx->pool.capacity, 8);
	KUNIT_EXPECT_FALSE(test, ctx->pool.gfp & __GFP_ZERO);
	KUNIT_EXPECT_EQ(test, refcount_read(&ctx->pool.ref), 7);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->init_calls), 6);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 0);

	ctx->active = false;
	objpool_fini(&ctx->pool);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 1);
}

static void objpool_test_pop_push(struct kunit *test)
{
	void *objects[4];
	struct objpool_test_context *ctx;
	int ret;
	int i;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ret = objpool_test_init(ctx, ARRAY_SIZE(objects), GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->active = true;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, objpool_test_cleanup, ctx),
			0);

	for (i = 0; i < ARRAY_SIZE(objects); i++) {
		objects[i] = objpool_pop(&ctx->pool);
		KUNIT_ASSERT_NOT_NULL(test, objects[i]);
		KUNIT_EXPECT_EQ(test, *(unsigned long *)objects[i],
				OBJPOOL_TEST_MARKER);
	}
	KUNIT_EXPECT_NULL(test, objpool_pop(&ctx->pool));

	for (i = 0; i < ARRAY_SIZE(objects); i++)
		KUNIT_EXPECT_EQ(test, objpool_push(objects[i], &ctx->pool), 0);
	for (i = 0; i < ARRAY_SIZE(objects); i++)
		KUNIT_EXPECT_NOT_NULL(test, objpool_pop(&ctx->pool));
}

static void objpool_test_drop_lifecycle(struct kunit *test)
{
	struct objpool_test_context *ctx;
	void *object;
	int ret;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ret = objpool_test_init(ctx, 2, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->active = true;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, objpool_test_cleanup, ctx),
			0);

	object = objpool_pop(&ctx->pool);
	KUNIT_ASSERT_NOT_NULL(test, object);
	KUNIT_EXPECT_EQ(test, objpool_drop(NULL, &ctx->pool), -EINVAL);
	KUNIT_EXPECT_EQ(test, objpool_drop(object, NULL), -EINVAL);

	ctx->active = false;
	objpool_fini(&ctx->pool);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 0);
	KUNIT_EXPECT_EQ(test, objpool_drop(object, &ctx->pool), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 1);
}

static void objpool_test_forced_free(struct kunit *test)
{
	struct objpool_test_context *ctx;
	void *object;
	int ret;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ret = objpool_test_init(ctx, 3, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->active = true;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, objpool_test_cleanup, ctx),
			0);

	object = objpool_pop(&ctx->pool);
	KUNIT_ASSERT_NOT_NULL(test, object);
	ctx->active = false;
	objpool_free(&ctx->pool);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 1);
}

static void objpool_test_init_failure(struct kunit *test)
{
	struct objpool_test_context *ctx;
	int ret;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ctx->fail_after = 2;
	ret = objpool_init(&ctx->pool, 8, OBJPOOL_TEST_OBJECT_SIZE, GFP_KERNEL,
			   ctx, objpool_test_init_object, objpool_test_release);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->init_calls), 3);
	KUNIT_EXPECT_EQ(test, atomic_read(&ctx->release_calls), 0);
}

static void objpool_test_atomic_allocation(struct kunit *test)
{
	struct objpool_test_context *ctx;
	void *objects[3];
	int ret;
	int i;

	ctx = objpool_test_context_alloc(test);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	ret = objpool_test_init(ctx, ARRAY_SIZE(objects), GFP_ATOMIC);
	KUNIT_ASSERT_EQ(test, ret, 0);
	ctx->active = true;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, objpool_test_cleanup, ctx),
			0);

	for (i = 0; i < ARRAY_SIZE(objects); i++)
		objects[i] = objpool_pop(&ctx->pool);

	for (i = 0; i < ARRAY_SIZE(objects); i++) {
		KUNIT_ASSERT_NOT_NULL(test, objects[i]);
		KUNIT_EXPECT_EQ(test, objpool_push(objects[i], &ctx->pool), 0);
	}
	KUNIT_EXPECT_EQ(test, ctx->pool.capacity, 4);
}

static struct kunit_case objpool_test_cases[] = {
	KUNIT_CASE(objpool_test_parameters),
	KUNIT_CASE(objpool_test_init_layout),
	KUNIT_CASE(objpool_test_pop_push),
	KUNIT_CASE(objpool_test_drop_lifecycle),
	KUNIT_CASE(objpool_test_forced_free),
	KUNIT_CASE(objpool_test_init_failure),
	KUNIT_CASE(objpool_test_atomic_allocation),
	{}
};

static struct kunit_suite objpool_test_suite = {
	.name = "objpool",
	.test_cases = objpool_test_cases,
};

kunit_test_suites(&objpool_test_suite);

MODULE_DESCRIPTION("objpool KUnit test suite");
MODULE_LICENSE("GPL");
