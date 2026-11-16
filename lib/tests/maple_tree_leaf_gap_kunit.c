// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the native Maple Tree leaf-gap calculation.
 */

#include <kunit/test.h>

#include <linux/limits.h>
#include <linux/maple_tree.h>
#include <linux/maple_tree_rust.h>
#include <linux/xarray.h>

static void maple_leaf_gap_test_dense_runs(struct kunit *test)
{
	void __rcu *slots[MAPLE_NODE_SLOTS] = {};
	unsigned long last = ARRAY_SIZE(slots) - 1;
	struct maple_node node = {};
	unsigned long gap;

	RCU_INIT_POINTER(slots[0], xa_mk_value(1));
	RCU_INIT_POINTER(slots[3], xa_mk_value(2));
	RCU_INIT_POINTER(slots[7], xa_mk_value(3));
	RCU_INIT_POINTER(slots[last], xa_mk_value(4));

	gap = rust_maple_tree_leaf_gap(&node, maple_dense, 0, last, NULL,
				       slots);
	KUNIT_EXPECT_EQ(test, gap, MAPLE_NODE_SLOTS - 9);
}

static void maple_leaf_gap_test_dense_empty(struct kunit *test)
{
	void __rcu *slots[MAPLE_NODE_SLOTS] = {};
	struct maple_node node = {};
	unsigned long gap;

	gap = rust_maple_tree_leaf_gap(&node, maple_dense, 0,
				       MAPLE_NODE_SLOTS - 1, NULL, slots);
	KUNIT_EXPECT_EQ(test, gap, MAPLE_NODE_SLOTS);
}

static void maple_leaf_gap_test_left_edge(struct kunit *test)
{
	struct maple_node node = {};
	unsigned int i;
	unsigned long gap;

	node.mr64.meta.end = 4;
	node.mr64.pivot[0] = 9;
	node.mr64.pivot[1] = 19;
	node.mr64.pivot[2] = 29;
	node.mr64.pivot[3] = 39;
	node.mr64.pivot[4] = 49;
	for (i = 1; i <= 4; i++)
		RCU_INIT_POINTER(node.mr64.slot[i], xa_mk_value(i));

	gap = rust_maple_tree_leaf_gap(&node, maple_leaf_64, 0, 49,
				       node.mr64.pivot, node.mr64.slot);
	KUNIT_EXPECT_EQ(test, gap, 10);
}

static void maple_leaf_gap_test_internal(struct kunit *test)
{
	struct maple_node node = {};
	unsigned int i;
	unsigned long gap;

	node.mr64.meta.end = 4;
	node.mr64.pivot[0] = 9;
	node.mr64.pivot[1] = 19;
	node.mr64.pivot[2] = 29;
	node.mr64.pivot[3] = 39;
	node.mr64.pivot[4] = 49;
	for (i = 0; i <= 4; i++) {
		if (i != 2)
			RCU_INIT_POINTER(node.mr64.slot[i], xa_mk_value(i));
	}

	gap = rust_maple_tree_leaf_gap(&node, maple_leaf_64, 0, 49,
				       node.mr64.pivot, node.mr64.slot);
	KUNIT_EXPECT_EQ(test, gap, 10);
}

static void maple_leaf_gap_test_right_edge(struct kunit *test)
{
	struct maple_node node = {};
	unsigned int i;
	unsigned long gap;

	node.mr64.meta.end = 4;
	node.mr64.pivot[3] = 100;
	for (i = 0; i < 4; i++)
		RCU_INIT_POINTER(node.mr64.slot[i], xa_mk_value(i));

	gap = rust_maple_tree_leaf_gap(&node, maple_leaf_64, 0, ULONG_MAX,
				       node.mr64.pivot, node.mr64.slot);
	KUNIT_EXPECT_EQ(test, gap, ULONG_MAX - 100);
}

static struct kunit_case maple_leaf_gap_test_cases[] = {
	KUNIT_CASE(maple_leaf_gap_test_dense_runs),
	KUNIT_CASE(maple_leaf_gap_test_dense_empty),
	KUNIT_CASE(maple_leaf_gap_test_left_edge),
	KUNIT_CASE(maple_leaf_gap_test_internal),
	KUNIT_CASE(maple_leaf_gap_test_right_edge),
	{}
};

static struct kunit_suite maple_leaf_gap_test_suite = {
	.name = "maple_tree_leaf_gap",
	.test_cases = maple_leaf_gap_test_cases,
};

kunit_test_suite(maple_leaf_gap_test_suite);
