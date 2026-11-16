// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray marked forward search.
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

static void xarray_test_marked_sparse(struct kunit *test)
{
	const unsigned long indexes[] = { 0, 3 * XA_CHUNK_SIZE + 7, 5000 };
	struct xarray *xa = xarray_test_init(test);
	void *values[ARRAY_SIZE(indexes)];
	unsigned long found[ARRAY_SIZE(indexes) - 1];
	void *entries[ARRAY_SIZE(indexes) - 1];
	struct xa_node *node;
	XA_STATE(xas, xa, 0);
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		values[i] = xa_mk_value(0x5100 + i);
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, indexes[i], values[i], GFP_KERNEL));
	}
	xa_set_mark(xa, indexes[1], XA_MARK_0);
	xa_set_mark(xa, indexes[2], XA_MARK_0);

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		entries[i] = xas_find_marked(&xas, ULONG_MAX, XA_MARK_0);
		found[i] = xas.xa_index;
	}
	KUNIT_EXPECT_NULL(test,
			  xas_find_marked(&xas, ULONG_MAX, XA_MARK_0));
	node = xas.xa_node;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, node, XAS_BOUNDS);
	for (i = 0; i < ARRAY_SIZE(entries); i++) {
		KUNIT_EXPECT_PTR_EQ(test, entries[i], values[i + 1]);
		KUNIT_EXPECT_EQ(test, found[i], indexes[i + 1]);
	}
}

static void xarray_test_marked_direct_root(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x5201);
	struct xa_node *node;
	unsigned long found;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_1);

	rcu_read_lock();
	entry = xas_find_marked(&xas, ULONG_MAX, XA_MARK_1);
	node = xas.xa_node;
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_NULL(test, node);
	KUNIT_EXPECT_EQ(test, found, 0);
}

static void xarray_test_marked_max_cutoff(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x5301);
	struct xa_node *node;
	unsigned long found;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 200, value, GFP_KERNEL));
	xa_set_mark(xa, 200, XA_MARK_2);

	rcu_read_lock();
	entry = xas_find_marked(&xas, 100, XA_MARK_2);
	node = xas.xa_node;
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_PTR_EQ(test, node, XAS_RESTART);
	KUNIT_EXPECT_EQ(test, found, 3 * XA_CHUNK_SIZE);
}

static void xarray_test_marked_empty_root(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *node;
	unsigned long found;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	rcu_read_lock();
	entry = xas_find_marked(&xas, ULONG_MAX, XA_MARK_0);
	node = xas.xa_node;
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_PTR_EQ(test, node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, found, 1);
}

static void xarray_test_marked_sibling_range(struct kunit *test)
{
	unsigned long first = XA_CHUNK_SIZE;
	unsigned long last = 2 * XA_CHUNK_SIZE - 1;
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x5401);
	unsigned long found;
	XA_STATE(xas, xa, first);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, last, value, GFP_KERNEL));
	xa_set_mark(xa, first + 7, XA_MARK_2);

	rcu_read_lock();
	entry = xas_find_marked(&xas, ULONG_MAX, XA_MARK_2);
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_TRUE(test, found >= first);
	KUNIT_EXPECT_TRUE(test, found <= last);
}

static void xarray_test_marked_maximum_index(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x5501);
	unsigned long found;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, ULONG_MAX, value, GFP_KERNEL));
	xa_set_mark(xa, ULONG_MAX, XA_MARK_0);

	rcu_read_lock();
	entry = xas_find_marked(&xas, ULONG_MAX, XA_MARK_0);
	found = xas.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, found, ULONG_MAX);
}

static void xarray_test_marked_error_state(struct kunit *test)
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

	entry = xas_find_marked(&xas, ULONG_MAX, XA_MARK_0);

	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, error_node);
	KUNIT_EXPECT_EQ(test, xas.xa_index, index);
}

static struct kunit_case xarray_find_marked_test_cases[] = {
	KUNIT_CASE(xarray_test_marked_sparse),
	KUNIT_CASE(xarray_test_marked_direct_root),
	KUNIT_CASE(xarray_test_marked_max_cutoff),
	KUNIT_CASE(xarray_test_marked_empty_root),
	KUNIT_CASE(xarray_test_marked_sibling_range),
	KUNIT_CASE(xarray_test_marked_maximum_index),
	KUNIT_CASE(xarray_test_marked_error_state),
	{}
};

static struct kunit_suite xarray_find_marked_test_suite = {
	.name = "xarray_find_marked",
	.test_cases = xarray_find_marked_test_cases,
};

kunit_test_suite(xarray_find_marked_test_suite);
