// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray range conflict search.
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

static void xarray_test_find_conflict_error(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE_ORDER(xas, xa, 0, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&xas, -EIO);
	entry = xas_find_conflict(&xas);

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
}

static void xarray_test_find_conflict_null_cursor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE_ORDER(xas, xa, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas.xa_node = NULL;

	KUNIT_EXPECT_NULL(test, xas_find_conflict(&xas));
}

static void xarray_test_find_conflict_direct_root(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x8100);
	XA_STATE_ORDER(xas, xa, 0, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));

	xas_lock(&xas);
	entry = xas_find_conflict(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 0);
}

static void xarray_test_find_conflict_sibling(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x8200);
	XA_STATE_ORDER(xas, xa, (XA_CHUNK_SIZE + 1), 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xarray_test_store_order(xa, XA_CHUNK_SIZE, 2, value,
						  GFP_KERNEL));

	xas_lock(&xas);
	entry = xas_find_conflict(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, xas.xa_index, XA_CHUNK_SIZE + 1);
}

static void xarray_test_find_conflict_absent(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x8300);
	XA_STATE_ORDER(xas, xa, XA_CHUNK_SIZE, 1);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));

	xas_lock(&xas);
	entry = xas_find_conflict(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), 0);
}

static void xarray_test_find_conflict_repeated(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *first = xa_mk_value(0x8400);
	void *second = xa_mk_value(0x8402);
	XA_STATE_ORDER(xas, xa, XA_CHUNK_SIZE, 2);
	unsigned int count = 0;
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XA_CHUNK_SIZE, first, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XA_CHUNK_SIZE + 2, second, GFP_KERNEL));

	xas_lock(&xas);
	xas_for_each_conflict(&xas, entry) {
		if (count == 0)
			KUNIT_EXPECT_PTR_EQ(test, entry, first);
		else if (count == 1)
			KUNIT_EXPECT_PTR_EQ(test, entry, second);
		count++;
	}
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, count, 2);
}

static struct kunit_case xarray_find_conflict_test_cases[] = {
	KUNIT_CASE(xarray_test_find_conflict_error),
	KUNIT_CASE(xarray_test_find_conflict_null_cursor),
	KUNIT_CASE(xarray_test_find_conflict_direct_root),
	KUNIT_CASE(xarray_test_find_conflict_sibling),
	KUNIT_CASE(xarray_test_find_conflict_absent),
	KUNIT_CASE(xarray_test_find_conflict_repeated),
	{}
};

static struct kunit_suite xarray_find_conflict_test_suite = {
	.name = "xarray_find_conflict",
	.test_cases = xarray_find_conflict_test_cases,
};

kunit_test_suite(xarray_find_conflict_test_suite);
