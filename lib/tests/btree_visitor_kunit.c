// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for B+Tree visitors.
 */

#include <kunit/test.h>

#include <linux/btree.h>

#define BTREE_VISITOR_ENTRIES 768
#define BTREE_VISITOR_REINSERT_ENTRIES 128
#define BTREE_VISITOR_NODE_BYTES 128

struct btree_visitor_entry {
	u32 value;
};

struct btree_visitor_state {
	struct kunit *test;
	struct btree_visitor_entry *entries;
	size_t count;
};

struct btree_visitor_alloc_state {
	struct kunit *test;
	size_t allocated;
};

static void btree_visitor_destroy(void *data)
{
	struct btree_head *head = data;

	btree_destroy(head);
}

static struct btree_head32 *btree_visitor_init32(struct kunit *test)
{
	struct btree_head32 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init32(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_visitor_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head64 *btree_visitor_init64(struct kunit *test)
{
	struct btree_head64 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init64(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_visitor_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head128 *btree_visitor_init128(struct kunit *test)
{
	struct btree_head128 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init128(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_visitor_destroy,
						  &tree->h), 0);
	return tree;
}

static void *btree_visitor_alloc(gfp_t gfp, void *data)
{
	struct btree_visitor_alloc_state *state = data;
	void *node;

	node = kunit_kzalloc(state->test, BTREE_VISITOR_NODE_BYTES, gfp);
	if (node)
		state->allocated++;
	return node;
}

static void btree_visitor_free(void *element, void *data)
{
	struct btree_visitor_alloc_state *state = data;

	if (state->allocated)
		state->allocated--;
}

static struct btree_head32 *
btree_visitor_init_tracked32(struct kunit *test,
			     struct btree_visitor_alloc_state **state)
{
	struct btree_head32 *tree;
	mempool_t *mempool;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	*state = kunit_kzalloc(test, sizeof(**state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, *state);
	(*state)->test = test;
	mempool = mempool_create(0, btree_visitor_alloc, btree_visitor_free,
				 *state);
	KUNIT_ASSERT_NOT_NULL(test, mempool);
	btree_init_mempool32(tree, mempool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_visitor_destroy,
						  &tree->h), 0);
	return tree;
}

static void btree_visitor32_callback(void *elem, unsigned long opaque,
				     u32 key, size_t index)
{
	struct btree_visitor_state *state = (void *)opaque;
	size_t item;

	KUNIT_ASSERT_LT(state->test, index, BTREE_VISITOR_ENTRIES);
	item = BTREE_VISITOR_ENTRIES - 1 - index;
	KUNIT_EXPECT_EQ(state->test, key, item * 4);
	KUNIT_EXPECT_PTR_EQ(state->test, elem, &state->entries[item].value);
	state->count++;
}

static void btree_visitor64_callback(void *elem, unsigned long opaque,
				     u64 key, size_t index)
{
	struct btree_visitor_state *state = (void *)opaque;
	size_t item;

	KUNIT_ASSERT_LT(state->test, index, BTREE_VISITOR_ENTRIES);
	item = BTREE_VISITOR_ENTRIES - 1 - index;
	KUNIT_EXPECT_EQ(state->test, key, item * 3ULL);
	KUNIT_EXPECT_PTR_EQ(state->test, elem, &state->entries[item].value);
	state->count++;
}

static void btree_visitor128_callback(void *elem, unsigned long opaque,
				      u64 key1, u64 key2, size_t index)
{
	struct btree_visitor_state *state = (void *)opaque;
	size_t item;

	KUNIT_ASSERT_LT(state->test, index, BTREE_VISITOR_ENTRIES);
	item = BTREE_VISITOR_ENTRIES - 1 - index;
	KUNIT_EXPECT_EQ(state->test, key1, item * 3ULL);
	KUNIT_EXPECT_EQ(state->test, key2, item * 5ULL);
	KUNIT_EXPECT_PTR_EQ(state->test, elem, &state->entries[item].value);
	state->count++;
}

static void btree_visitor32_test(struct kunit *test)
{
	struct btree_visitor_state state;
	struct btree_head32 *tree = btree_visitor_init32(test);
	struct btree_visitor_entry *entries;
	int initial_height;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_VISITOR_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);
	for (i = 0; i < BTREE_VISITOR_ENTRIES; i++) {
		int index = (i * 37) % BTREE_VISITOR_ENTRIES;

		entries[index].value = index + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, index * 4,
					       &entries[index].value, GFP_KERNEL),
				0);
	}
	initial_height = tree->h.height;
	KUNIT_ASSERT_GT(test, initial_height, 2);

	state = (struct btree_visitor_state) {
		.test = test,
		.entries = entries,
	};
	KUNIT_EXPECT_EQ(test,
			btree_visitor32(tree, (unsigned long)&state,
					btree_visitor32_callback),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, state.count, BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, tree->h.height, initial_height);
	for (i = 0; i < BTREE_VISITOR_ENTRIES; i++)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(tree, i * 4),
				    &entries[i].value);
}

static void btree_visitor64_test(struct kunit *test)
{
	struct btree_visitor_state state;
	struct btree_head64 *tree = btree_visitor_init64(test);
	struct btree_visitor_entry *entries;
	int initial_height;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_VISITOR_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);
	for (i = 0; i < BTREE_VISITOR_ENTRIES; i++) {
		int index = (i * 53) % BTREE_VISITOR_ENTRIES;

		entries[index].value = index + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert64(tree, index * 3ULL,
					       &entries[index].value, GFP_KERNEL),
				0);
	}
	initial_height = tree->h.height;
	KUNIT_ASSERT_GT(test, initial_height, 2);

	state = (struct btree_visitor_state) {
		.test = test,
		.entries = entries,
	};
	KUNIT_EXPECT_EQ(test,
			btree_visitor64(tree, (unsigned long)&state,
					btree_visitor64_callback),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, state.count, BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test,
			btree_visitor64(tree, (unsigned long)&state, NULL),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, tree->h.height, initial_height);
}

static void btree_visitor128_test(struct kunit *test)
{
	struct btree_visitor_state state;
	struct btree_head128 *tree = btree_visitor_init128(test);
	struct btree_visitor_entry *entries;
	int initial_height;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_VISITOR_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);
	for (i = 0; i < BTREE_VISITOR_ENTRIES; i++) {
		int index = (i * 59) % BTREE_VISITOR_ENTRIES;

		entries[index].value = index + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert128(tree, index * 3ULL, index * 5ULL,
						&entries[index].value, GFP_KERNEL),
				0);
	}
	initial_height = tree->h.height;
	KUNIT_ASSERT_GT(test, initial_height, 2);

	state = (struct btree_visitor_state) {
		.test = test,
		.entries = entries,
	};
	KUNIT_EXPECT_EQ(test,
			btree_visitor128(tree, (unsigned long)&state,
					 btree_visitor128_callback),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, state.count, BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, tree->h.height, initial_height);
}

static void btree_visitor_grim32_test(struct kunit *test)
{
	struct btree_visitor_alloc_state *alloc_state;
	struct btree_visitor_state state;
	struct btree_head32 *tree;
	struct btree_visitor_entry *entries;
	int i;

	tree = btree_visitor_init_tracked32(test, &alloc_state);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_VISITOR_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);
	KUNIT_EXPECT_EQ(test, alloc_state->allocated, 1);

	for (i = 0; i < BTREE_VISITOR_ENTRIES; i++) {
		int index = (i * 193) % BTREE_VISITOR_ENTRIES;

		entries[index].value = index + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, index * 4,
					       &entries[index].value, GFP_KERNEL),
				0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);
	KUNIT_ASSERT_GT(test, alloc_state->allocated, 1);
	state = (struct btree_visitor_state) {
		.test = test,
		.entries = entries,
	};
	KUNIT_EXPECT_EQ(test,
			btree_visitor32(tree, (unsigned long)&state,
					btree_visitor32_callback),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, state.count, BTREE_VISITOR_ENTRIES);

	state = (struct btree_visitor_state) {
		.test = test,
		.entries = entries,
	};
	KUNIT_EXPECT_EQ(test,
			btree_grim_visitor32(tree, (unsigned long)&state,
					     btree_visitor32_callback),
			BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_EQ(test, state.count, BTREE_VISITOR_ENTRIES);
	KUNIT_EXPECT_NULL(test, tree->h.node);
	KUNIT_EXPECT_EQ(test, tree->h.height, 0);
	KUNIT_EXPECT_EQ(test, alloc_state->allocated, 1);
	KUNIT_EXPECT_EQ(test, btree_grim_visitor32(tree, 0, NULL), 0);
	KUNIT_EXPECT_EQ(test, alloc_state->allocated, 1);

	for (i = 0; i < BTREE_VISITOR_REINSERT_ENTRIES; i++) {
		entries[i].value = i + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, i,
					       &entries[i].value, GFP_KERNEL), 0);
	}
	KUNIT_EXPECT_EQ(test, btree_grim_visitor32(tree, 0, NULL),
			BTREE_VISITOR_REINSERT_ENTRIES);
	KUNIT_EXPECT_NULL(test, tree->h.node);
	KUNIT_EXPECT_EQ(test, tree->h.height, 0);
	KUNIT_EXPECT_EQ(test, alloc_state->allocated, 1);
}

static struct kunit_case btree_visitor_test_cases[] = {
	KUNIT_CASE(btree_visitor32_test),
	KUNIT_CASE(btree_visitor64_test),
	KUNIT_CASE(btree_visitor128_test),
	KUNIT_CASE(btree_visitor_grim32_test),
	{}
};

static struct kunit_suite btree_visitor_test_suite = {
	.name = "btree_visitor",
	.test_cases = btree_visitor_test_cases,
};

kunit_test_suite(btree_visitor_test_suite);
