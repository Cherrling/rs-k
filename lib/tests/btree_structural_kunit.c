// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for B+Tree structural operations.
 */

#include <kunit/test.h>

#include <linux/btree.h>
#include <linux/errno.h>

#define BTREE_STRUCTURAL_ENTRIES 768
#define BTREE_STRUCTURAL_MERGE_ENTRIES 384
#define BTREE_STRUCTURAL_NODE_BYTES 128

struct btree_structural_entry {
	u32 value;
};

struct btree_alloc_state {
	struct kunit *test;
	int remaining;
	int attempts;
};

static void btree_structural_destroy(void *data)
{
	struct btree_head *head = data;

	btree_destroy(head);
}

static void *btree_structural_alloc(gfp_t gfp, void *data)
{
	struct btree_alloc_state *state = data;

	state->attempts++;
	if (!state->remaining)
		return NULL;
	state->remaining--;
	return kunit_kzalloc(state->test, BTREE_STRUCTURAL_NODE_BYTES, gfp);
}

static void btree_structural_free(void *element, void *data)
{
}

static struct btree_head32 *btree_structural_init32(struct kunit *test)
{
	struct btree_head32 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init32(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_structural_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head64 *btree_structural_init64(struct kunit *test)
{
	struct btree_head64 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init64(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_structural_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head128 *btree_structural_init128(struct kunit *test)
{
	struct btree_head128 *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return NULL;
	KUNIT_ASSERT_EQ(test, btree_init128(tree), 0);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_structural_destroy,
						  &tree->h), 0);
	return tree;
}

static struct btree_head32 *
btree_structural_init_failing32(struct kunit *test,
				struct btree_alloc_state **state, int allocations)
{
	struct btree_head32 *tree;
	mempool_t *mempool;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	*state = kunit_kzalloc(test, sizeof(**state), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, *state);
	(*state)->test = test;
	(*state)->remaining = allocations;
	mempool = mempool_create(0, btree_structural_alloc, btree_structural_free,
				 *state);
	KUNIT_ASSERT_NOT_NULL(test, mempool);
	btree_init_mempool32(tree, mempool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_structural_destroy,
						  &tree->h), 0);
	return tree;
}

static void btree_structural_remove_sparse32(struct kunit *test)
{
	struct btree_head32 *tree = btree_structural_init32(test);
	struct btree_structural_entry *entries;
	u32 key;
	int initial_height;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_STRUCTURAL_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		entries[i].value = i + 1;
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 37) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, index * 4, &entries[index].value,
					       GFP_KERNEL), 0);
	}

	initial_height = tree->h.height;
	KUNIT_ASSERT_GT(test, initial_height, 2);

	key = 4 * 100 + 1;
	KUNIT_EXPECT_NULL(test, btree_remove32(tree, key));
	KUNIT_EXPECT_EQ(test, tree->h.height, initial_height);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 193) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_PTR_EQ(test, btree_remove32(tree, index * 4),
				    &entries[index].value);
		KUNIT_ASSERT_NULL(test, btree_lookup32(tree, index * 4));
	}

	KUNIT_EXPECT_NULL(test, btree_remove32(tree, 4));
	KUNIT_EXPECT_LE(test, tree->h.height, initial_height);
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		KUNIT_ASSERT_NULL(test, btree_lookup32(tree, i * 4));

	key = 0;
	KUNIT_EXPECT_NULL(test, btree_last32(tree, &key));

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 101) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, index * 4, &entries[index].value,
					       GFP_KERNEL), 0);
	}
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(tree, i * 4),
				    &entries[i].value);
}

static void btree_structural_partial_remove64(struct kunit *test)
{
	struct btree_head64 *tree = btree_structural_init64(test);
	struct btree_structural_entry *entries;
	u64 key;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_STRUCTURAL_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		entries[i].value = i + 1;
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 53) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert64(tree, index * 3ULL,
					       &entries[index].value, GFP_KERNEL), 0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i += 2) {
		KUNIT_ASSERT_PTR_EQ(test, btree_remove64(tree, i * 3ULL),
				    &entries[i].value);
		KUNIT_ASSERT_NULL(test, btree_lookup64(tree, i * 3ULL));
	}
	for (i = 1; i < BTREE_STRUCTURAL_ENTRIES; i += 2)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup64(tree, i * 3ULL),
				    &entries[i].value);

	key = 3ULL + 1;
	KUNIT_EXPECT_NULL(test, btree_remove64(tree, key));
	KUNIT_EXPECT_PTR_EQ(test, btree_lookup64(tree, 3ULL),
			    &entries[1].value);

	for (i = 1; i < BTREE_STRUCTURAL_ENTRIES; i += 2) {
		KUNIT_ASSERT_PTR_EQ(test, btree_remove64(tree, i * 3ULL),
				    &entries[i].value);
		KUNIT_ASSERT_NULL(test, btree_lookup64(tree, i * 3ULL));
	}
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		KUNIT_ASSERT_NULL(test, btree_lookup64(tree, i * 3ULL));
}

static void btree_structural_partial_remove128(struct kunit *test)
{
	struct btree_head128 *tree = btree_structural_init128(test);
	struct btree_structural_entry *entries;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	entries = kunit_kzalloc(test,
				sizeof(*entries) * BTREE_STRUCTURAL_ENTRIES,
				GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		entries[i].value = i + 1;
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 59) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert128(tree, index * 3ULL,
						index * 5ULL,
						&entries[index].value,
						GFP_KERNEL), 0);
	}
	KUNIT_ASSERT_GT(test, tree->h.height, 2);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i += 3) {
		KUNIT_ASSERT_PTR_EQ(test,
				    btree_remove128(tree, i * 3ULL, i * 5ULL),
				    &entries[i].value);
		KUNIT_ASSERT_NULL(test,
				  btree_lookup128(tree, i * 3ULL, i * 5ULL));
	}
	for (i = 1; i < BTREE_STRUCTURAL_ENTRIES; i++)
		if (i % 3)
			KUNIT_ASSERT_PTR_EQ(test,
					    btree_lookup128(tree, i * 3ULL,
							    i * 5ULL),
					    &entries[i].value);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i += 3) {
		KUNIT_ASSERT_EQ(test,
				btree_insert128(tree, i * 3ULL, i * 5ULL,
						&entries[i].value, GFP_KERNEL), 0);
	}
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		KUNIT_ASSERT_PTR_EQ(test,
				    btree_lookup128(tree, i * 3ULL, i * 5ULL),
				    &entries[i].value);

	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++) {
		int index = (i * 151) % BTREE_STRUCTURAL_ENTRIES;

		KUNIT_ASSERT_PTR_EQ(test,
				    btree_remove128(tree, index * 3ULL,
						    index * 5ULL),
				    &entries[index].value);
	}
	for (i = 0; i < BTREE_STRUCTURAL_ENTRIES; i++)
		KUNIT_ASSERT_NULL(test,
				  btree_lookup128(tree, i * 3ULL, i * 5ULL));
}

static void btree_structural_merge32(struct kunit *test)
{
	struct btree_head32 *target = btree_structural_init32(test);
	struct btree_head32 *victim = btree_structural_init32(test);
	struct btree_structural_entry *target_entries;
	struct btree_structural_entry *victim_entries;
	size_t entries_bytes;
	u32 key;
	int i;

	KUNIT_ASSERT_NOT_NULL(test, target);
	KUNIT_ASSERT_NOT_NULL(test, victim);
	entries_bytes = sizeof(*target_entries) *
			BTREE_STRUCTURAL_MERGE_ENTRIES;
	target_entries = kunit_kzalloc(test, entries_bytes, GFP_KERNEL);
	victim_entries = kunit_kzalloc(test, entries_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, target_entries);
	KUNIT_ASSERT_NOT_NULL(test, victim_entries);

	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++) {
		target_entries[i].value = i + 1;
		victim_entries[i].value = i + 0x10000;
	}
	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++) {
		int index = (i * 193) % BTREE_STRUCTURAL_MERGE_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert32(target, index * 2,
					       &target_entries[index].value,
					       GFP_KERNEL), 0);
		KUNIT_ASSERT_EQ(test,
				btree_insert32(victim, index * 2 + 1,
					       &victim_entries[index].value,
					       GFP_KERNEL), 0);
	}

	KUNIT_ASSERT_EQ(test, btree_merge32(target, victim, GFP_KERNEL), 0);
	KUNIT_ASSERT_LE(test, victim->h.height, 1);

	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++) {
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(target, i * 2),
				    &target_entries[i].value);
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(target, i * 2 + 1),
				    &victim_entries[i].value);
		KUNIT_EXPECT_NULL(test, btree_lookup32(victim, i * 2));
		KUNIT_EXPECT_NULL(test, btree_lookup32(victim, i * 2 + 1));
	}

	key = 0;
	KUNIT_EXPECT_PTR_EQ(test, btree_last32(target, &key),
			    &victim_entries[BTREE_STRUCTURAL_MERGE_ENTRIES - 1].value);
	KUNIT_EXPECT_EQ(test, key,
			(BTREE_STRUCTURAL_MERGE_ENTRIES - 1) * 2 + 1);
	KUNIT_EXPECT_NULL(test, btree_last32(victim, &key));
}

static void btree_structural_empty_merge32(struct kunit *test)
{
	struct btree_head32 *target;
	struct btree_head32 *victim;
	struct btree_structural_entry *entries;
	mempool_t *mempool;
	size_t entries_bytes;
	unsigned long *victim_node;
	int victim_height;
	int i;

	target = kunit_kzalloc(test, sizeof(*target), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, target);
	victim = kunit_kzalloc(test, sizeof(*victim), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, victim);
	mempool = mempool_create(0, btree_alloc, btree_free, NULL);
	KUNIT_ASSERT_NOT_NULL(test, mempool);
	btree_init_mempool32(target, mempool);
	btree_init_mempool32(victim, mempool);
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, btree_structural_destroy,
						  &target->h), 0);
	entries_bytes = sizeof(*entries) * BTREE_STRUCTURAL_MERGE_ENTRIES;
	entries = kunit_kzalloc(test, entries_bytes, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entries);

	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++)
		entries[i].value = i + 1;
	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++) {
		int index = (i * 193) % BTREE_STRUCTURAL_MERGE_ENTRIES;

		KUNIT_ASSERT_EQ(test,
				btree_insert32(victim, index * 2,
					       &entries[index].value, GFP_KERNEL), 0);
	}
	victim_node = victim->h.node;
	victim_height = victim->h.height;
	KUNIT_ASSERT_EQ(test, btree_merge32(target, victim, GFP_KERNEL), 0);
	KUNIT_ASSERT_PTR_EQ(test, target->h.node, victim_node);
	KUNIT_ASSERT_EQ(test, target->h.height, victim_height);
	KUNIT_ASSERT_NULL(test, victim->h.node);
	KUNIT_ASSERT_EQ(test, victim->h.height, 0);

	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(target, i * 2),
				    &entries[i].value);
	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++) {
		int index = (i * 193) % BTREE_STRUCTURAL_MERGE_ENTRIES;

		KUNIT_ASSERT_PTR_EQ(test, btree_remove32(target, index * 2),
				    &entries[index].value);
	}
	for (i = 0; i < BTREE_STRUCTURAL_MERGE_ENTRIES; i++)
		KUNIT_ASSERT_NULL(test, btree_lookup32(target, i * 2));
}

static void btree_structural_allocation_failure32(struct kunit *test)
{
	struct btree_alloc_state *state;
	struct btree_head32 *tree;
	struct btree_structural_entry entries[10];
	void *reserve;
	int i;

	tree = btree_structural_init_failing32(test, &state, 1);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	/* Occupy the reserve to force the initial root allocation to fail. */
	reserve = mempool_alloc(tree->h.mempool, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, reserve);
	entries[0].value = 1;
	KUNIT_EXPECT_EQ(test,
			btree_insert32(tree, 0, &entries[0].value, GFP_NOWAIT),
			-ENOMEM);
	KUNIT_EXPECT_EQ(test, tree->h.height, 0);
	KUNIT_EXPECT_NULL(test, tree->h.node);
	KUNIT_EXPECT_EQ(test, state->attempts, 3);
	mempool_free(reserve, tree->h.mempool);

	tree = btree_structural_init_failing32(test, &state, 1);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	for (i = 0; i < 8; i++) {
		entries[i].value = i + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, i, &entries[i].value,
					       GFP_KERNEL), 0);
	}

	entries[8].value = 9;
	KUNIT_EXPECT_EQ(test,
			btree_insert32(tree, 8, &entries[8].value, GFP_NOWAIT),
			-ENOMEM);
	KUNIT_EXPECT_EQ(test, tree->h.height, 1);
	KUNIT_EXPECT_NULL(test, btree_lookup32(tree, 8));
	KUNIT_EXPECT_EQ(test, state->attempts, 3);
	for (i = 0; i < 8; i++)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(tree, i),
				    &entries[i].value);

	tree = btree_structural_init_failing32(test, &state, 2);
	KUNIT_ASSERT_NOT_NULL(test, tree);
	for (i = 0; i < 8; i++) {
		entries[i].value = i + 1;
		KUNIT_ASSERT_EQ(test,
				btree_insert32(tree, i, &entries[i].value,
					       GFP_KERNEL), 0);
	}

	entries[8].value = 9;
	KUNIT_EXPECT_EQ(test,
			btree_insert32(tree, 8, &entries[8].value, GFP_NOWAIT),
			-ENOMEM);
	KUNIT_EXPECT_EQ(test, tree->h.height, 1);
	KUNIT_EXPECT_NULL(test, btree_lookup32(tree, 8));
	KUNIT_EXPECT_EQ(test, state->attempts, 4);
	for (i = 0; i < 8; i++)
		KUNIT_ASSERT_PTR_EQ(test, btree_lookup32(tree, i),
				    &entries[i].value);
}

static struct kunit_case btree_structural_test_cases[] = {
	KUNIT_CASE(btree_structural_remove_sparse32),
	KUNIT_CASE(btree_structural_partial_remove64),
	KUNIT_CASE(btree_structural_partial_remove128),
	KUNIT_CASE(btree_structural_merge32),
	KUNIT_CASE(btree_structural_empty_merge32),
	KUNIT_CASE(btree_structural_allocation_failure32),
	{}
};

static struct kunit_suite btree_structural_test_suite = {
	.name = "btree_structural",
	.test_cases = btree_structural_test_cases,
};

kunit_test_suite(btree_structural_test_suite);
