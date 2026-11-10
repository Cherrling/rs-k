// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the Rust implementation and C ABI adapters of the
 * intrusive red-black tree.
 */

#include <kunit/test.h>
#include <linux/minmax.h>
#include <linux/rbtree_augmented.h>

#define TEST_NODES 128

struct test_node {
	u32 key;
	u32 val;
	u32 augmented;
	struct rb_node rb;
};

struct linked_test_node {
	u32 key;
	struct rb_node_linked rb;
};

static int black_path_count(const struct rb_node *node)
{
	int count = 0;

	for (; node; node = rb_parent(node))
		count += rb_is_black(node);

	return count;
}

static void check_tree(struct kunit *test, const struct rb_root *root,
		       int expected_nodes)
{
	const struct rb_node *node;
	const struct rb_node *last_leaf = NULL;
	u32 previous = 0;
	int count = 0;
	int postorder = 0;
	int reverse = 0;

	if (!root->rb_node) {
		KUNIT_EXPECT_EQ(test, expected_nodes, 0);
		return;
	}

	KUNIT_EXPECT_TRUE(test, rb_is_black(root->rb_node));

	for (node = rb_first(root); node; node = rb_next(node)) {
		const struct test_node *entry =
			rb_entry(node, struct test_node, rb);

		if (count)
			KUNIT_EXPECT_GT(test, entry->key, previous);
		KUNIT_EXPECT_FALSE(test, rb_is_red(node) &&
					 (!rb_parent(node) ||
					  rb_is_red(rb_parent(node))));

		if (!node->rb_left || !node->rb_right)
			last_leaf = node;
		previous = entry->key;
		count++;
	}
	KUNIT_EXPECT_EQ(test, count, expected_nodes);

	if (last_leaf) {
		int expected_black = black_path_count(last_leaf);

		for (node = rb_first(root); node; node = rb_next(node)) {
			if (!node->rb_left || !node->rb_right)
				KUNIT_EXPECT_EQ(test, black_path_count(node),
						expected_black);
		}
	}

	for (node = rb_last(root); node; node = rb_prev(node))
		reverse++;
	KUNIT_EXPECT_EQ(test, reverse, expected_nodes);

	for (node = rb_first_postorder(root); node;
	     node = rb_next_postorder(node))
		postorder++;
	KUNIT_EXPECT_EQ(test, postorder, expected_nodes);
}

static void insert_node(struct test_node *node, struct rb_root *root)
{
	struct rb_node **link = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*link) {
		parent = *link;
		if (node->key <
		    rb_entry(parent, struct test_node, rb)->key)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node(&node->rb, parent, link);
	rb_insert_color(&node->rb, root);
}

static void rust_rbtree_insert_erase_test(struct kunit *test)
{
	struct test_node *nodes;
	struct rb_root root = RB_ROOT;
	int i;

	nodes = kunit_kzalloc(test, sizeof(*nodes) * TEST_NODES, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, nodes);

	for (i = 0; i < TEST_NODES; i++)
		nodes[i].key = i;

	for (i = 0; i < TEST_NODES; i++) {
		int index = (i * 37) % TEST_NODES;

		insert_node(&nodes[index], &root);
		check_tree(test, &root, i + 1);
	}

	KUNIT_EXPECT_EQ(test, rb_entry(rb_first(&root), struct test_node, rb)->key, 0);
	KUNIT_EXPECT_EQ(test, rb_entry(rb_last(&root), struct test_node, rb)->key,
			TEST_NODES - 1);

	for (i = 0; i < TEST_NODES; i++) {
		int index = (i * 53) % TEST_NODES;

		rb_erase(&nodes[index].rb, &root);
		check_tree(test, &root, TEST_NODES - i - 1);
	}
	KUNIT_EXPECT_PTR_EQ(test, root.rb_node, NULL);
}

#define NODE_VAL(node) ((node)->val)

RB_DECLARE_CALLBACKS_MAX(static, augment_callbacks,
			 struct test_node, rb, u32, augmented, NODE_VAL)

static void insert_augmented_node(struct test_node *node,
				  struct rb_root *root)
{
	struct rb_node **link = &root->rb_node;
	struct rb_node *parent = NULL;

	while (*link) {
		struct test_node *entry;

		parent = *link;
		entry = rb_entry(parent, struct test_node, rb);
		entry->augmented = max(entry->augmented, node->val);
		if (node->key < entry->key)
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	node->augmented = node->val;
	rb_link_node(&node->rb, parent, link);
	rb_insert_augmented(&node->rb, root, &augment_callbacks);
}

static void check_augmented(struct kunit *test, const struct rb_root *root,
			    int expected_nodes)
{
	const struct rb_node *node;

	check_tree(test, root, expected_nodes);
	for (node = rb_first(root); node; node = rb_next(node)) {
		const struct test_node *entry =
			rb_entry(node, struct test_node, rb);
		u32 expected = entry->val;

		if (node->rb_left)
			expected = max(expected,
				       rb_entry(node->rb_left,
						struct test_node, rb)->augmented);
		if (node->rb_right)
			expected = max(expected,
				       rb_entry(node->rb_right,
						struct test_node, rb)->augmented);
		KUNIT_EXPECT_EQ(test, entry->augmented, expected);
	}
}

static void rust_rbtree_augmented_test(struct kunit *test)
{
	struct test_node *nodes;
	struct rb_root root = RB_ROOT;
	int i;

	nodes = kunit_kzalloc(test, sizeof(*nodes) * TEST_NODES, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, nodes);

	for (i = 0; i < TEST_NODES; i++) {
		nodes[i].key = i;
		nodes[i].val = (u32)(i * 2654435761U);
	}

	for (i = 0; i < TEST_NODES; i++) {
		int index = (i * 37) % TEST_NODES;

		insert_augmented_node(&nodes[index], &root);
		check_augmented(test, &root, i + 1);
	}

	for (i = 0; i < TEST_NODES; i++) {
		int index = (i * 53) % TEST_NODES;

		rb_erase_augmented(&nodes[index].rb, &root,
				   &augment_callbacks);
		check_augmented(test, &root, TEST_NODES - i - 1);
	}
}

static struct test_node *find_node(struct rb_root *root, u32 key)
{
	struct rb_node *node = root->rb_node;

	while (node) {
		struct test_node *entry = rb_entry(node, struct test_node, rb);

		if (key < entry->key)
			node = node->rb_left;
		else if (key > entry->key)
			node = node->rb_right;
		else
			return entry;
	}
	return NULL;
}

static void rust_rbtree_replace_test(struct kunit *test)
{
	struct test_node *nodes;
	struct test_node replacement1 = { };
	struct test_node replacement2 = { };
	struct test_node replacement3 = { };
	struct test_node replacement4 = { };
	struct rb_root root = RB_ROOT;
	struct test_node *victim;
	int i;

	nodes = kunit_kzalloc(test, sizeof(*nodes) * TEST_NODES, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, nodes);

	for (i = 0; i < TEST_NODES; i++) {
		nodes[i].key = i;
		insert_node(&nodes[i], &root);
	}

	victim = find_node(&root, 31);
	KUNIT_ASSERT_NOT_NULL(test, victim);
	replacement1.key = victim->key;
	rb_replace_node(&victim->rb, &replacement1.rb, &root);
	check_tree(test, &root, TEST_NODES);

	victim = find_node(&root, 63);
	KUNIT_ASSERT_NOT_NULL(test, victim);
	replacement2.key = victim->key;
	rb_replace_node_rcu(&victim->rb, &replacement2.rb, &root);
	check_tree(test, &root, TEST_NODES);

	victim = rb_entry(root.rb_node, struct test_node, rb);
	rb_replace_node(&victim->rb, &victim->rb, &root);
	check_tree(test, &root, TEST_NODES);

	replacement3.key = victim->key;
	rb_replace_node(&victim->rb, &replacement3.rb, &root);
	check_tree(test, &root, TEST_NODES);

	replacement4.key = victim->key;
	rb_replace_node_rcu(&replacement3.rb, &replacement4.rb, &root);
	check_tree(test, &root, TEST_NODES);
}

static bool linked_less(struct rb_node *new, const struct rb_node *parent)
{
	return rb_entry(new, struct linked_test_node, rb.node)->key <
	       rb_entry(parent, struct linked_test_node, rb.node)->key;
}

static int check_linked_tree(struct kunit *test,
			     struct rb_root_linked *root,
			     int expected_nodes)
{
	struct rb_node_linked *linked = root->rb_leftmost;
	struct rb_node_linked *previous = NULL;
	struct rb_node *node;
	int linked_count = 0;

	while (linked) {
		struct linked_test_node *entry =
			container_of(linked, struct linked_test_node, rb);

		if (previous) {
			struct linked_test_node *previous_entry =
				container_of(previous, struct linked_test_node, rb);

			KUNIT_ASSERT_LT_MSG(test, previous_entry->key,
					    entry->key,
					    "linked order: previous=%u current=%u",
					    previous_entry->key, entry->key);
			KUNIT_EXPECT_PTR_EQ(test, linked->prev, previous);
		}
		previous = linked;
		linked = linked->next;
		linked_count++;
	}
	KUNIT_EXPECT_EQ(test, linked_count, expected_nodes);

	linked = root->rb_leftmost;
	for (node = rb_first(&root->rb_root); node;
	     node = rb_next(node), linked = linked->next) {
		KUNIT_ASSERT_NOT_NULL(test, linked);
		KUNIT_EXPECT_PTR_EQ(test, node, &linked->node);
	}
	KUNIT_EXPECT_PTR_EQ(test, linked, NULL);

	return linked_count;
}

static void rust_rbtree_linked_test(struct kunit *test)
{
	struct linked_test_node *nodes;
	struct rb_root_linked root = RB_ROOT_LINKED;
	int i;

	nodes = kunit_kzalloc(test, sizeof(*nodes) * TEST_NODES, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, nodes);

	for (i = 0; i < TEST_NODES; i++) {
		nodes[i].key = i;
		rb_add_linked(&nodes[i].rb, &root, linked_less);
		check_linked_tree(test, &root, i + 1);
	}

	for (i = 0; i < TEST_NODES; i++) {
		int index = (i * 53) % TEST_NODES;

		rb_erase_linked(&nodes[index].rb, &root);
		check_linked_tree(test, &root, TEST_NODES - i - 1);
	}
	KUNIT_EXPECT_PTR_EQ(test, root.rb_root.rb_node, NULL);
	KUNIT_EXPECT_PTR_EQ(test, root.rb_leftmost, NULL);
}

static struct kunit_case rust_rbtree_test_cases[] = {
	KUNIT_CASE(rust_rbtree_insert_erase_test),
	KUNIT_CASE(rust_rbtree_augmented_test),
	KUNIT_CASE(rust_rbtree_replace_test),
	KUNIT_CASE(rust_rbtree_linked_test),
	{}
};

static struct kunit_suite rust_rbtree_test_suite = {
	.name = "rust_rbtree_c_abi",
	.test_cases = rust_rbtree_test_cases,
};

kunit_test_suite(rust_rbtree_test_suite);
