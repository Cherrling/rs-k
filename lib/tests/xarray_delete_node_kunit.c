// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray node deletion and root shrinking.
 */

#include <kunit/test.h>

#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_DEEP_INDEX (XA_CHUNK_SIZE * XA_CHUNK_SIZE)
#define XARRAY_TEST_LAST_INDEX (XARRAY_TEST_DEEP_INDEX * 2)

static void xarray_test_destroy(void *data)
{
	xa_destroy(data);
}

static struct xarray *xarray_test_init_flags(struct kunit *test, gfp_t flags)
{
	struct xarray *xa;

	xa = kunit_kzalloc(test, sizeof(*xa), GFP_KERNEL);
	if (!xa) {
		KUNIT_FAIL(test, "failed to allocate XArray");
		return NULL;
	}

	xa_init_flags(xa, flags);
	if (kunit_add_action_or_reset(test, xarray_test_destroy, xa)) {
		KUNIT_FAIL(test, "failed to register XArray cleanup");
		return NULL;
	}

	return xa;
}

static struct xarray *xarray_test_init(struct kunit *test)
{
	return xarray_test_init_flags(test, 0);
}

static void *xarray_test_head(struct xarray *xa)
{
	void *entry;

	rcu_read_lock();
	entry = xa_head(xa);
	rcu_read_unlock();

	return entry;
}

static void xarray_test_shrink_single_survivor(struct kunit *test)
{
	void *value_a = xa_mk_value(0x1234);
	void *value_b = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *node;
	XA_STATE(xas, NULL, 1);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value_a, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test, xa_store(xa, 1, value_b, GFP_KERNEL));

	xas.xa = xa;
	xas_lock(&xas);
	entry = xas_load(&xas);
	node = xas.xa_node;
	KUNIT_EXPECT_PTR_EQ(test, entry, value_b);
	KUNIT_EXPECT_NOT_NULL(test, node);
	if (node) {
		KUNIT_EXPECT_PTR_EQ(test, xa_entry_locked(xa, node, 0), value_a);
		KUNIT_EXPECT_PTR_EQ(test, xas_store(&xas, NULL), value_b);
		KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_BOUNDS);
		KUNIT_EXPECT_PTR_EQ(test, xa_entry_locked(xa, node, 0),
				    XA_RETRY_ENTRY);
	}
	xas_unlock(&xas);

	KUNIT_EXPECT_NULL(test, xa_load(xa, 1));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), value_a);
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), value_a);
}

static void xarray_test_delete_parent_chain(struct kunit *test)
{
	void *low_value = xa_mk_value(0x2345);
	void *middle_value = xa_mk_value(0x3456);
	void *last_value = xa_mk_value(0x4567);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, 0, low_value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, middle_value,
				   GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_LAST_INDEX, last_value,
				   GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, xa_is_node(xarray_test_head(xa)));

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX),
			    middle_value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), low_value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_LAST_INDEX),
			    last_value);

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_LAST_INDEX),
			    last_value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), low_value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_LAST_INDEX));
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), low_value);
}

static void xarray_test_clear_deep_root(struct kunit *test)
{
	void *value = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init(test);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));
	KUNIT_EXPECT_TRUE(test, xa_is_node(xarray_test_head(xa)));

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX), value);
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));
}

static void xarray_test_shrink_through_chain(struct kunit *test)
{
	void *value = xa_mk_value(0x6789);
	void *other = xa_mk_value(0x789a);
	struct xarray *xa = xarray_test_init(test);
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, other,
				   GFP_KERNEL));

	head = xarray_test_head(xa);
	KUNIT_EXPECT_TRUE(test, xa_is_node(head));
	if (!xa_is_node(head))
		return;

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX), other);
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), value);
	KUNIT_EXPECT_NULL(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX));
}

static void xarray_test_zero_busy_root_clear(struct kunit *test)
{
	void *reserved = xa_mk_value(0x89ab);
	void *deep_value = xa_mk_value(0x9abc);
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC1);
	u32 id;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, 0, GFP_KERNEL), 0);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, deep_value,
				   GFP_KERNEL));
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));

	KUNIT_EXPECT_PTR_EQ(test, xa_erase(xa, XARRAY_TEST_DEEP_INDEX),
			    deep_value);
	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));
	KUNIT_EXPECT_EQ(test,
			xa_alloc(xa, &id, reserved, XA_LIMIT(0, 0), GFP_KERNEL),
			-EBUSY);
}

static void xarray_test_shrink_clears_free_mark(struct kunit *test)
{
	void *value = xa_mk_value(0xabcd);
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC);
	XA_STATE(xas, NULL, 1);
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, 0, GFP_KERNEL), 0);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 1, value, GFP_KERNEL));

	xas.xa = xa;
	xas_lock(&xas);
	KUNIT_EXPECT_PTR_EQ(test, xas_load(&xas), value);
	KUNIT_EXPECT_PTR_EQ(test, xas_store(&xas, NULL), value);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, XAS_BOUNDS);
	KUNIT_EXPECT_FALSE(test, xa->xa_flags & XA_FLAGS_MARK(XA_FREE_MARK));
	xas_unlock(&xas);

	head = xarray_test_head(xa);
	KUNIT_EXPECT_PTR_EQ(test, head, XA_ZERO_ENTRY);
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));
	KUNIT_EXPECT_FALSE(test, xa->xa_flags & XA_FLAGS_MARK(XA_FREE_MARK));

	xa_release(xa, 0);
	KUNIT_EXPECT_TRUE(test, xa_empty(xa));
}

static struct kunit_case xarray_delete_node_test_cases[] = {
	KUNIT_CASE(xarray_test_shrink_single_survivor),
	KUNIT_CASE(xarray_test_delete_parent_chain),
	KUNIT_CASE(xarray_test_clear_deep_root),
	KUNIT_CASE(xarray_test_shrink_through_chain),
	KUNIT_CASE(xarray_test_zero_busy_root_clear),
	KUNIT_CASE(xarray_test_shrink_clears_free_mark),
	{}
};

static struct kunit_suite xarray_delete_node_test_suite = {
	.name = "xarray_delete_node",
	.test_cases = xarray_delete_node_test_cases,
};

kunit_test_suite(xarray_delete_node_test_suite);
