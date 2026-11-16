// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray adjacent cursor movement.
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

static void xarray_test_next_prev_leaf(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value5 = xa_mk_value(0x6105);
	void *value7 = xa_mk_value(0x6107);
	XA_STATE(xas, xa, 5);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 5, value5, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, 7, value7, GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value5);
	KUNIT_EXPECT_NULL(test, xas_next(&xas));
	KUNIT_EXPECT_EQ(test, xas.xa_index, 6);
	KUNIT_EXPECT_PTR_EQ(test, xas_next(&xas), value7);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 7);
	KUNIT_EXPECT_NULL(test, xas_prev(&xas));
	KUNIT_EXPECT_EQ(test, xas.xa_index, 6);
	entry = xas_prev(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value5);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 5);
}

static void xarray_test_next_prev_parent_crossing(struct kunit *test)
{
	unsigned long last = XA_CHUNK_SIZE * XA_CHUNK_SIZE - 1;
	struct xarray *xa = xarray_test_init(test);
	void *lower = xa_mk_value(0x6201);
	void *upper = xa_mk_value(0x6202);
	XA_STATE(xas, xa, last);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, last, lower, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, last + 1, upper, GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), lower);
	KUNIT_EXPECT_PTR_EQ(test, xas_next(&xas), upper);
	KUNIT_EXPECT_EQ(test, xas.xa_index, last + 1);
	entry = xas_prev(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, lower);
	KUNIT_EXPECT_EQ(test, xas.xa_index, last);
}

static void xarray_test_next_prev_restart(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x6301);
	XA_STATE(next, xa, 5);
	XA_STATE(prev, xa, 5);
	unsigned long next_index;
	unsigned long prev_index;
	void *next_entry;
	void *prev_entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 5, value, GFP_KERNEL));

	rcu_read_lock();
	next_entry = xas_next(&next);
	next_index = next.xa_index;
	prev_entry = xas_prev(&prev);
	prev_index = prev.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, next_entry, value);
	KUNIT_EXPECT_EQ(test, next_index, 5);
	KUNIT_EXPECT_PTR_EQ(test, prev_entry, value);
	KUNIT_EXPECT_EQ(test, prev_index, 5);
}

static void xarray_test_next_prev_null_cursor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(next, xa, 5);
	XA_STATE(prev, xa, 5);
	unsigned long next_index;
	unsigned long prev_index;
	void *next_entry;
	void *prev_entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	next.xa_node = NULL;
	prev.xa_node = NULL;

	rcu_read_lock();
	next_entry = xas_next(&next);
	next_index = next.xa_index;
	prev_entry = xas_prev(&prev);
	prev_index = prev.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, next_entry);
	KUNIT_EXPECT_PTR_EQ(test, next.xa_node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, next_index, 6);
	KUNIT_EXPECT_NULL(test, prev_entry);
	KUNIT_EXPECT_PTR_EQ(test, prev.xa_node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, prev_index, 4);
}

static void xarray_test_next_prev_error(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(next, xa, 5);
	XA_STATE(prev, xa, 5);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&next, -EIO);
	xas_set_err(&prev, -EIO);

	KUNIT_EXPECT_NULL(test, xas_next(&next));
	KUNIT_EXPECT_EQ(test, xas_error(&next), -EIO);
	KUNIT_EXPECT_EQ(test, next.xa_index, 5);
	KUNIT_EXPECT_NULL(test, xas_prev(&prev));
	KUNIT_EXPECT_EQ(test, xas_error(&prev), -EIO);
	KUNIT_EXPECT_EQ(test, prev.xa_index, 5);
}

static void xarray_test_next_prev_wrap(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *first = xa_mk_value(0x6500);
	void *last = xa_mk_value(0x65ff);
	XA_STATE(prev, xa, 0);
	XA_STATE(next, xa, ULONG_MAX);
	unsigned long prev_index;
	unsigned long next_index;
	void *prev_entry;
	void *next_entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, first, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, ULONG_MAX, last, GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&prev), first);
	prev_entry = xas_prev(&prev);
	prev_index = prev.xa_index;
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&next), last);
	next_entry = xas_next(&next);
	next_index = next.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_NULL(test, prev_entry);
	KUNIT_EXPECT_EQ(test, prev_index, ULONG_MAX);
	KUNIT_EXPECT_NULL(test, next_entry);
	KUNIT_EXPECT_EQ(test, next_index, 0);
}

static void xarray_test_next_prev_sibling(struct kunit *test)
{
	unsigned long first = XA_CHUNK_SIZE;
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x6601);
	XA_STATE(xas, xa, first);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xarray_test_store_order(xa, first, 2, value,
						  GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	entry = xas_next(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_EQ(test, xas.xa_index, first + 1);
	KUNIT_EXPECT_TRUE(test, xa_is_sibling(entry));

	rcu_read_lock();
	entry = xas_prev(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, xas.xa_index, first);
}

static void xarray_test_next_prev_bounds_cursor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value1 = xa_mk_value(0x6701);
	void *value3 = xa_mk_value(0x6703);
	XA_STATE(next, xa, 2);
	XA_STATE(prev, xa, 2);
	unsigned long next_index;
	unsigned long prev_index;
	void *next_entry;
	void *prev_entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 1, value1, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, 3, value3, GFP_KERNEL));
	next.xa_node = XAS_BOUNDS;
	prev.xa_node = XAS_BOUNDS;

	rcu_read_lock();
	next_entry = xas_next(&next);
	next_index = next.xa_index;
	prev_entry = xas_prev(&prev);
	prev_index = prev.xa_index;
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, next_entry, value3);
	KUNIT_EXPECT_EQ(test, next_index, 3);
	KUNIT_EXPECT_PTR_EQ(test, prev_entry, value1);
	KUNIT_EXPECT_EQ(test, prev_index, 1);
}

static struct kunit_case xarray_next_prev_test_cases[] = {
	KUNIT_CASE(xarray_test_next_prev_leaf),
	KUNIT_CASE(xarray_test_next_prev_parent_crossing),
	KUNIT_CASE(xarray_test_next_prev_restart),
	KUNIT_CASE(xarray_test_next_prev_null_cursor),
	KUNIT_CASE(xarray_test_next_prev_error),
	KUNIT_CASE(xarray_test_next_prev_wrap),
	KUNIT_CASE(xarray_test_next_prev_sibling),
	KUNIT_CASE(xarray_test_next_prev_bounds_cursor),
	{}
};

static struct kunit_suite xarray_next_prev_test_suite = {
	.name = "xarray_next_prev",
	.test_cases = xarray_next_prev_test_cases,
};

kunit_test_suite(xarray_next_prev_test_suite);
