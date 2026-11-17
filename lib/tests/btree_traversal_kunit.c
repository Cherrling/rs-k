// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for B+Tree traversal and leaf updates.
 */

#include <kunit/test.h>

#include <linux/btree.h>
#include <linux/errno.h>

#define BTREE_TEST_ENTRIES 768
#define BTREE_TEST_STRIDE 4

struct btree_test_entry {
	u32 key;
	u32 value;
	u32 replacement;
};

static void btree_traversal_destroy(void *data)
{
	struct btree_head *head = data;

	btree_destroy(head);
}

static struct btree_head32 *btree_test_init32(struct kunit *test)
{
	struct btree_head32 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init32(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_traversal_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head64 *btree_test_init64(struct kunit *test)
{
	struct btree_head64 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init64(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_traversal_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head128 *btree_test_init128(struct kunit *test)
{
	struct btree_head128 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init128(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_traversal_destroy,
						  &tree->h), 0);
	return tree;
}

static void btree_traversal_empty32(struct kunit *test)
{
	struct btree_head32 *tree = btree_test_init32(test);
	u32 key = 1234;
	u32 value;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_EXPECT_NULL(test, btree_lookup32(tree, key));
	KUNIT_EXPECT_NULL(test, btree_last32(tree, &key));
	KUNIT_EXPECT_EQ(test, key, 1234);

	key = 37;
	KUNIT_EXPECT_NULL(test, btree_get_prev32(tree, &key));
	KUNIT_EXPECT_EQ(test, key, 37);
	KUNIT_EXPECT_EQ(test, btree_update32(tree, 37, &value), -ENOENT);
}

static void btree_traversal_sparse32(struct kunit *test)
{
	struct btree_head32 *tree = btree_test_init32(test);
	struct btree_test_entry *entries;
	u32 previous;
	u32 key;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_TEST_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		entries[i].key = i * BTREE_TEST_STRIDE;
		entries[i].value = i + 1;
		entries[i].replacement = i + 0x10000;
	}

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		int index = (i * 37) % BTREE_TEST_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, entries[index].key,
					       &entries[index].value, GFP_KERNEL),
				0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		KUNIT_ASSERT_PTR_EQ(test,
				    btree_lookup32(tree, entries[i].key),
				    &entries[i].value);
		KUNIT_ASSERT_NULL(test,
				  btree_lookup32(tree, entries[i].key + 1));
		KUNIT_ASSERT_EQ(test,
				btree_update32(tree, entries[i].key,
					       &entries[i].replacement),
				0);
		KUNIT_ASSERT_PTR_EQ(test,
				    btree_lookup32(tree, entries[i].key),
				    &entries[i].replacement);
	}

	key = 10 * BTREE_TEST_STRIDE + 1;
	KUNIT_EXPECT_PTR_EQ(test, btree_get_prev32(tree, &key),
			    &entries[10].replacement);
	KUNIT_EXPECT_EQ(test, key, entries[10].key);

	previous = BTREE_TEST_ENTRIES * BTREE_TEST_STRIDE - 1;
	KUNIT_EXPECT_PTR_EQ(test, btree_last32(tree, &previous),
			    &entries[BTREE_TEST_ENTRIES - 1].replacement);
	KUNIT_EXPECT_EQ(test, previous,
			entries[BTREE_TEST_ENTRIES - 1].key);

	key = BTREE_TEST_ENTRIES * BTREE_TEST_STRIDE + 1;
	for (i = BTREE_TEST_ENTRIES - 1; i >= 0; i--) {
		void *value = btree_get_prev32(tree, &key);

		KUNIT_ASSERT_EQ_MSG(test, key, entries[i].key, "i=%d", i);
		KUNIT_ASSERT_PTR_EQ(test, value, &entries[i].replacement);
	}

	key = 0;
	KUNIT_EXPECT_NULL(test, btree_get_prev32(tree, &key));
	KUNIT_EXPECT_EQ(test, key, 0);
	KUNIT_EXPECT_EQ(test,
			btree_update32(tree, BTREE_TEST_STRIDE + 1, &previous),
			-ENOENT);
}

static void btree_traversal_sparse64(struct kunit *test)
{
	struct btree_head64 *tree = btree_test_init64(test);
	struct btree_test_entry *entries;
	u64 previous;
	u64 key;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_TEST_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		entries[i].key = i * 3;
		entries[i].value = i + 1;
		entries[i].replacement = i + 0x20000;
	}

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		int index = (i * 53) % BTREE_TEST_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert64(tree, entries[index].key,
					       &entries[index].value, GFP_KERNEL),
				0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++)
		KUNIT_ASSERT_PTR_EQ(test,
				    btree_lookup64(tree, entries[i].key),
				    &entries[i].value);

	key = 101 * 3 + 2;
	KUNIT_EXPECT_PTR_EQ(test, btree_get_prev64(tree, &key),
			    &entries[101].value);
	KUNIT_EXPECT_EQ(test, key, entries[101].key);

	key = BTREE_TEST_ENTRIES * 3 + 2;
	/* btree_last64() overwrites its key argument, so keep the cursor apart. */
	previous = key;
	KUNIT_EXPECT_PTR_EQ(test, btree_last64(tree, &previous),
			    &entries[BTREE_TEST_ENTRIES - 1].value);
	KUNIT_EXPECT_EQ(test, previous,
			entries[BTREE_TEST_ENTRIES - 1].key);

	for (i = BTREE_TEST_ENTRIES - 1; i >= 0; i--) {
		void *value = btree_get_prev64(tree, &key);

		KUNIT_ASSERT_EQ_MSG(test, key, entries[i].key, "i=%d", i);
		KUNIT_ASSERT_PTR_EQ(test, value, &entries[i].value);
	}
}

static void btree_traversal_lexicographic128(struct kunit *test)
{
	struct btree_head128 *tree = btree_test_init128(test);
	struct btree_test_entry *entries;
	u64 previous1;
	u64 previous2;
	u64 key1;
	u64 key2;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_TEST_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		entries[i].key = i;
		entries[i].value = i + 1;
		entries[i].replacement = i + 0x30000;
	}

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		int index = (i * 59) % BTREE_TEST_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert128(tree, entries[index].key * 3ULL,
						entries[index].key * 5ULL,
						&entries[index].value,
						GFP_KERNEL), 0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);

	for (i = 0; i < BTREE_TEST_ENTRIES; i++) {
		u64 first = entries[i].key * 3ULL;
		u64 second = entries[i].key * 5ULL;

		KUNIT_ASSERT_PTR_EQ(test,
				    btree_lookup128(tree, first, second),
				    &entries[i].value);
		KUNIT_ASSERT_NULL(test,
				  btree_lookup128(tree, first, second + 1));
		KUNIT_ASSERT_EQ(test,
				btree_update128(tree, first, second,
						&entries[i].replacement),
				0);
	}

	key1 = 201 * 3ULL;
	key2 = 201 * 5ULL + 1;
	KUNIT_EXPECT_PTR_EQ(test, btree_get_prev128(tree, &key1, &key2),
			    &entries[201].replacement);
	KUNIT_EXPECT_EQ(test, key1, entries[201].key * 3ULL);
	KUNIT_EXPECT_EQ(test, key2, entries[201].key * 5ULL);

	key1 = BTREE_TEST_ENTRIES * 3ULL;
	key2 = BTREE_TEST_ENTRIES * 5ULL;
	/* btree_last128() overwrites its key arguments, so keep the cursor apart. */
	previous1 = key1;
	previous2 = key2;
	KUNIT_EXPECT_PTR_EQ(test,
			    btree_last128(tree, &previous1, &previous2),
			    &entries[BTREE_TEST_ENTRIES - 1].replacement);
	KUNIT_EXPECT_EQ(test, previous1,
			(BTREE_TEST_ENTRIES - 1) * 3ULL);
	KUNIT_EXPECT_EQ(test, previous2,
			(BTREE_TEST_ENTRIES - 1) * 5ULL);

	for (i = BTREE_TEST_ENTRIES - 1; i >= 0; i--) {
		void *value = btree_get_prev128(tree, &key1, &key2);

		KUNIT_ASSERT_EQ_MSG(test, key1, entries[i].key * 3ULL,
				    "i=%d", i);
		KUNIT_ASSERT_EQ_MSG(test, key2, entries[i].key * 5ULL,
				    "i=%d", i);
		KUNIT_ASSERT_PTR_EQ(test, value, &entries[i].replacement);
	}

	key1 = 1;
	key2 = 0;
	KUNIT_EXPECT_PTR_EQ(test, btree_get_prev128(tree, &key1, &key2),
			    &entries[0].replacement);
	KUNIT_EXPECT_EQ(test, key1, 0);
	KUNIT_EXPECT_EQ(test, key2, 0);

	key1 = 0;
	key2 = 0;
	KUNIT_EXPECT_NULL(test, btree_get_prev128(tree, &key1, &key2));
	KUNIT_EXPECT_EQ(test, key1, 0);
	KUNIT_EXPECT_EQ(test, key2, 0);
}

static struct kunit_case btree_traversal_test_cases[] = {
	KUNIT_CASE(btree_traversal_empty32),
	KUNIT_CASE(btree_traversal_sparse32),
	KUNIT_CASE(btree_traversal_sparse64),
	KUNIT_CASE(btree_traversal_lexicographic128),
	{}
};

static struct kunit_suite btree_traversal_test_suite = {
	.name = "btree_traversal",
	.test_cases = btree_traversal_test_cases,
};

kunit_test_suite(btree_traversal_test_suite);
