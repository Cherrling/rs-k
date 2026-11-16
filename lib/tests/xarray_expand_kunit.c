// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray root expansion.
 */

#include <kunit/test.h>

#include <linux/slab.h>
#include <linux/xarray.h>

#define XARRAY_TEST_DEEP_INDEX (XA_CHUNK_SIZE * XA_CHUNK_SIZE)

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

static void xarray_test_check_chain(struct kunit *test, struct xarray *xa)
{
	XA_STATE(xas, xa, XARRAY_TEST_DEEP_INDEX);
	struct xa_node *leaf;
	struct xa_node *node;
	struct xa_node *root;
	void *head = xarray_test_head(xa);
	void *loaded;
	unsigned int shift = 0;

	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);

	xas_lock(&xas);
	loaded = xas_load(&xas);
	leaf = xas.xa_node;
	xas_unlock(&xas);

	KUNIT_ASSERT_PTR_EQ(test, loaded, xa_mk_value(0x1234));
	KUNIT_ASSERT_NOT_NULL(test, leaf);
	KUNIT_EXPECT_EQ(test, leaf->shift, 0);
	KUNIT_EXPECT_EQ(test, leaf->nr_values, 1);

	for (node = leaf; node; node = xa_parent_locked(xa, node)) {
		struct xa_node *parent = xa_parent_locked(xa, node);

		KUNIT_EXPECT_EQ(test, node->shift, shift);
		if (parent)
			KUNIT_EXPECT_PTR_EQ(test,
					    xa_entry_locked(xa, parent,
							    node->offset),
					    xa_mk_node(node));
		shift += XA_CHUNK_SHIFT;
	}

	KUNIT_EXPECT_GE(test, root->shift, 2 * XA_CHUNK_SHIFT);
}

static void xarray_test_expand_empty_root(struct kunit *test)
{
	void *value = xa_mk_value(0x1234);
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *root;
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, value,
				   GFP_KERNEL));

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_EQ(test, root->count, 1);
	KUNIT_EXPECT_EQ(test, root->nr_values, 0);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX), value);
	xarray_test_check_chain(test, xa);
}

static void xarray_test_expand_wraps_value_root(struct kunit *test)
{
	void *low_value = xa_mk_value(0x2345);
	void *deep_value = xa_mk_value(0x3456);
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *root;
	struct xa_node *low_leaf;
	XA_STATE(xas, xa, 0);
	void *loaded;
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, low_value, GFP_KERNEL));
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, deep_value,
				   GFP_KERNEL));

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_EQ(test, root->count, 2);
	KUNIT_EXPECT_EQ(test, root->nr_values, 0);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, 0), low_value);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX), deep_value);

	xas_lock(&xas);
	loaded = xas_load(&xas);
	low_leaf = xas.xa_node;
	xas_unlock(&xas);

	KUNIT_ASSERT_PTR_EQ(test, loaded, low_value);
	KUNIT_ASSERT_NOT_NULL(test, low_leaf);
	KUNIT_EXPECT_EQ(test, low_leaf->nr_values, 1);
}

static void xarray_test_expand_propagates_mark(struct kunit *test)
{
	int marker;
	void *deep_value = xa_mk_value(0x4567);
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *root;
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, &marker, GFP_KERNEL));
	xa_set_mark(xa, 0, XA_MARK_0);

	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, deep_value,
				   GFP_KERNEL));

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_MARK_0));
	KUNIT_EXPECT_TRUE(test, test_bit(0, root->marks[XA_MARK_0]));
	KUNIT_EXPECT_TRUE(test, xa_get_mark(xa, 0, XA_MARK_0));
	KUNIT_EXPECT_FALSE(test,
			   xa_get_mark(xa, XARRAY_TEST_DEEP_INDEX, XA_MARK_0));
}

static void xarray_test_expand_track_free_slots(struct kunit *test)
{
	void *deep_value = xa_mk_value(0x5678);
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC);
	struct xa_node *root;
	void *head;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_EQ(test, xa_reserve(xa, 0, GFP_KERNEL), 0);
	KUNIT_ASSERT_NULL(test,
			  xa_store(xa, XARRAY_TEST_DEEP_INDEX, deep_value,
				   GFP_KERNEL));

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_FREE_MARK));
	KUNIT_EXPECT_TRUE(test, test_bit(0, root->marks[XA_FREE_MARK]));
	KUNIT_EXPECT_TRUE(test,
			  test_bit(XA_CHUNK_MASK, root->marks[XA_FREE_MARK]));
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, XARRAY_TEST_DEEP_INDEX), deep_value);
}

static struct kunit_case xarray_expand_test_cases[] = {
	KUNIT_CASE(xarray_test_expand_empty_root),
	KUNIT_CASE(xarray_test_expand_wraps_value_root),
	KUNIT_CASE(xarray_test_expand_propagates_mark),
	KUNIT_CASE(xarray_test_expand_track_free_slots),
	{}
};

static struct kunit_suite xarray_expand_test_suite = {
	.name = "xarray_expand",
	.test_cases = xarray_expand_test_cases,
};

kunit_test_suite(xarray_expand_test_suite);
