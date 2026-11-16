// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray range creation.
 */

#include <kunit/test.h>

#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_RANGE_ORDER (XA_CHUNK_SHIFT + 2)
#define XARRAY_TEST_DEEP_ORDER (2 * XA_CHUNK_SHIFT + 2)

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

static void xarray_test_create_range_single(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x1234);
	unsigned long index = 123;
	struct xa_node *node;
	unsigned char offset;
	unsigned char shift;
	unsigned char sibs;
	XA_STATE(xas, xa, index);
	unsigned long result_index;
	int error;
	void *old;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_lock(&xas);
	xas_create_range(&xas);
	node = xas.xa_node;
	offset = xas.xa_offset;
	shift = xas.xa_shift;
	sibs = xas.xa_sibs;
	result_index = xas.xa_index;
	error = xas_error(&xas);
	old = xas_store(&xas, value);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_EQ(test, result_index, index);
	KUNIT_EXPECT_EQ(test, shift, 0);
	KUNIT_EXPECT_EQ(test, sibs, 0);
	KUNIT_ASSERT_NOT_NULL(test, node);
	KUNIT_EXPECT_EQ(test, node->shift, 0);
	KUNIT_EXPECT_EQ(test, offset, index & XA_CHUNK_MASK);
	KUNIT_EXPECT_NULL(test, old);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value);
}

static void xarray_test_create_range_multi_chunk(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	unsigned long nr = 1UL << XARRAY_TEST_RANGE_ORDER;
	unsigned long index = 3 * nr + 5;
	void *value = xa_mk_value(0x2345);
	unsigned long base;
	struct xa_node *node;
	unsigned char offset;
	bool store_valid = true;
	unsigned long i;
	XA_STATE_ORDER(xas, xa, index, XARRAY_TEST_RANGE_ORDER);
	int error;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	base = xas.xa_index;
	KUNIT_ASSERT_NE(test, base, 0);

	xas_lock(&xas);
	xas_create_range(&xas);
	node = xas.xa_node;
	offset = xas.xa_offset;
	error = xas_error(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_EQ(test, xas.xa_index, base);
	KUNIT_EXPECT_EQ(test, xas.xa_shift, 0);
	KUNIT_EXPECT_EQ(test, xas.xa_sibs, 0);
	KUNIT_ASSERT_NOT_NULL(test, node);
	KUNIT_EXPECT_EQ(test, node->shift, 0);
	KUNIT_EXPECT_EQ(test, offset, base & XA_CHUNK_MASK);

	xas_lock(&xas);
	for (i = 0; i < nr; i++) {
		if (xas_store(&xas, value))
			store_valid = false;
		xas_next(&xas);
	}
	error = xas_error(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_TRUE(test, store_valid);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base + nr - 1), value);
}

static void xarray_test_create_range_deep(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	unsigned long nr = 1UL << XARRAY_TEST_DEEP_ORDER;
	unsigned long index = nr + 5;
	unsigned long base;
	bool leaves_valid = true;
	unsigned long i;
	XA_STATE_ORDER(xas, xa, index, XARRAY_TEST_DEEP_ORDER);
	int error;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	base = xas.xa_index;

	xas_lock(&xas);
	xas_create_range(&xas);
	error = xas_error(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_EQ(test, xas.xa_index, base);
	KUNIT_EXPECT_EQ(test, xas.xa_shift, 0);
	KUNIT_EXPECT_EQ(test, xas.xa_sibs, 0);

	for (i = base; i < base + nr; i += XA_CHUNK_SIZE) {
		struct xa_node *node;
		XA_STATE(probe, xa, i);

		xas_lock(&probe);
		xas_load(&probe);
		node = probe.xa_node;
		xas_unlock(&probe);

		if (!node || node->shift) {
			leaves_valid = false;
			break;
		}
	}

	KUNIT_EXPECT_TRUE(test, leaves_valid);
}

static void xarray_test_create_range_existing_entry(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	unsigned long index = 2 * (1UL << XARRAY_TEST_RANGE_ORDER);
	unsigned long nr = 1UL << XARRAY_TEST_RANGE_ORDER;
	void *old_value = xa_mk_value(0x3456);
	void *new_value = xa_mk_value(0x4567);
	unsigned long base;
	bool store_valid = true;
	unsigned long i;
	XA_STATE_ORDER(xas, xa, index, XARRAY_TEST_RANGE_ORDER);
	int error;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	base = xas.xa_index;
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, base, base + nr - 1, old_value,
					 GFP_KERNEL));

	xas_lock(&xas);
	xas_create_range(&xas);
	error = xas_error(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base), old_value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base + nr - 1), old_value);

	xas_lock(&xas);
	for (i = 0; i < nr; i++) {
		void *old = xas_store(&xas, new_value);

		if (xa_err(old))
			store_valid = false;
		xas_next(&xas);
	}
	error = xas_error(&xas);
	xas_unlock(&xas);

	KUNIT_EXPECT_EQ(test, error, 0);
	KUNIT_EXPECT_TRUE(test, store_valid);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base), new_value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, base + nr - 1), new_value);
}

static void xarray_test_create_range_error_restore(struct kunit *test)
{
	unsigned long index;
	struct xa_node *error_node;
	unsigned char shift;
	unsigned char sibs;
	XA_STATE(xas, NULL, 0);

	xas_set_order(&xas, 0x1234, XARRAY_TEST_DEEP_ORDER);
	xas_set_err(&xas, -EEXIST);
	index = xas.xa_index;
	error_node = xas.xa_node;
	shift = xas.xa_shift;
	sibs = xas.xa_sibs;

	xas_create_range(&xas);

	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EEXIST);
	KUNIT_EXPECT_EQ(test, xas.xa_index, index);
	KUNIT_EXPECT_EQ(test, xas.xa_shift, shift);
	KUNIT_EXPECT_EQ(test, xas.xa_sibs, sibs);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, error_node);
}

static struct kunit_case xarray_create_range_test_cases[] = {
	KUNIT_CASE(xarray_test_create_range_single),
	KUNIT_CASE(xarray_test_create_range_multi_chunk),
	KUNIT_CASE(xarray_test_create_range_deep),
	KUNIT_CASE(xarray_test_create_range_existing_entry),
	KUNIT_CASE(xarray_test_create_range_error_restore),
	{}
};

static struct kunit_suite xarray_create_range_test_suite = {
	.name = "xarray_create_range",
	.test_cases = xarray_create_range_test_cases,
};

kunit_test_suite(xarray_create_range_test_suite);
