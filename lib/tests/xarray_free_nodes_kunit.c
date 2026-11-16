// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray subtree reclamation.
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

static void xarray_test_replace_root_slots(struct kunit *test)
{
	const unsigned int order = XA_CHUNK_SHIFT * 2 + 1;
	const unsigned long size = 1UL << order;
	const unsigned long first = size;
	const unsigned long middle = first + size / 2;
	const unsigned long last = first + size - 1;
	const unsigned long outside = last + 1;
	void *value = xa_mk_value(0x4567);
	void *outside_value = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);
	void *old;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, outside, outside_value, GFP_KERNEL));
	xa_set_mark(xa, outside, XA_MARK_2);

	KUNIT_ASSERT_NULL(test, xa_store(xa, first, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, middle, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, last, value, GFP_KERNEL));
	xa_set_mark(xa, first, XA_MARK_0);
	xa_set_mark(xa, middle, XA_MARK_1);
	xa_set_mark(xa, last, XA_MARK_2);

	old = xarray_test_store_order(xa, first, order, value);
	KUNIT_EXPECT_FALSE(test, xa_err(old));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, first), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, middle), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, last), value);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_0));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_2));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, outside), outside_value);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, outside, XA_MARK_2));

	old = xarray_test_store_order(xa, first, order, NULL);
	KUNIT_EXPECT_FALSE(test, xa_err(old));
	KUNIT_EXPECT_NULL(test, xa_load(xa, first));
	KUNIT_EXPECT_NULL(test, xa_load(xa, middle));
	KUNIT_EXPECT_NULL(test, xa_load(xa, last));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, outside), outside_value);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, outside, XA_MARK_2));
}

static void xarray_test_replace_nested_subtree(struct kunit *test)
{
	const unsigned int order = XA_CHUNK_SHIFT * 3;
	const unsigned long size = 1UL << order;
	const unsigned long first = size;
	const unsigned long second = first + 1;
	const unsigned long child = first + XA_CHUNK_SIZE;
	const unsigned long last = first + size - 1;
	const unsigned long outside = last + 1;
	void *value = xa_mk_value(0x6789);
	void *outside_value = xa_mk_value(0x789a);
	struct xarray *xa = xarray_test_init(test);
	void *old;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, outside, outside_value, GFP_KERNEL));

	KUNIT_ASSERT_NULL(test, xa_store(xa, first, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, second, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, child, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, last, value, GFP_KERNEL));
	xa_set_mark(xa, first, XA_MARK_0);
	xa_set_mark(xa, second, XA_MARK_1);
	xa_set_mark(xa, child, XA_MARK_2);
	xa_set_mark(xa, last, XA_MARK_2);

	old = xarray_test_store_order(xa, first, order, value);
	KUNIT_EXPECT_FALSE(test, xa_err(old));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, first), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, second), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, child), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, last), value);
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_0));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_1));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, first, XA_MARK_2));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, outside), outside_value);

	old = xarray_test_store_order(xa, first, order, NULL);
	KUNIT_EXPECT_FALSE(test, xa_err(old));
	KUNIT_EXPECT_NULL(test, xa_load(xa, first));
	KUNIT_EXPECT_NULL(test, xa_load(xa, second));
	KUNIT_EXPECT_NULL(test, xa_load(xa, child));
	KUNIT_EXPECT_NULL(test, xa_load(xa, last));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, outside), outside_value);
}

static struct kunit_case xarray_free_nodes_test_cases[] = {
	KUNIT_CASE(xarray_test_replace_root_slots),
	KUNIT_CASE(xarray_test_replace_nested_subtree),
	{}
};

static struct kunit_suite xarray_free_nodes_test_suite = {
	.name = "xarray_free_nodes",
	.test_cases = xarray_free_nodes_test_cases,
};

kunit_test_suite(xarray_free_nodes_test_suite);
