// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray one-shot multi-index splitting.
 */

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/xarray.h>

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

static void xarray_test_store_order(struct kunit *test, struct xarray *xa,
				    unsigned long index, unsigned int order,
				    void *entry)
{
	XA_STATE_ORDER(xas, xa, index, order);
	void *curr;

	do {
		xas_lock(&xas);
		curr = xas_store(&xas, entry);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	KUNIT_ASSERT_PTR_EQ_MSG(test, curr, NULL,
				"failed to store order %u", order);
}

static void xarray_test_expect_range(struct kunit *test, struct xarray *xa,
				     void *entry, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, i), entry);
}

static void xarray_test_add_prealloc(struct kunit *test, struct xa_state *xas)
{
	xas_set_err(xas, -ENOMEM);
	KUNIT_ASSERT_TRUE(test, xas_nomem(xas, GFP_KERNEL));
	KUNIT_ASSERT_NOT_NULL(test, xas->xa_alloc);
}

static void xarray_test_try_split_min_order(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(0), 0);
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(1), 0);
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(XA_CHUNK_SHIFT - 1), 0);
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(XA_CHUNK_SHIFT),
			XA_CHUNK_SHIFT - 1);
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(XA_CHUNK_SHIFT + 1),
			XA_CHUNK_SHIFT);
	KUNIT_EXPECT_EQ(test, xas_try_split_min_order(2 * XA_CHUNK_SHIFT),
			2 * XA_CHUNK_SHIFT - 1);
}

static void xarray_test_try_split_top_noop(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0xa100);
	XA_STATE_ORDER(xas, xa, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, entry, GFP_KERNEL));

	xas_lock(&xas);
	xas_try_split(&xas, entry, 0);
	xas_unlock(&xas);

	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), entry);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_try_split_horizontal(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0xa200);
	XA_STATE_ORDER(xas, xa, 0, XA_CHUNK_SHIFT);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xarray_test_store_order(test, xa, 0, XA_CHUNK_SHIFT + 1, entry);
	xa_set_mark(xa, 0, XA_MARK_1);

	xas_lock(&xas);
	xas_try_split(&xas, entry, XA_CHUNK_SHIFT + 1);
	xas_unlock(&xas);

	xarray_test_expect_range(test, xa, entry, 1U << (XA_CHUNK_SHIFT + 1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, (1U << (XA_CHUNK_SHIFT + 1)) - 1,
					    XA_MARK_1));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_try_split_vertical_prealloc(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0xa300);
	XA_STATE_ORDER(xas, xa, 0, (2 * XA_CHUNK_SHIFT - 1));

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xarray_test_store_order(test, xa, 0, 2 * XA_CHUNK_SHIFT, entry);
	xa_set_mark(xa, 0, XA_MARK_1);
	xarray_test_add_prealloc(test, &xas);

	xas_lock(&xas);
	xas_try_split(&xas, entry, 2 * XA_CHUNK_SHIFT);
	xas_unlock(&xas);

	xarray_test_expect_range(test, xa, entry, 1U << (2 * XA_CHUNK_SHIFT));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 4095, XA_MARK_1));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_try_split_vertical_nowait(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0xa400);
	XA_STATE_ORDER(xas, xa, 0, (2 * XA_CHUNK_SHIFT - 1));

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xarray_test_store_order(test, xa, 0, 2 * XA_CHUNK_SHIFT, entry);
	xas_lock(&xas);
	xas_try_split(&xas, entry, 2 * XA_CHUNK_SHIFT);
	xas_unlock(&xas);

	xarray_test_expect_range(test, xa, entry, 1U << (2 * XA_CHUNK_SHIFT));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_try_split_cascade_rejected(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0xa500);
	XA_STATE_ORDER(xas, xa, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xarray_test_store_order(test, xa, 0, 2 * XA_CHUNK_SHIFT, entry);
	xarray_test_add_prealloc(test, &xas);

	xas_lock(&xas);
	xas_try_split(&xas, entry, 2 * XA_CHUNK_SHIFT);
	xas_unlock(&xas);

	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), entry);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EINVAL);
}

static struct kunit_case xarray_try_split_test_cases[] = {
	KUNIT_CASE(xarray_test_try_split_min_order),
	KUNIT_CASE(xarray_test_try_split_top_noop),
	KUNIT_CASE(xarray_test_try_split_horizontal),
	KUNIT_CASE(xarray_test_try_split_vertical_prealloc),
	KUNIT_CASE(xarray_test_try_split_vertical_nowait),
	KUNIT_CASE(xarray_test_try_split_cascade_rejected),
	{}
};

static struct kunit_suite xarray_try_split_test_suite = {
	.name = "xarray_try_split",
	.test_cases = xarray_try_split_test_cases,
};

kunit_test_suite(xarray_try_split_test_suite);
