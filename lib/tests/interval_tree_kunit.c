// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for interval tree implementations.
 */

#include <kunit/test.h>

#include <linux/interval_tree.h>
#include <linux/limits.h>
#include <linux/minmax.h>
#include <linux/module.h>
#include <linux/rbtree_augmented.h>
#include <linux/string.h>

#define INTERVAL_TEST_ORDERED_NODES 9
#define INTERVAL_TEST_RANDOM_NODES 97
#define INTERVAL_TEST_RANDOM_ITERATIONS 2000

struct interval_test_node {
	struct interval_tree_node tree;
	unsigned int serial;
};

struct interval_test_span {
	bool hole;
	unsigned long first;
	unsigned long last;
};

static struct interval_test_node *
interval_test_alloc_nodes(struct kunit *test, size_t count)
{
	struct interval_test_node *nodes;
	size_t i;

	nodes = kunit_kcalloc(test, count, sizeof(*nodes), GFP_KERNEL);
	if (!nodes)
		return NULL;

	for (i = 0; i < count; i++)
		nodes[i].serial = i;

	return nodes;
}

static int interval_test_black_height(struct rb_node *node)
{
	struct rb_node *left = node->rb_left;
	struct rb_node *right = node->rb_right;
	int left_height;
	int right_height;

	if (rb_is_red(node) &&
	    ((left && rb_is_red(left)) || (right && rb_is_red(right))))
		return -1;

	if ((left && rb_parent(left) != node) ||
	    (right && rb_parent(right) != node))
		return -1;

	left_height = left ? interval_test_black_height(left) : 1;
	right_height = right ? interval_test_black_height(right) : 1;
	if (left_height < 0 || left_height != right_height)
		return -1;

	return left_height + rb_is_black(node);
}

static unsigned long
interval_test_validate_subtree(struct kunit *test, struct rb_node *rb,
			       bool *valid)
{
	struct interval_tree_node *node;
	unsigned long maximum;

	if (!rb)
		return 0;

	node = rb_entry(rb, struct interval_tree_node, rb);
	maximum = node->last;

	if (rb->rb_left) {
		struct interval_tree_node *left;
		unsigned long subtree_last;

		left = rb_entry(rb->rb_left, struct interval_tree_node, rb);
		subtree_last = interval_test_validate_subtree(test, rb->rb_left,
							      valid);
		if (left->start > node->start)
			*valid = false;
		maximum = max(maximum, subtree_last);
	}

	if (rb->rb_right) {
		struct interval_tree_node *right;
		unsigned long subtree_last;

		right = rb_entry(rb->rb_right, struct interval_tree_node, rb);
		subtree_last = interval_test_validate_subtree(test, rb->rb_right,
							      valid);
		if (right->start < node->start)
			*valid = false;
		maximum = max(maximum, subtree_last);
	}

	if (node->__subtree_last != maximum)
		*valid = false;

	return maximum;
}

static bool interval_test_validate_tree(struct kunit *test,
					struct rb_root_cached *root)
{
	struct rb_node *leftmost;
	unsigned long subtree_last;
	bool valid = true;

	if (!root->rb_root.rb_node) {
		KUNIT_EXPECT_PTR_EQ(test, root->rb_leftmost, NULL);
		return !root->rb_leftmost;
	}

	KUNIT_EXPECT_TRUE(test, rb_is_black(root->rb_root.rb_node));
	KUNIT_EXPECT_PTR_EQ(test, rb_parent(root->rb_root.rb_node), NULL);
	KUNIT_EXPECT_GT(test, interval_test_black_height(root->rb_root.rb_node),
			0);

	leftmost = root->rb_root.rb_node;
	while (leftmost->rb_left)
		leftmost = leftmost->rb_left;
	KUNIT_EXPECT_PTR_EQ(test, root->rb_leftmost, leftmost);

	subtree_last = interval_test_validate_subtree(test, root->rb_root.rb_node,
						      &valid);
	KUNIT_EXPECT_TRUE(test, valid);
	KUNIT_EXPECT_EQ(test, subtree_last,
			rb_entry(root->rb_root.rb_node,
				 struct interval_tree_node, rb)->__subtree_last);

	return valid;
}

static bool interval_test_model_overlap(const struct interval_test_node *node,
					unsigned long start,
					unsigned long last)
{
	return node->tree.start <= last && start <= node->tree.last;
}

static bool interval_test_validate_query(struct kunit *test,
					 struct rb_root_cached *root,
					 struct interval_test_node *nodes,
					 const bool *present, size_t node_count,
					 unsigned long start, unsigned long last)
{
	bool *actual;
	bool *expected;
	struct interval_tree_node *node;
	unsigned long previous_start = 0;
	size_t actual_count = 0;
	size_t expected_count = 0;
	size_t i;

	actual = kunit_kcalloc(test, node_count, sizeof(*actual), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, actual);
	expected = kunit_kcalloc(test, node_count, sizeof(*expected), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, expected);

	for (i = 0; i < node_count; i++) {
		if (present[i] &&
		    interval_test_model_overlap(&nodes[i], start, last)) {
			expected[i] = true;
			expected_count++;
		}
	}

	for (node = interval_tree_iter_first(root, start, last); node;
	     node = interval_tree_iter_next(node, start, last)) {
		struct interval_test_node *entry;

		entry = container_of(node, struct interval_test_node, tree);
		KUNIT_ASSERT_LT(test, entry->serial, node_count);
		if (actual[entry->serial]) {
			KUNIT_FAIL(test, "duplicate interval %u in query",
				   entry->serial);
			return false;
		}
		if (actual_count && node->start < previous_start)
			KUNIT_FAIL(test, "query iteration order decreased");
		actual[entry->serial] = true;
		previous_start = node->start;
		actual_count++;
	}

	KUNIT_EXPECT_EQ(test, actual_count, expected_count);
	KUNIT_EXPECT_MEMEQ(test, actual, expected, node_count * sizeof(*actual));
	return actual_count == expected_count &&
	       !memcmp(actual, expected, node_count * sizeof(*actual));
}

static void interval_test_ordered(struct kunit *test)
{
	static const struct {
		unsigned long start;
		unsigned long last;
	} intervals[INTERVAL_TEST_ORDERED_NODES] = {
		{ 10, 20 },
		{ 0, 5 },
		{ 30, 40 },
		{ 5, 10 },
		{ 15, 35 },
		{ 50, 60 },
		{ 70, 80 },
		{ 20, 30 },
		{ 40, 45 },
	};
	struct interval_test_node *nodes;
	struct rb_root_cached root = RB_ROOT_CACHED;
	bool present[INTERVAL_TEST_ORDERED_NODES] = {};
	struct interval_tree_node *root_node;
	struct interval_tree_node *found;
	size_t i;

	nodes = interval_test_alloc_nodes(test, INTERVAL_TEST_ORDERED_NODES);
	KUNIT_ASSERT_NOT_NULL(test, nodes);
	KUNIT_EXPECT_TRUE(test, interval_test_validate_tree(test, &root));

	for (i = 0; i < INTERVAL_TEST_ORDERED_NODES; i++) {
		nodes[i].tree.start = intervals[i].start;
		nodes[i].tree.last = intervals[i].last;
		interval_tree_insert(&nodes[i].tree, &root);
		present[i] = true;
		KUNIT_ASSERT_TRUE(test,
				  interval_test_validate_tree(test, &root));
	}

	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_query(test, &root, nodes,
						       present,
						       INTERVAL_TEST_ORDERED_NODES,
						       0, ULONG_MAX));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_query(test, &root, nodes,
						       present,
						       INTERVAL_TEST_ORDERED_NODES,
						       5, 5));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_query(test, &root, nodes,
						       present,
						       INTERVAL_TEST_ORDERED_NODES,
						       21, 29));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_query(test, &root, nodes,
						       present,
						       INTERVAL_TEST_ORDERED_NODES,
						       81, ULONG_MAX));

	root_node = rb_entry(root.rb_root.rb_node, struct interval_tree_node, rb);
	found = interval_tree_subtree_search(root_node, 8, 12);
	KUNIT_EXPECT_PTR_EQ(test, found,
			    interval_tree_iter_first(&root, 8, 12));

	for (i = 0; i < INTERVAL_TEST_ORDERED_NODES; i++) {
		interval_tree_remove(&nodes[i].tree, &root);
		present[i] = false;
		KUNIT_ASSERT_TRUE(test,
				  interval_test_validate_tree(test, &root));
	}

	KUNIT_EXPECT_PTR_EQ(test, interval_tree_iter_first(&root, 0, ULONG_MAX),
			    NULL);
}

static bool interval_test_validate_spans(struct kunit *test,
					 struct rb_root_cached *root,
					 unsigned long first,
					 unsigned long last,
					 const struct interval_test_span *expected,
					 size_t expected_count)
{
	struct interval_tree_span_iter iter;
	size_t count = 0;

	interval_tree_span_iter_first(&iter, root, first, last);
	while (!interval_tree_span_iter_done(&iter)) {
		unsigned long actual_first;
		unsigned long actual_last;
		bool hole;

		if (count >= expected_count) {
			KUNIT_FAIL(test, "too many spans");
			return false;
		}

		hole = iter.is_hole;
		if (hole) {
			actual_first = iter.start_hole;
			actual_last = iter.last_hole;
		} else {
			actual_first = iter.start_used;
			actual_last = iter.last_used;
		}
		if (hole != expected[count].hole ||
		    actual_first != expected[count].first ||
		    actual_last != expected[count].last) {
			KUNIT_FAIL(test,
				   "span %zu mismatch: hole=%d [%lu,%lu]",
				   count, hole, actual_first, actual_last);
			return false;
		}

		interval_tree_span_iter_next(&iter);
		count++;
	}

	KUNIT_EXPECT_EQ(test, count, expected_count);
	return count == expected_count;
}

static void interval_test_spans(struct kunit *test)
{
	static const struct {
		unsigned long start;
		unsigned long last;
	} intervals[] = {
		{ 0, 2 },
		{ 3, 4 },
		{ 8, 12 },
		{ 20, 25 },
		{ 23, 28 },
	};
	static const struct interval_test_span full_expected[] = {
		{ false, 0, 4 },
		{ true, 5, 7 },
		{ false, 8, 12 },
		{ true, 13, 19 },
		{ false, 20, 28 },
		{ true, 29, 30 },
	};
	static const struct interval_test_span middle_expected[] = {
		{ true, 6, 7 },
		{ false, 8, 12 },
		{ true, 13, 19 },
		{ false, 20, 20 },
	};
	static const struct interval_test_span used_expected[] = {
		{ false, 0, 4 },
	};
	struct interval_test_node *nodes;
	struct rb_root_cached root = RB_ROOT_CACHED;
	struct interval_tree_span_iter iter;
	size_t i;

	nodes = interval_test_alloc_nodes(test, ARRAY_SIZE(intervals));
	KUNIT_ASSERT_NOT_NULL(test, nodes);
	for (i = 0; i < ARRAY_SIZE(intervals); i++) {
		nodes[i].tree.start = intervals[i].start;
		nodes[i].tree.last = intervals[i].last;
		interval_tree_insert(&nodes[i].tree, &root);
	}
	KUNIT_ASSERT_TRUE(test, interval_test_validate_tree(test, &root));

	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_spans(test, &root, 0, 30,
						       full_expected,
						       ARRAY_SIZE(full_expected)));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_spans(test, &root, 6, 20,
						       middle_expected,
						       ARRAY_SIZE(middle_expected)));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_spans(test, &root, 0, 4,
						       used_expected,
						       ARRAY_SIZE(used_expected)));
	KUNIT_ASSERT_TRUE(test,
			  interval_test_validate_spans(test, &root, 100, 110,
						       (struct interval_test_span[]) {
							       { true, 100, 110 },
						       }, 1));

	interval_tree_span_iter_first(&iter, &root, 0, 30);
	KUNIT_EXPECT_FALSE(test, iter.is_hole);
	KUNIT_EXPECT_EQ(test, iter.start_used, 0UL);
	KUNIT_EXPECT_EQ(test, iter.last_used, 4UL);

	interval_tree_span_iter_advance(&iter, &root, 2);
	KUNIT_EXPECT_FALSE(test, iter.is_hole);
	KUNIT_EXPECT_EQ(test, iter.start_used, 2UL);
	KUNIT_EXPECT_EQ(test, iter.last_used, 4UL);

	interval_tree_span_iter_advance(&iter, &root, 5);
	KUNIT_EXPECT_TRUE(test, iter.is_hole);
	KUNIT_EXPECT_EQ(test, iter.start_hole, 5UL);
	KUNIT_EXPECT_EQ(test, iter.last_hole, 7UL);

	interval_tree_span_iter_advance(&iter, &root, 6);
	KUNIT_EXPECT_TRUE(test, iter.is_hole);
	KUNIT_EXPECT_EQ(test, iter.start_hole, 6UL);

	interval_tree_span_iter_advance(&iter, &root, 8);
	KUNIT_EXPECT_FALSE(test, iter.is_hole);
	KUNIT_EXPECT_EQ(test, iter.start_used, 8UL);
	KUNIT_EXPECT_EQ(test, iter.last_used, 12UL);

	interval_tree_span_iter_advance(&iter, &root, 31);
	KUNIT_EXPECT_TRUE(test, interval_tree_span_iter_done(&iter));

	for (i = 0; i < ARRAY_SIZE(intervals); i++)
		interval_tree_remove(&nodes[i].tree, &root);
	KUNIT_EXPECT_TRUE(test, interval_test_validate_tree(test, &root));
}

static void interval_test_random_model(struct kunit *test)
{
	struct interval_test_node *nodes;
	struct rb_root_cached root = RB_ROOT_CACHED;
	bool *present;
	unsigned int state = 0x193939;
	size_t present_count = 0;
	size_t final_present = 0;
	size_t i;

	nodes = interval_test_alloc_nodes(test, INTERVAL_TEST_RANDOM_NODES);
	KUNIT_ASSERT_NOT_NULL(test, nodes);
	present = kunit_kcalloc(test, INTERVAL_TEST_RANDOM_NODES,
				sizeof(*present), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, present);

	for (i = 0; i < INTERVAL_TEST_RANDOM_ITERATIONS; i++) {
		unsigned int serial;
		unsigned long query_start;
		unsigned long query_last;

		state = state * 193939 % 47629;
		serial = state % INTERVAL_TEST_RANDOM_NODES;
		if (present[serial]) {
			interval_tree_remove(&nodes[serial].tree, &root);
			present[serial] = false;
			present_count--;
		} else {
			state = state * 193939 % 47629;
			nodes[serial].tree.start = state % 64;
			state = state * 193939 % 47629;
			nodes[serial].tree.last =
				nodes[serial].tree.start + state % 16;
			interval_tree_insert(&nodes[serial].tree, &root);
			present[serial] = true;
			present_count++;
		}

		if (!interval_test_validate_tree(test, &root)) {
			KUNIT_FAIL(test, "tree mismatch after iteration %zu", i);
			return;
		}

		state = state * 193939 % 47629;
		query_start = state % 80;
		state = state * 193939 % 47629;
		query_last = query_start + state % 20;
		if (!interval_test_validate_query(test, &root, nodes, present,
						  INTERVAL_TEST_RANDOM_NODES,
						  query_start, query_last)) {
			KUNIT_FAIL(test, "query mismatch after iteration %zu", i);
			return;
		}
	}

	for (i = 0; i < INTERVAL_TEST_RANDOM_NODES; i++)
		if (present[i])
			final_present++;
	KUNIT_EXPECT_EQ(test, present_count, final_present);

	for (i = 0; i < INTERVAL_TEST_RANDOM_NODES; i++) {
		if (!present[i])
			continue;
		interval_tree_remove(&nodes[i].tree, &root);
		present[i] = false;
		if (!interval_test_validate_tree(test, &root)) {
			KUNIT_FAIL(test, "tree mismatch after draining node %zu", i);
			return;
		}
	}
}

static struct kunit_case interval_test_cases[] = {
	KUNIT_CASE(interval_test_ordered),
	KUNIT_CASE(interval_test_spans),
	KUNIT_CASE(interval_test_random_model),
	{}
};

static struct kunit_suite interval_test_suite = {
	.name = "interval_tree",
	.test_cases = interval_test_cases,
};

kunit_test_suite(interval_test_suite);

MODULE_DESCRIPTION("Interval tree KUnit tests");
MODULE_LICENSE("GPL");
