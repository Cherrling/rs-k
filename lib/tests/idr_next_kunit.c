// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for IDR next-entry iteration.
 */

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/rcupdate.h>
#include <linux/spinlock.h>

#define IDR_NEXT_TEST_MANY 256

struct idr_next_test_context {
	struct idr idr;
};

static void idr_next_test_destroy(void *data)
{
	struct idr_next_test_context *ctx = data;

	idr_destroy(&ctx->idr);
}

static struct idr_next_test_context *idr_next_test_init(struct kunit *test,
							int base)
{
	struct idr_next_test_context *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return NULL;

	idr_init_base(&ctx->idr, base);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, idr_next_test_destroy,
						  ctx), 0);

	return ctx;
}

static int idr_next_test_alloc(struct idr_next_test_context *ctx, void *ptr,
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

static void *idr_next_test_remove(struct idr_next_test_context *ctx,
				  unsigned long id)
{
	unsigned long flags;
	void *entry;

	idr_lock_irqsave(&ctx->idr, flags);
	entry = idr_remove(&ctx->idr, id);
	idr_unlock_irqrestore(&ctx->idr, flags);

	return entry;
}

static void *idr_next_test_get_ul(struct idr_next_test_context *ctx,
				  unsigned long *id)
{
	void *entry;

	rcu_read_lock();
	entry = idr_get_next_ul(&ctx->idr, id);
	rcu_read_unlock();

	return entry;
}

static void *idr_next_test_get(struct idr_next_test_context *ctx, int *id)
{
	void *entry;

	rcu_read_lock();
	entry = idr_get_next(&ctx->idr, id);
	rcu_read_unlock();

	return entry;
}

static void idr_next_test_sparse(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 100);
	u32 values[3];
	unsigned long id;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &values[0], 100, 0), 100);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &values[1], 405, 0), 405);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &values[2], 901, 0), 901);

	id = 0;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &values[0]);
	KUNIT_EXPECT_EQ(test, id, 100);

	id = 100;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &values[0]);
	KUNIT_EXPECT_EQ(test, id, 100);

	id = 101;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &values[1]);
	KUNIT_EXPECT_EQ(test, id, 405);

	id = 406;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &values[2]);
	KUNIT_EXPECT_EQ(test, id, 901);

	id = 902;
	KUNIT_EXPECT_NULL(test, idr_next_test_get_ul(ctx, &id));
	KUNIT_EXPECT_EQ(test, id, 902);
}

static void idr_next_test_base_hole(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 37);
	u32 first;
	u32 second;
	unsigned long id;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &first, 37, 0), 37);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &second, 39, 0), 39);
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_remove(ctx, 37), &first);

	id = 0;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &second);
	KUNIT_EXPECT_EQ(test, id, 39);

	id = 38;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &second);
	KUNIT_EXPECT_EQ(test, id, 39);

	id = 39;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &second);
	KUNIT_EXPECT_EQ(test, id, 39);

	id = 40;
	KUNIT_EXPECT_NULL(test, idr_next_test_get_ul(ctx, &id));
	KUNIT_EXPECT_EQ(test, id, 40);
}

static void idr_next_test_null_entry(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 0);
	u32 later;
	unsigned long id;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test, idr_next_test_alloc(ctx, NULL, 10, 0), 10);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &later, 11, 0), 11);

	id = 0;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &later);
	KUNIT_EXPECT_EQ(test, id, 11);

	id = 12;
	KUNIT_EXPECT_NULL(test, idr_next_test_get_ul(ctx, &id));
	KUNIT_EXPECT_EQ(test, id, 12);
}

static void idr_next_test_internal_value(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 0);
	void *value = xa_mk_value(0x1234);
	u32 later;
	unsigned long id;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test, idr_next_test_alloc(ctx, value, 1, 0), 1);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &later, 9, 0), 9);

	id = 0;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), value);
	KUNIT_EXPECT_EQ(test, id, 1);

	id = 2;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get_ul(ctx, &id), &later);
	KUNIT_EXPECT_EQ(test, id, 9);
}

static void idr_next_test_signed_wrapper(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 5);
	u32 first;
	u32 second;
	int id;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &first, 5, 0), 5);
	KUNIT_ASSERT_EQ(test,
			idr_next_test_alloc(ctx, &second, 9, 0), 9);

	id = 0;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get(ctx, &id), &first);
	KUNIT_EXPECT_EQ(test, id, 5);

	id = 6;
	KUNIT_EXPECT_PTR_EQ(test, idr_next_test_get(ctx, &id), &second);
	KUNIT_EXPECT_EQ(test, id, 9);

	id = 10;
	KUNIT_EXPECT_NULL(test, idr_next_test_get(ctx, &id));
	KUNIT_EXPECT_EQ(test, id, 10);
}

static void idr_next_test_many(struct kunit *test)
{
	struct idr_next_test_context *ctx = idr_next_test_init(test, 3);
	u32 values[IDR_NEXT_TEST_MANY];
	unsigned long id = 0;
	unsigned int found = 0;

	KUNIT_ASSERT_NOT_NULL(test, ctx);
	for (found = 0; found < IDR_NEXT_TEST_MANY; found++)
		KUNIT_ASSERT_EQ(test,
				idr_next_test_alloc(ctx, &values[found],
						    found + 3, 0),
				found + 3);

	found = 0;
	while (idr_next_test_get_ul(ctx, &id)) {
		KUNIT_ASSERT_LT(test, found, IDR_NEXT_TEST_MANY);
		KUNIT_EXPECT_EQ(test, id, found + 3);
		found++;
		id++;
	}

	KUNIT_EXPECT_EQ(test, found, IDR_NEXT_TEST_MANY);
}

static struct kunit_case idr_next_test_cases[] = {
	KUNIT_CASE(idr_next_test_sparse),
	KUNIT_CASE(idr_next_test_base_hole),
	KUNIT_CASE(idr_next_test_null_entry),
	KUNIT_CASE(idr_next_test_internal_value),
	KUNIT_CASE(idr_next_test_signed_wrapper),
	KUNIT_CASE(idr_next_test_many),
	{}
};

static struct kunit_suite idr_next_test_suite = {
	.name = "idr_next",
	.test_cases = idr_next_test_cases,
};

kunit_test_suite(idr_next_test_suite);
