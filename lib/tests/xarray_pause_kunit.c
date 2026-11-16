// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray pause and cursor recovery.
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

static void xarray_test_pause_invalid_cursor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 7);
	void *node;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&xas, -EIO);
	node = xas.xa_node;

	xas_pause(&xas);

	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, node);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 7);
}

static void xarray_test_pause_null_cursor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, ULONG_MAX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas.xa_node = NULL;

	xas_pause(&xas);

	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_RESTART);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 0);
}

static void xarray_test_pause_advance(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x7102);
	XA_STATE(xas, xa, 2);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 2, value, GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	xas_pause(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_RESTART);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 3);
}

static void xarray_test_pause_siblings(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x7200);
	XA_STATE(xas, xa, XA_CHUNK_SIZE);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xarray_test_store_order(xa, XA_CHUNK_SIZE, 2, value,
						  GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	xas_pause(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_RESTART);
	KUNIT_EXPECT_EQ(test, xas.xa_index, XA_CHUNK_SIZE + 4);
}

static void xarray_test_pause_wrap_bounds(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x7300);
	XA_STATE(xas, xa, ULONG_MAX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, ULONG_MAX, value, GFP_KERNEL));

	rcu_read_lock();
	KUNIT_ASSERT_PTR_EQ(test, xas_load(&xas), value);
	xas_pause(&xas);
	rcu_read_unlock();

	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_BOUNDS);
	KUNIT_EXPECT_EQ(test, xas.xa_index, 0);
}

static void xarray_test_pause_resume_iteration(struct kunit *test)
{
	const unsigned long indexes[] = { 1, 8, 65, 4096 };
	const unsigned int count = ARRAY_SIZE(indexes);
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);
	unsigned int i;
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	for (i = 0; i < count; i++)
		KUNIT_ASSERT_NULL(test,
				  xa_store(xa, indexes[i], xa_mk_value(indexes[i]),
					   GFP_KERNEL));

	i = 0;
	rcu_read_lock();
	xas_for_each(&xas, entry, ULONG_MAX) {
		if (i >= count) {
			KUNIT_FAIL(test, "iteration exceeded expected entries");
			break;
		}
		KUNIT_EXPECT_PTR_EQ(test, entry, xa_mk_value(indexes[i]));
		KUNIT_EXPECT_EQ(test, xas.xa_index, indexes[i]);
		xas_pause(&xas);
		i++;
	}
	rcu_read_unlock();

	KUNIT_EXPECT_EQ(test, i, count);
}

static struct kunit_case xarray_pause_test_cases[] = {
	KUNIT_CASE(xarray_test_pause_invalid_cursor),
	KUNIT_CASE(xarray_test_pause_null_cursor),
	KUNIT_CASE(xarray_test_pause_advance),
	KUNIT_CASE(xarray_test_pause_siblings),
	KUNIT_CASE(xarray_test_pause_wrap_bounds),
	KUNIT_CASE(xarray_test_pause_resume_iteration),
	{}
};

static struct kunit_suite xarray_pause_test_suite = {
	.name = "xarray_pause",
	.test_cases = xarray_pause_test_cases,
};

kunit_test_suite(xarray_pause_test_suite);
