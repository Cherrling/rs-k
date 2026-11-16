// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray multi-index splitting.
 */

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/rcupdate.h>
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

static void *xarray_test_store_order(struct xarray *xa, unsigned long index,
				     unsigned int order, void *entry,
				     gfp_t gfp)
{
	XA_STATE_ORDER(xas, xa, index, order);
	void *curr;

	do {
		xas_lock(&xas);
		curr = xas_store(&xas, entry);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, gfp));

	return curr;
}

static void xarray_test_split_alloc_noop(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0x9100);
	XA_STATE_ORDER(xas, xa, 0, XA_CHUNK_SHIFT);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_split_alloc(&xas, entry, XA_CHUNK_SHIFT, GFP_KERNEL);

	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_split_alloc_chain(struct kunit *test)
{
	struct xa_node *node;
	unsigned int count = 0;
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0x9200);
	XA_STATE_ORDER(xas, xa, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_split_alloc(&xas, entry, XA_CHUNK_SHIFT + 1, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);

	for (node = xas.xa_alloc; node;
	     node = rcu_dereference_raw(node->parent))
		count++;

	KUNIT_EXPECT_EQ(test, count, 2);
	xas_destroy(&xas);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_split_top_noop(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0x9300);
	XA_STATE_ORDER(xas, xa, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, entry, GFP_KERNEL));
	xas_split_alloc(&xas, entry, 0, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);

	xas_lock(&xas);
	xas_split(&xas, entry, 0);
	xas_unlock(&xas);

	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), entry);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_split_horizontal(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0x9400);
	XA_STATE_ORDER(xas, xa, 0, XA_CHUNK_SHIFT);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xarray_test_store_order(xa, 0, 2 * XA_CHUNK_SHIFT,
						  entry, GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_1);
	KUNIT_ASSERT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));

	xas_split_alloc(&xas, entry, 2 * XA_CHUNK_SHIFT, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);

	xas_lock(&xas);
	xas_split(&xas, entry, 2 * XA_CHUNK_SHIFT);
	xas_unlock(&xas);

	for (i = 0; i < (1U << (2 * XA_CHUNK_SHIFT)); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, i), entry);

	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 37, XA_MARK_1));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_split_vertical(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *entry = xa_mk_value(0x9500);
	XA_STATE_ORDER(xas, xa, 0, XA_CHUNK_SHIFT);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xarray_test_store_order(xa, 0, 2 * XA_CHUNK_SHIFT,
						  entry, GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_1);
	KUNIT_ASSERT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));

	xas_split_alloc(&xas, entry, 2 * XA_CHUNK_SHIFT, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);

	xas_lock(&xas);
	xas_split(&xas, entry, 2 * XA_CHUNK_SHIFT);
	xas_unlock(&xas);

	for (i = 0; i < (1U << (2 * XA_CHUNK_SHIFT)); i++)
		KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, i), entry);

	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 4095, XA_MARK_1));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static struct kunit_case xarray_split_test_cases[] = {
	KUNIT_CASE(xarray_test_split_alloc_noop),
	KUNIT_CASE(xarray_test_split_alloc_chain),
	KUNIT_CASE(xarray_test_split_top_noop),
	KUNIT_CASE(xarray_test_split_horizontal),
	KUNIT_CASE(xarray_test_split_vertical),
	{}
};

static struct kunit_suite xarray_split_test_suite = {
	.name = "xarray_split",
	.test_cases = xarray_split_test_cases,
};

kunit_test_suite(xarray_split_test_suite);
