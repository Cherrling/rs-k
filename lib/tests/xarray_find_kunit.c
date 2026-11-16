// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray forward search.
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

static void xarray_test_find_restart_sparse(struct kunit *test)
{
	const unsigned long indexes[] = { 0, 3 * XA_CHUNK_SIZE + 7, 5000 };
	struct xarray *xa = xarray_test_init(test);
	void *values[ARRAY_SIZE(indexes)];
	struct xa_node *node;
	unsigned long found[ARRAY_SIZE(indexes)];
	void *entries[ARRAY_SIZE(indexes)];
	XA_STATE(xas, xa, 0);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		values[i] = xa_mk_value(0x1000 + i);
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, indexes[i], values[i], GFP_KERNEL));
	}

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		entries[i] = xas_find(&xas, ULONG_MAX);
		found[i] = xas.xa_index;
	}
	xas_find(&xas, ULONG_MAX);
	node = xas.xa_node;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, node, XAS_BOUNDS);
	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		KUNIT_EXPECT_PTR_EQ(test, entries[i], values[i]);
		KUNIT_EXPECT_EQ(test, found[i], indexes[i]);
	}
}

static void xarray_test_find_resume_unaligned(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *earlier = xa_mk_value(0x2001);
	void *target = xa_mk_value(0x2002);
	struct xa_node *node;
	unsigned long found;
	unsigned char offset;
	XA_STATE(xas, xa, 7);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 5, earlier, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, 9, target, GFP_KERNEL));

	rcu_read_lock();
	entry = xas_find(&xas, ULONG_MAX);
	found = xas.xa_index;
	node = xas.xa_node;
	offset = xas.xa_offset;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, target);
	KUNIT_EXPECT_EQ(test, found, 9);
	KUNIT_ASSERT_NOT_NULL(test, node);
	KUNIT_EXPECT_EQ(test, node->shift, 0);
	KUNIT_EXPECT_EQ(test, offset, 9 & XA_CHUNK_MASK);
}

static void xarray_test_find_empty_root(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *first_node;
	struct xa_node *second_node;
	unsigned long first_index;
	unsigned long second_index;
	XA_STATE(xas, xa, 0);
	void *first;
	void *second;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	rcu_read_lock();
	first = xas_find(&xas, ULONG_MAX);
	first_node = xas.xa_node;
	first_index = xas.xa_index;
	second = xas_find(&xas, ULONG_MAX);
	second_node = xas.xa_node;
	second_index = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, first);
	KUNIT_EXPECT_NULL(test, first_node);
	KUNIT_EXPECT_EQ(test, first_index, 0);
	KUNIT_EXPECT_NULL(test, second);
	KUNIT_EXPECT_PTR_EQ(test, second_node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, second_index, 1);
}

static void xarray_test_find_bounds(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *node;
	XA_STATE(xas, xa, 11);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	rcu_read_lock();
	entry = xas_find(&xas, 10);
	node = xas.xa_node;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_PTR_EQ(test, node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 11);
}

static void xarray_test_find_sibling_skip(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	unsigned long first = XA_CHUNK_SIZE;
	unsigned long last = 2 * XA_CHUNK_SIZE - 1;
	unsigned long target = last + 73;
	void *range_value = xa_mk_value(0x3001);
	void *target_value = xa_mk_value(0x3002);
	unsigned long found[2];
	void *entries[2];
	XA_STATE(xas, xa, first);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, range_value,
					 GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, target, target_value, GFP_KERNEL));

	rcu_read_lock();
	entries[0] = xas_find(&xas, ULONG_MAX);
	found[0] = xas.xa_index;
	entries[1] = xas_find(&xas, ULONG_MAX);
	found[1] = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entries[0], range_value);
	KUNIT_EXPECT_EQ(test, found[0], first);
	KUNIT_EXPECT_PTR_EQ(test, entries[1], target_value);
	KUNIT_EXPECT_EQ(test, found[1], target);
}

static void xarray_test_find_maximum_index(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x4001);
	unsigned long found;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, ULONG_MAX, value, GFP_KERNEL));

	rcu_read_lock();
	entry = xas_find(&xas, ULONG_MAX);
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, found, ULONG_MAX);
}

static void xarray_test_find_error_state(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *error_node;
	unsigned long index;
	XA_STATE(xas, xa, 17);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&xas, -EIO);
	error_node = xas.xa_node;
	index = xas.xa_index;

	entry = xas_find(&xas, ULONG_MAX);

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, error_node);
	KUNIT_EXPECT_EQ(test, xas.xa_index, index);
}

static struct kunit_case xarray_find_test_cases[] = {
	KUNIT_CASE(xarray_test_find_restart_sparse),
	KUNIT_CASE(xarray_test_find_resume_unaligned),
	KUNIT_CASE(xarray_test_find_empty_root),
	KUNIT_CASE(xarray_test_find_bounds),
	KUNIT_CASE(xarray_test_find_sibling_skip),
	KUNIT_CASE(xarray_test_find_maximum_index),
	KUNIT_CASE(xarray_test_find_error_state),
	{}
};

static struct kunit_suite xarray_find_test_suite = {
	.name = "xarray_find",
	.test_cases = xarray_find_test_cases,
};

kunit_test_suite(xarray_find_test_suite);
