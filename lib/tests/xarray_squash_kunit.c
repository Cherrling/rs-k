// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray multi-index mark squashing.
 */

#include <kunit/test.h>

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
				     unsigned int order, void *entry)
{
	XA_STATE_ORDER(xas, xa, index, order);
	void *curr;

	do {
		xas_lock(&xas);
		curr = xas_store(&xas, entry);
		xas_unlock(&xas);
	} while (xas_nomem(&xas, GFP_KERNEL));

	return curr;
}

static void xarray_test_union_across_orders(struct kunit *test)
{
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test);
	unsigned int order;

	KUNIT_ASSERT_NOT_NULL(test, xa);

	for (order = 2; order <= 6; order++) {
		unsigned long size = 1UL << order;
		unsigned long first = size << 1;
		unsigned long middle = first + size / 2;
		unsigned long last = first + size - 1;
		unsigned long outside = first + size;
		unsigned long index;
		void *old;

		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, first, value, GFP_KERNEL));
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, middle, value, GFP_KERNEL));
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, last, value, GFP_KERNEL));
		xa_set_mark(xa, first, XA_MARK_0);
		xa_set_mark(xa, middle, XA_MARK_1);
		xa_set_mark(xa, last, XA_MARK_2);

		old = xarray_test_store_order(xa, first, order, value);
		KUNIT_EXPECT_FALSE(test, xa_err(old));
		for (index = first; index <= last; index++) {
			KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value);
			KUNIT_EXPECT_TRUE(test,
					  xa_get_mark(xa, index, XA_MARK_0));
			KUNIT_EXPECT_TRUE(test,
					  xa_get_mark(xa, index, XA_MARK_1));
			KUNIT_EXPECT_TRUE(test,
					  xa_get_mark(xa, index, XA_MARK_2));
		}
		KUNIT_EXPECT_NULL(test, xa_load(xa, outside));
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, outside, XA_MARK_0));
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, outside, XA_MARK_1));
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, outside, XA_MARK_2));

		old = xarray_test_store_order(xa, first, order, NULL);
		KUNIT_EXPECT_FALSE(test, xa_err(old));
		for (index = first; index <= last; index++)
			KUNIT_EXPECT_NULL(test, xa_load(xa, index));
		KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
		KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
		KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_2));
	}
}

static void xarray_test_sibling_clear_preserves_outside(struct kunit *test)
{
	const unsigned long first = XA_CHUNK_SIZE * 2;
	const unsigned long last = first + XA_CHUNK_SIZE - 1;
	const unsigned long outside = last + 1;
	void *value = xa_mk_value(0x2345);
	void *outside_value = xa_mk_value(0x3456);
	struct xarray *xa = xarray_test_init(test);
	unsigned long index;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, outside, outside_value, GFP_KERNEL));
	xa_set_mark(xa, outside, XA_MARK_2);

	KUNIT_ASSERT_NULL(test, xa_store(xa, first, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, first + XA_CHUNK_SIZE / 2, value,
				   GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, last, value, GFP_KERNEL));
	xa_set_mark(xa, first, XA_MARK_0);
	xa_set_mark(xa, first + XA_CHUNK_SIZE / 2, XA_MARK_1);
	xa_set_mark(xa, last, XA_MARK_2);

	KUNIT_EXPECT_FALSE(test,
			   xa_err(xarray_test_store_order(xa, first,
							  XA_CHUNK_SHIFT,
							  value)));
	for (index = first; index <= last; index++) {
		KUNIT_EXPECT_TRUE(test,
				  xa_get_mark(xa, index, XA_MARK_0));
		KUNIT_EXPECT_TRUE(test,
				  xa_get_mark(xa, index, XA_MARK_1));
		KUNIT_EXPECT_TRUE(test,
				  xa_get_mark(xa, index, XA_MARK_2));
	}
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, outside, XA_MARK_2));

	KUNIT_EXPECT_FALSE(test,
			   xa_err(xarray_test_store_order(xa, first,
							  XA_CHUNK_SHIFT,
							  NULL)));
	for (index = first; index <= last; index++) {
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, index, XA_MARK_0));
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, index, XA_MARK_1));
		KUNIT_EXPECT_FALSE(test,
				   xa_get_mark(xa, index, XA_MARK_2));
	}
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test, xa_marked(xa, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_2));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, outside, XA_MARK_2));
}

static struct kunit_case xarray_squash_test_cases[] = {
	KUNIT_CASE(xarray_test_union_across_orders),
	KUNIT_CASE(xarray_test_sibling_clear_preserves_outside),
	{}
};

static struct kunit_suite xarray_squash_test_suite = {
	.name = "xarray_squash",
	.test_cases = xarray_squash_test_cases,
};

kunit_test_suite(xarray_squash_test_suite);
