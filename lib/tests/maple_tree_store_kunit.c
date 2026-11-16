// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for the active Maple Tree write dispatcher.
 */

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/maple_tree.h>
#include <linux/sched.h>
#include <linux/xarray.h>

#define MAPLE_STORE_RANGES 101
#define MAPLE_STORE_SPACING 10
#define MAPLE_STORE_LENGTH 5
#define MAPLE_STORE_CONCURRENT_WORKERS 4
#define MAPLE_STORE_CONCURRENT_ITERATIONS 1000
#define MAPLE_STORE_TIMEOUT_MS 10000

static void maple_store_test_destroy(void *data)
{
	mtree_destroy(data);
}

static struct maple_tree *maple_store_test_init_flags(struct kunit *test,
						      unsigned int flags)
{
	struct maple_tree *tree;

	tree = kunit_kzalloc(test, sizeof(*tree), GFP_KERNEL);
	if (!tree) {
		KUNIT_FAIL(test, "failed to allocate Maple tree");
		return NULL;
	}

	mt_init_flags(tree, flags);
	if (kunit_add_action_or_reset(test, maple_store_test_destroy, tree)) {
		KUNIT_FAIL(test, "failed to register Maple tree cleanup");
		return NULL;
	}

	return tree;
}

static struct maple_tree *maple_store_test_init(struct kunit *test)
{
	return maple_store_test_init_flags(test, 0);
}

static int maple_store_test_store_n(struct maple_tree *tree, unsigned long count)
{
	unsigned long i;

	for (i = 0; i < count; i++) {
		int ret;

		ret = mtree_store_range(tree, i * MAPLE_STORE_SPACING,
					i * MAPLE_STORE_SPACING +
					MAPLE_STORE_LENGTH,
					xa_mk_value(i + 1), GFP_KERNEL);
		if (ret)
			return ret;
	}

	return 0;
}

static int maple_store_test_store_many(struct maple_tree *tree)
{
	return maple_store_test_store_n(tree, MAPLE_STORE_RANGES);
}

static int maple_store_test_prepare_locked(struct maple_tree *tree,
					   struct ma_state *mas,
					   void *entry)
{
	int ret;

	mtree_lock(tree);
	ret = mas_preallocate(mas, entry, GFP_KERNEL);
	if (ret)
		mtree_unlock(tree);

	return ret;
}

static void maple_store_test_exact_fit_null(struct kunit *test)
{
	void *value = xa_mk_value(0x1001);
	void *other = xa_mk_value(0x1002);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(to_null, tree, 10, 20);
	MA_STATE(to_value, tree, 10, 20);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 9,
					  xa_mk_value(0x1000), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 10, 20, value, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 21, 30,
					  xa_mk_value(0x1003), GFP_KERNEL), 0);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &to_null, NULL), 0);
	KUNIT_EXPECT_EQ(test, to_null.store_type, wr_exact_fit);
	KUNIT_EXPECT_EQ(test, to_null.node_request, 0);
	mas_store_prealloc(&to_null, NULL);
	mtree_unlock(tree);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 10));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 20));

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &to_value, other), 0);
	KUNIT_EXPECT_EQ(test, to_value.store_type, wr_exact_fit);
	KUNIT_EXPECT_EQ(test, to_value.node_request, 0);
	mas_store_prealloc(&to_value, other);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 10), other);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 20), other);
}

static void maple_store_test_append(struct kunit *test)
{
	unsigned long first_append = ULONG_MAX;
	struct maple_tree *tree = maple_store_test_init(test);
	unsigned long i;

	KUNIT_ASSERT_NOT_NULL(test, tree);

	for (i = 0; i < MAPLE_STORE_RANGES; i++) {
		void *entry = xa_mk_value(i + 1);

		MA_STATE(mas, tree, i * MAPLE_STORE_SPACING,
			 i * MAPLE_STORE_SPACING + MAPLE_STORE_LENGTH);
		int ret;

		ret = maple_store_test_prepare_locked(tree, &mas, entry);
		KUNIT_ASSERT_EQ(test, ret, 0);
		if (mas.store_type == wr_append && first_append == ULONG_MAX)
			first_append = i;
		mas_store_prealloc(&mas, entry);
		mtree_unlock(tree);
	}

	KUNIT_ASSERT_NE(test, first_append, ULONG_MAX);
	for (i = 0; i < MAPLE_STORE_RANGES; i++) {
		void *entry = xa_mk_value(i + 1);
		unsigned long first = i * MAPLE_STORE_SPACING;
		unsigned long last = first + MAPLE_STORE_LENGTH;

		KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, first), entry);
		KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, last), entry);
	}
}

static void maple_store_test_slot_boundary(struct kunit *test)
{
	void *value = xa_mk_value(0x3001);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(mas, tree, 6, 10);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(tree), 0);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, value), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_slot_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	mas_store_prealloc(&mas, value);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 6), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 10), value);
}

static void maple_store_test_slot_three_touches(struct kunit *test)
{
	void *value = xa_mk_value(0x3101);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(mas, tree, 11, 22);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(tree), 0);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, value), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_slot_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	mas_store_prealloc(&mas, value);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 11), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 13), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 18), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 22), value);
	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, 10)), 2);
	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, 23)), 3);
}

static void maple_store_test_gap_reuse(struct kunit *test)
{
	void *value = xa_mk_value(0x4001);
	unsigned long start = 0;
	struct maple_tree *tree =
		maple_store_test_init_flags(test, MT_FLAGS_ALLOC_RANGE);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 9,
					  xa_mk_value(0x4002), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 20, 29,
					  xa_mk_value(0x4003), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 40, 49,
					  xa_mk_value(0x4004), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 20, 29, NULL, GFP_KERNEL), 0);

	KUNIT_EXPECT_EQ(test,
			mtree_alloc_range(tree, &start, value, 20, 10, 39,
					  GFP_KERNEL), 0);
	KUNIT_EXPECT_EQ(test, start, 10);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 10), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 29), value);
}

static void maple_store_test_node_store(struct kunit *test)
{
	void *first = xa_mk_value(0x4101);
	void *middle = xa_mk_value(0x4102);
	void *last = xa_mk_value(0x4103);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(mas, tree, 5, 24);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 9, first, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 20, 29, last, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, middle), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_node_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	mas_store_prealloc(&mas, middle);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 4), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 5), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 24), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 25), last);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 29), last);
}

static void maple_store_test_node_store_rcu(struct kunit *test)
{
	void *first = xa_mk_value(0x4201);
	void *middle = xa_mk_value(0x4202);
	void *last = xa_mk_value(0x4203);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(mas, tree, 5, 24);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 9, first, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 20, 29, last, GFP_KERNEL), 0);
	mt_set_in_rcu(tree);
	KUNIT_ASSERT_TRUE(test, mt_in_rcu(tree));
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, middle), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_node_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	KUNIT_ASSERT_NOT_NULL(test, mas.alloc);
	mas_store_prealloc(&mas, middle);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 4), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 5), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 24), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 25), last);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 29), last);
}

static void maple_store_test_store_root_pointer(struct kunit *test)
{
	void *value = xa_mk_value(0x4251);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(store, tree, 0, 0);
	MA_STATE(clear, tree, 0, 0);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &store, value), 0);
	KUNIT_EXPECT_EQ(test, store.store_type, wr_store_root);
	KUNIT_EXPECT_EQ(test, store.node_request, 0);
	mas_store_prealloc(&store, value);
	mtree_unlock(tree);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), value);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &clear, NULL), 0);
	KUNIT_EXPECT_EQ(test, clear.store_type, wr_store_root);
	KUNIT_EXPECT_EQ(test, clear.node_request, 0);
	mas_store_prealloc(&clear, NULL);
	mtree_unlock(tree);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 0));
}

static void maple_store_test_store_root_expand(struct kunit *test)
{
	void *existing = xa_mk_value(0x4253);
	void *value = xa_mk_value(0x4252);
	struct maple_tree *tree = maple_store_test_init(test);
	struct maple_tree *direct_tree = maple_store_test_init(test);

	MA_STATE(expand, tree, 5, 10);
	MA_STATE(seed, direct_tree, 0, 0);
	MA_STATE(direct_expand, direct_tree, 5, 10);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_NOT_NULL(test, direct_tree);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &expand, value), 0);
	KUNIT_EXPECT_EQ(test, expand.store_type, wr_store_root);
	KUNIT_EXPECT_EQ(test, expand.node_request, 0);
	KUNIT_ASSERT_TRUE(test, expand.alloc || expand.sheaf);
	mas_store_prealloc(&expand, value);
	mtree_unlock(tree);

	KUNIT_EXPECT_EQ(test, expand.offset, 1);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 0));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 4));
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 5), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 10), value);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 11));

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(direct_tree, &seed,
							existing), 0);
	mas_store_prealloc(&seed, existing);
	mtree_unlock(direct_tree);
	KUNIT_ASSERT_PTR_EQ(test, mtree_load(direct_tree, 0), existing);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(direct_tree,
							&direct_expand, value), 0);
	KUNIT_EXPECT_EQ(test, direct_expand.store_type, wr_store_root);
	mas_store_prealloc(&direct_expand, value);
	mtree_unlock(direct_tree);

	KUNIT_EXPECT_EQ(test, direct_expand.offset, 2);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(direct_tree, 0), existing);
	KUNIT_EXPECT_NULL(test, mtree_load(direct_tree, 4));
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(direct_tree, 5), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(direct_tree, 10), value);
	KUNIT_EXPECT_NULL(test, mtree_load(direct_tree, 11));
}

static void maple_store_test_new_root_common(struct kunit *test, bool in_rcu)
{
	void *old = xa_mk_value(0x4261);
	void *value = xa_mk_value(0x4262);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(global, tree, 0, ULONG_MAX);
	MA_STATE(clear, tree, 0, ULONG_MAX);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 5, 10, old, GFP_KERNEL), 0);
	if (in_rcu)
		mt_set_in_rcu(tree);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &global, value), 0);
	KUNIT_EXPECT_EQ(test, global.store_type, wr_new_root);
	KUNIT_EXPECT_EQ(test, global.node_request, 0);
	KUNIT_ASSERT_TRUE(test, global.alloc || global.sheaf);
	mas_store_prealloc(&global, value);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 5), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, ULONG_MAX), value);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &clear, NULL), 0);
	KUNIT_EXPECT_EQ(test, clear.store_type, wr_exact_fit);
	KUNIT_EXPECT_EQ(test, clear.node_request, 0);
	mas_store_prealloc(&clear, NULL);
	mtree_unlock(tree);

	KUNIT_EXPECT_NULL(test, mtree_load(tree, 0));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 5));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, ULONG_MAX));
}

static void maple_store_test_new_root(struct kunit *test)
{
	maple_store_test_new_root_common(test, false);
}

static void maple_store_test_new_root_rcu(struct kunit *test)
{
	maple_store_test_new_root_common(test, true);
}

static void maple_store_test_spanning_store_common(struct kunit *test,
						   bool in_rcu)
{
	void *first = xa_mk_value(0x4301);
	void *middle = xa_mk_value(0x4302);
	void *last = xa_mk_value(0x4303);
	struct maple_tree *tree = maple_store_test_init(test);
	struct maple_tree *alloc_tree =
		maple_store_test_init_flags(test, MT_FLAGS_ALLOC_RANGE);

	MA_STATE(mas, tree, 15, 604);
	MA_STATE(alloc_mas, alloc_tree, 15, 604);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_NOT_NULL(test, alloc_tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(tree), 0);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(alloc_tree), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 14, first, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 605, 609, last, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(alloc_tree, 0, 14, first,
					  GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(alloc_tree, 605, 609, last,
					  GFP_KERNEL), 0);
	if (in_rcu) {
		mt_set_in_rcu(tree);
		mt_set_in_rcu(alloc_tree);
	}

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, middle), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_spanning_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	KUNIT_ASSERT_TRUE(test, mas.alloc || mas.sheaf);
	mas_store_prealloc(&mas, middle);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 0), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 14), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 15), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 604), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 605), last);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 616));

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(alloc_tree, &alloc_mas,
							middle), 0);
	KUNIT_EXPECT_EQ(test, alloc_mas.store_type, wr_spanning_store);
	KUNIT_EXPECT_EQ(test, alloc_mas.node_request, 0);
	KUNIT_ASSERT_TRUE(test, alloc_mas.alloc || alloc_mas.sheaf);
	mas_store_prealloc(&alloc_mas, middle);
	mtree_unlock(alloc_tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(alloc_tree, 0), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(alloc_tree, 14), first);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(alloc_tree, 15), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(alloc_tree, 604), middle);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(alloc_tree, 605), last);
	KUNIT_EXPECT_NULL(test, mtree_load(alloc_tree, 616));
}

static void maple_store_test_spanning_store(struct kunit *test)
{
	maple_store_test_spanning_store_common(test, false);
}

static void maple_store_test_spanning_store_rcu(struct kunit *test)
{
	maple_store_test_spanning_store_common(test, true);
}

static void maple_store_test_spanning_null(struct kunit *test)
{
	void *first = xa_mk_value(0x4401);
	void *last = xa_mk_value(0x4402);
	struct maple_tree *tree = maple_store_test_init(test);

	MA_STATE(mas, tree, 5, 604);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(tree), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 0, 4, first, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 605, 609, last, GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, NULL), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_spanning_store);
	KUNIT_EXPECT_EQ(test, mas.node_request, 0);
	KUNIT_ASSERT_TRUE(test, mas.alloc || mas.sheaf);
	mas_store_prealloc(&mas, NULL);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 4), first);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 5));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, 604));
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 605), last);
}

static void maple_store_test_split_tree(struct kunit *test, unsigned int flags,
					bool in_rcu)
{
	void *before = xa_mk_value(6);
	void *after = xa_mk_value(7);
	void *value = xa_mk_value(0x4501);
	struct maple_tree *tree = maple_store_test_init_flags(test, flags);

	MA_STATE(mas, tree, 54, 54);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_many(tree), 0);
	if (in_rcu)
		mt_set_in_rcu(tree);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, value), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_split_store);
	KUNIT_ASSERT_TRUE(test, mas.alloc || mas.sheaf);
	mas_store_prealloc(&mas, value);
	mtree_unlock(tree);

	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 50), before);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 53), before);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 54), value);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 55), before);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 60), after);
	KUNIT_EXPECT_PTR_EQ(test, mtree_load(tree, 65), after);
}

static void maple_store_test_split_store_common(struct kunit *test, bool in_rcu)
{
	maple_store_test_split_tree(test, 0, in_rcu);
	maple_store_test_split_tree(test, MT_FLAGS_ALLOC_RANGE, in_rcu);
}

static void maple_store_test_split_store(struct kunit *test)
{
	maple_store_test_split_store_common(test, false);
}

static void maple_store_test_split_store_rcu(struct kunit *test)
{
	maple_store_test_split_store_common(test, true);
}

static void maple_store_test_rebalance_tree(struct kunit *test,
					    unsigned int flags, bool in_rcu,
					    unsigned long first,
					    unsigned long last,
					    unsigned long left_value,
					    unsigned long right_value)
{
	const unsigned long ranges = 20;
	struct maple_tree *tree = maple_store_test_init_flags(test, flags);

	MA_STATE(mas, tree, first, last);

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test, maple_store_test_store_n(tree, ranges), 0);
	if (in_rcu)
		mt_set_in_rcu(tree);

	KUNIT_ASSERT_EQ(test,
			maple_store_test_prepare_locked(tree, &mas, NULL), 0);
	KUNIT_EXPECT_EQ(test, mas.store_type, wr_rebalance);
	KUNIT_ASSERT_TRUE(test, mas.alloc || mas.sheaf);
	mas_store_prealloc(&mas, NULL);
	mtree_unlock(tree);

	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, 0)), 1);
	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, first - 1)),
			left_value);
	KUNIT_EXPECT_NULL(test, mtree_load(tree, first));
	KUNIT_EXPECT_NULL(test, mtree_load(tree, last));
	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, last + 1)),
			right_value);
	KUNIT_EXPECT_EQ(test, xa_to_value(mtree_load(tree, 190)), ranges);
}

static void maple_store_test_rebalance_common(struct kunit *test, bool in_rcu)
{
	maple_store_test_rebalance_tree(test, 0, in_rcu, 11, 69, 2, 8);
	maple_store_test_rebalance_tree(test, MT_FLAGS_ALLOC_RANGE, in_rcu,
					11, 69, 2, 8);
	maple_store_test_rebalance_tree(test, 0, in_rcu, 141, 189, 15, 20);
	maple_store_test_rebalance_tree(test, MT_FLAGS_ALLOC_RANGE, in_rcu,
					141, 189, 15, 20);
}

static void maple_store_test_rebalance_store(struct kunit *test)
{
	maple_store_test_rebalance_common(test, false);
}

static void maple_store_test_rebalance_store_rcu(struct kunit *test)
{
	maple_store_test_rebalance_common(test, true);
}

struct maple_store_worker {
	struct maple_tree *tree;
	atomic_t *done;
	atomic_t *failed;
	atomic_t *ready;
	atomic_t *start;
	struct completion *complete;
	bool writer;
};

static int maple_store_worker_fn(void *data)
{
	const unsigned long stable = 4096;
	const unsigned long moving_first = 8192;
	const unsigned long moving_last = 8447;
	struct maple_store_worker *worker = data;
	void *stable_value = xa_mk_value(0x5001);
	void *moving_value = xa_mk_value(0x5002);
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start))
		cond_resched();

	for (i = 0; i < MAPLE_STORE_CONCURRENT_ITERATIONS; i++) {
		if (worker->writer) {
			MA_STATE(mas, worker->tree, moving_first, moving_last);
			void *entry = (i & 1) ? NULL : moving_value;
			int ret;
			bool exact;

			mtree_lock(worker->tree);
			ret = mas_preallocate(&mas, entry, GFP_KERNEL);
			exact = !ret && mas.store_type == wr_exact_fit;
			if (exact)
				mas_store_prealloc(&mas, entry);
			else
				mas_destroy(&mas);
			mtree_unlock(worker->tree);
			if (!exact)
				atomic_inc(worker->failed);
		} else {
			void *entry;

			rcu_read_lock();
			entry = mtree_load(worker->tree, stable);
			if (entry != stable_value)
				atomic_inc(worker->failed);
			entry = mtree_load(worker->tree, moving_first);
			if (entry && entry != moving_value)
				atomic_inc(worker->failed);
			rcu_read_unlock();
		}
		if (!(i & 31))
			cond_resched();
	}

	atomic_inc(worker->done);
	complete(worker->complete);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void maple_store_test_concurrent_rcu(struct kunit *test)
{
	struct maple_store_worker workers[MAPLE_STORE_CONCURRENT_WORKERS];
	struct task_struct *tasks[MAPLE_STORE_CONCURRENT_WORKERS] = {};
	struct completion complete;
	atomic_t done = ATOMIC_INIT(0);
	atomic_t failed = ATOMIC_INIT(0);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	struct maple_tree *tree = maple_store_test_init(test);
	unsigned long deadline;
	unsigned int started = 0;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, tree);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 4096, 4351,
					  xa_mk_value(0x5001), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 8000, 8191,
					  xa_mk_value(0x5003), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 8192, 8447,
					  xa_mk_value(0x5002), GFP_KERNEL), 0);
	KUNIT_ASSERT_EQ(test,
			mtree_store_range(tree, 8448, 8600,
					  xa_mk_value(0x5004), GFP_KERNEL), 0);
	mt_set_in_rcu(tree);
	init_completion(&complete);

	for (i = 0; i < MAPLE_STORE_CONCURRENT_WORKERS; i++) {
		workers[i].tree = tree;
		workers[i].done = &done;
		workers[i].failed = &failed;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].complete = &complete;
		workers[i].writer = i == 0;
		tasks[i] = kthread_run(maple_store_worker_fn, &workers[i],
				       "maple-store-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(MAPLE_STORE_TIMEOUT_MS);
	while (atomic_read(&ready) < started && time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) == started)
		atomic_set(&start, 1);

	for (i = 0; i < started; i++) {
		unsigned long timeout =
			msecs_to_jiffies(MAPLE_STORE_TIMEOUT_MS);

		if (!wait_for_completion_timeout(&complete, timeout)) {
			KUNIT_FAIL(test, "Maple store worker timed out");
			break;
		}
	}
	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, MAPLE_STORE_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&done),
			MAPLE_STORE_CONCURRENT_WORKERS);
	KUNIT_EXPECT_EQ(test, atomic_read(&failed), 0);
}

static struct kunit_case maple_store_test_cases[] = {
	KUNIT_CASE(maple_store_test_exact_fit_null),
	KUNIT_CASE(maple_store_test_append),
	KUNIT_CASE(maple_store_test_slot_boundary),
	KUNIT_CASE(maple_store_test_slot_three_touches),
	KUNIT_CASE(maple_store_test_gap_reuse),
	KUNIT_CASE(maple_store_test_node_store),
	KUNIT_CASE(maple_store_test_node_store_rcu),
	KUNIT_CASE(maple_store_test_store_root_pointer),
	KUNIT_CASE(maple_store_test_store_root_expand),
	KUNIT_CASE(maple_store_test_new_root),
	KUNIT_CASE(maple_store_test_new_root_rcu),
	KUNIT_CASE(maple_store_test_spanning_store),
	KUNIT_CASE(maple_store_test_spanning_store_rcu),
	KUNIT_CASE(maple_store_test_spanning_null),
	KUNIT_CASE(maple_store_test_split_store),
	KUNIT_CASE(maple_store_test_split_store_rcu),
	KUNIT_CASE(maple_store_test_rebalance_store),
	KUNIT_CASE(maple_store_test_rebalance_store_rcu),
	KUNIT_CASE(maple_store_test_concurrent_rcu),
	{}
};

static struct kunit_suite maple_store_test_suite = {
	.name = "maple_tree_store",
	.test_cases = maple_store_test_cases,
};

kunit_test_suite(maple_store_test_suite);
