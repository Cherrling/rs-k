// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray allocation lifecycle.
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

static void xarray_test_nomem_alloc(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&xas, -ENOMEM);

	KUNIT_EXPECT_TRUE(test, xas_nomem(&xas, GFP_KERNEL));
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_NULL(test, rcu_dereference_raw(xas.xa_alloc->parent));
	KUNIT_EXPECT_TRUE(test, list_empty(&xas.xa_alloc->private_list));
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_RESTART);

	xas_destroy(&xas);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_nomem_accounted(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xa->xa_flags |= XA_FLAGS_ACCOUNT;
	xas_set_err(&xas, -ENOMEM);

	KUNIT_EXPECT_TRUE(test, xas_nomem(&xas, GFP_KERNEL));
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_RESTART);

	xas_destroy(&xas);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_nomem_not_needed(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_set_err(&xas, -EIO);

	KUNIT_EXPECT_FALSE(test, xas_nomem(&xas, GFP_KERNEL));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
}

static void xarray_test_nomem_destroy_chain(struct kunit *test)
{
	struct xa_node *node;
	unsigned int count = 0;
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_split_alloc(&xas, xa_mk_value(1), XA_CHUNK_SHIFT + 1,
			GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);

	for (node = xas.xa_alloc; node;
	     node = rcu_dereference_raw(node->parent))
		count++;

	KUNIT_EXPECT_EQ(test, count, 2);
	xas_destroy(&xas);
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static void xarray_test_nomem_cleanup_chain(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	XA_STATE(xas, xa, 0);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	xas_split_alloc(&xas, xa_mk_value(2), XA_CHUNK_SHIFT + 1,
			GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, xas_error(&xas), 0);
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_alloc);

	xas.xa_node = XAS_RESTART;
	KUNIT_EXPECT_FALSE(test, xas_nomem(&xas, GFP_KERNEL));
	KUNIT_EXPECT_NULL(test, xas.xa_alloc);
}

static struct kunit_case xarray_nomem_test_cases[] = {
	KUNIT_CASE(xarray_test_nomem_alloc),
	KUNIT_CASE(xarray_test_nomem_accounted),
	KUNIT_CASE(xarray_test_nomem_not_needed),
	KUNIT_CASE(xarray_test_nomem_destroy_chain),
	KUNIT_CASE(xarray_test_nomem_cleanup_chain),
	{}
};

static struct kunit_suite xarray_nomem_test_suite = {
	.name = "xarray_nomem",
	.test_cases = xarray_nomem_test_cases,
};

kunit_test_suite(xarray_nomem_test_suite);
