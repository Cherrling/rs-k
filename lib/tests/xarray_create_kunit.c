// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for native Rust XArray node-chain creation.
 */

#include <kunit/test.h>

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/xarray-rust.h>

#define XARRAY_TEST_DEEP_INDEX (XA_CHUNK_SIZE * XA_CHUNK_SIZE + 17)

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

static void *xarray_test_create(struct xa_state *xas, bool allow_root)
{
	void *entry;

	xas_lock(xas);
	entry = rust_xarray_create_for_store(xas, allow_root);
	xas_unlock(xas);

	return entry;
}

static void xarray_test_create_direct_root(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *marker = xa_mk_value(0x1234);
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	entry = xarray_test_create(&xas, true);
	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_NULL(test, xarray_test_head(xa));
	KUNIT_EXPECT_NULL(test, xas.xa_node);

	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, marker, GFP_KERNEL));
	xas_reset(&xas);
	entry = xarray_test_create(&xas, true);
	KUNIT_EXPECT_PTR_EQ(test, entry, marker);
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), marker);
	KUNIT_EXPECT_NULL(test, xas.xa_node);
}

static void xarray_test_create_deep_chain(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x2345);
	unsigned long index = XARRAY_TEST_DEEP_INDEX;
	struct xa_node *leaf;
	struct xa_node *node;
	struct xa_node *root;
	struct xa_node *top;
	unsigned int shift = 0;
	bool chain_valid = true;
	void *head;
	XA_STATE(xas, xa, index);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	entry = xarray_test_create(&xas, true);
	KUNIT_EXPECT_NULL(test, entry);

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	leaf = xas.xa_node;
	KUNIT_ASSERT_NOT_NULL(test, leaf);
	KUNIT_EXPECT_EQ(test, leaf->shift, 0);
	KUNIT_EXPECT_EQ(test, xas.xa_offset, index & XA_CHUNK_MASK);

	xas_lock(&xas);
	for (node = leaf; node; node = xa_parent_locked(xa, node)) {
		struct xa_node *parent = xa_parent_locked(xa, node);

		top = node;
		if (node->shift != shift) {
			chain_valid = false;
			break;
		}
		if (parent &&
		    xa_entry_locked(xa, parent, node->offset) != xa_mk_node(node)) {
			chain_valid = false;
			break;
		}
		shift += XA_CHUNK_SHIFT;
	}
	xas_unlock(&xas);

	KUNIT_EXPECT_TRUE(test, chain_valid);
	KUNIT_EXPECT_PTR_EQ(test, root, top);
	KUNIT_EXPECT_GE(test, root->shift, 2 * XA_CHUNK_SHIFT);

	xas_lock(&xas);
	entry = xas_store(&xas, value);
	xas_unlock(&xas);
	KUNIT_EXPECT_NULL(test, entry);
	KUNIT_EXPECT_PTR_EQ(test, xa_load(xa, index), value);
}

static void xarray_test_create_allow_root_false(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	struct xa_node *root;
	void *head;
	XA_STATE(xas, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	entry = xarray_test_create(&xas, false);
	KUNIT_EXPECT_NULL(test, entry);

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_EQ(test, root->shift, 0);
	KUNIT_EXPECT_EQ(test, root->count, 0);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, root);
	KUNIT_EXPECT_EQ(test, xas.xa_offset, 0);
}

static void xarray_test_create_existing_chain(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x3456);
	unsigned long index = XARRAY_TEST_DEEP_INDEX;
	struct xa_node *old_leaf;
	void *old_head;
	void *loaded;
	XA_STATE(old, xa, index);
	XA_STATE(xas, xa, index);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, index, value, GFP_KERNEL));
	old_head = xarray_test_head(xa);

	xas_lock(&old);
	loaded = xas_load(&old);
	old_leaf = old.xa_node;
	xas_unlock(&old);
	KUNIT_ASSERT_PTR_EQ(test, loaded, value);
	KUNIT_ASSERT_NOT_NULL(test, old_leaf);

	entry = xarray_test_create(&xas, true);
	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), old_head);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, old_leaf);
	KUNIT_EXPECT_EQ(test, xas.xa_offset, index & XA_CHUNK_MASK);
}

static void xarray_test_create_direct_ancestor(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *value = xa_mk_value(0x4567);
	unsigned long first = XA_CHUNK_SIZE;
	struct xa_node *root;
	void *head;
	XA_STATE(xas, xa, first + 7);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test,
			  xa_store_range(xa, first, first + XA_CHUNK_SIZE - 1,
					 value, GFP_KERNEL));
	entry = xarray_test_create(&xas, false);

	head = xarray_test_head(xa);
	KUNIT_ASSERT_TRUE(test, xa_is_node(head));
	root = xa_to_node(head);
	KUNIT_EXPECT_PTR_EQ(test, entry, value);
	KUNIT_EXPECT_EQ(test, root->shift, XA_CHUNK_SHIFT);
	KUNIT_EXPECT_EQ(test, root->count, 1);
	KUNIT_EXPECT_EQ(test, xas.xa_offset, 1);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, root);
}

static void xarray_test_create_zero_busy(struct kunit *test)
{
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC1);
	unsigned long index = XARRAY_TEST_DEEP_INDEX;
	XA_STATE(xas, xa, index);
	XA_STATE(low, xa, 0);
	void *entry;

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xarray_test_create(&xas, true));
	KUNIT_ASSERT_TRUE(test, xa_is_node(xarray_test_head(xa)));
	KUNIT_EXPECT_NULL(test, xa_load(xa, 0));

	xas_lock(&low);
	entry = xas_load(&low);
	xas_unlock(&low);
	KUNIT_EXPECT_TRUE(test, xa_is_zero(entry));
}

static void xarray_test_create_free_marks(struct kunit *test)
{
	struct xarray *xa = xarray_test_init_flags(test, XA_FLAGS_ALLOC);
	struct xa_node *node;
	bool marks_valid = true;
	XA_STATE(xas, xa, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xarray_test_create(&xas, true));
	KUNIT_ASSERT_NOT_NULL(test, xas.xa_node);

	xas_lock(&xas);
	for (node = xas.xa_node; node; node = xa_parent_locked(xa, node)) {
		if (!bitmap_full(node->marks[XA_FREE_MARK], XA_CHUNK_SIZE)) {
			marks_valid = false;
			break;
		}
	}
	xas_unlock(&xas);

	KUNIT_EXPECT_TRUE(test, marks_valid);
	KUNIT_EXPECT_TRUE(test, xa_marked(xa, XA_FREE_MARK));
}

static void xarray_test_create_error_state(struct kunit *test)
{
	struct xarray *xa = xarray_test_init(test);
	void *marker = xa_mk_value(0x5678);
	void *head;
	struct xa_node *error;
	XA_STATE(xas, xa, XARRAY_TEST_DEEP_INDEX);

	KUNIT_ASSERT_NOT_NULL(test, xa);
	KUNIT_ASSERT_NULL(test, xa_store(xa, 0, marker, GFP_KERNEL));
	head = xarray_test_head(xa);
	xas_set_err(&xas, -EIO);
	error = xas.xa_node;

	KUNIT_EXPECT_NULL(test, xarray_test_create(&xas, true));
	KUNIT_EXPECT_PTR_EQ(test, xarray_test_head(xa), head);
	KUNIT_EXPECT_PTR_EQ(test, xas.xa_node, error);
	KUNIT_EXPECT_EQ(test, xas_error(&xas), -EIO);
}

static struct kunit_case xarray_create_test_cases[] = {
	KUNIT_CASE(xarray_test_create_direct_root),
	KUNIT_CASE(xarray_test_create_deep_chain),
	KUNIT_CASE(xarray_test_create_allow_root_false),
	KUNIT_CASE(xarray_test_create_existing_chain),
	KUNIT_CASE(xarray_test_create_direct_ancestor),
	KUNIT_CASE(xarray_test_create_zero_busy),
	KUNIT_CASE(xarray_test_create_free_marks),
	KUNIT_CASE(xarray_test_create_error_state),
	{}
};

static struct kunit_suite xarray_create_test_suite = {
	.name = "xarray_create",
	.test_cases = xarray_create_test_cases,
};

kunit_test_suite(xarray_create_test_suite);
