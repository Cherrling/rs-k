// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for generic radix trees.
 */

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/generic-radix-tree.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#define OBJS_PER_PAGE (GENRADIX_NODE_SIZE / sizeof(u32))
#define NR_WORKERS 4
#define PAGES_PER_WORKER 16
#define WORKER_TIMEOUT_MS 10000

struct test_entry {
	u8 data[3];
};

#define genradix_u32 typeof(GENRADIX(u32))
#define test_entry_radix typeof(GENRADIX(struct test_entry))

static void genradix_test_free(void *data)
{
	__genradix_free(data);
}

static void genradix_test_init(struct kunit *test, struct __genradix *tree)
{
	tree->root = NULL;
	KUNIT_ASSERT_EQ(test,
			kunit_add_action_or_reset(test, genradix_test_free, tree),
			0);
}

static u32 *genradix_test_set(struct kunit *test, struct __genradix *tree,
			      size_t index, u32 value)
{
	u32 *entry;

	entry = __genradix_ptr_alloc(tree, index * sizeof(*entry), NULL,
				     GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	*entry = value;

	return entry;
}

static void genradix_test_empty(struct kunit *test)
{
	genradix_u32 *radix;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	KUNIT_EXPECT_NULL(test, genradix_ptr(radix, 0));
	KUNIT_EXPECT_NULL(test, genradix_ptr(radix, 1UL << 20));
	KUNIT_EXPECT_NULL(test,
			  genradix_iter_peek(&(struct genradix_iter){}, radix));
}

static void genradix_test_alloc_should_zero_entry(struct kunit *test)
{
	genradix_u32 *radix;
	u32 *entry;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	entry = genradix_ptr_alloc(radix, 0, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, *entry, 0);

	*entry = 0x12345678;
	KUNIT_EXPECT_PTR_EQ(test, genradix_ptr(radix, 0), entry);
	KUNIT_EXPECT_EQ(test, *entry, 0x12345678);
}

static void genradix_test_sparse_and_deep_indices(struct kunit *test)
{
	genradix_u32 *radix;
	const size_t indexes[] = {
		0, 1, OBJS_PER_PAGE - 1, OBJS_PER_PAGE,
		OBJS_PER_PAGE * 64, 1UL << 20,
	};
	size_t i;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);

	for (i = 0; i < ARRAY_SIZE(indexes); i++) {
		u32 *entry = genradix_ptr_alloc(radix, indexes[i], GFP_KERNEL);

		KUNIT_ASSERT_NOT_NULL(test, entry);
		*entry = 0x1000 + i;
	}

	for (i = 0; i < ARRAY_SIZE(indexes); i++)
		KUNIT_EXPECT_EQ(test, *genradix_ptr(radix, indexes[i]),
				0x1000 + i);

	KUNIT_EXPECT_NULL(test,
			  genradix_ptr(radix, (1UL << 20) + OBJS_PER_PAGE));
}

static void genradix_test_prealloc_should_create_zeroed_pages(struct kunit *test)
{
	genradix_u32 *radix;
	unsigned int pages = 4;
	size_t i;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	KUNIT_EXPECT_EQ(test,
			genradix_prealloc(radix, pages * OBJS_PER_PAGE,
					  GFP_KERNEL),
			0);

	for (i = 0; i < pages; i++) {
		u32 *entry = genradix_ptr(radix, i * OBJS_PER_PAGE);

		KUNIT_ASSERT_NOT_NULL(test, entry);
		KUNIT_EXPECT_EQ(test, *entry, 0);
	}

	KUNIT_EXPECT_NULL(test,
			  genradix_ptr(radix, (pages + 1) * OBJS_PER_PAGE));
}

static void genradix_test_preallocated_node_should_be_used(struct kunit *test)
{
	genradix_u32 *radix;
	struct genradix_node *node;
	u32 *entry;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	genradix_test_set(test, &radix->tree, 0, 1);

	node = kzalloc(GENRADIX_NODE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, node);
	entry = genradix_ptr_alloc_preallocated(radix, OBJS_PER_PAGE, &node, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);

	kfree(node);
	KUNIT_EXPECT_NULL(test, node);
	KUNIT_EXPECT_EQ(test, *entry, 0);
	*entry = 2;
	KUNIT_EXPECT_EQ(test, *genradix_ptr(radix, OBJS_PER_PAGE), 2);
}

static void genradix_test_forward_iter_should_skip_missing_pages(struct kunit *test)
{
	genradix_u32 *radix;
	struct genradix_iter iter;
	u32 *entry;
	unsigned int i;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	genradix_test_set(test, &radix->tree, 0, 11);
	genradix_test_set(test, &radix->tree, OBJS_PER_PAGE * 2, 22);

	iter = genradix_iter_init(radix, 0);
	entry = genradix_iter_peek(&iter, radix);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, iter.pos, 0);
	KUNIT_EXPECT_EQ(test, *entry, 11);

	for (i = 0; i < OBJS_PER_PAGE; i++)
		genradix_iter_advance(&iter, radix);

	entry = genradix_iter_peek(&iter, radix);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, iter.pos, OBJS_PER_PAGE * 2);
	KUNIT_EXPECT_EQ(test, *entry, 22);

	for (i = 0; i < OBJS_PER_PAGE; i++)
		genradix_iter_advance(&iter, radix);
	KUNIT_EXPECT_NULL(test, genradix_iter_peek(&iter, radix));
}

static void genradix_test_reverse_iter_should_skip_missing_pages(struct kunit *test)
{
	genradix_u32 *radix;
	struct genradix_iter iter;
	u32 *entry;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	genradix_test_set(test, &radix->tree, OBJS_PER_PAGE - 1, 33);
	genradix_test_set(test, &radix->tree, OBJS_PER_PAGE * 3 - 1, 44);

	iter = genradix_iter_init(radix, OBJS_PER_PAGE * 2 - 1);
	entry = genradix_iter_peek_prev(&iter, radix);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, iter.pos, OBJS_PER_PAGE - 1);
	KUNIT_EXPECT_EQ(test, *entry, 33);

	iter = genradix_iter_init(radix, OBJS_PER_PAGE * 3);
	entry = genradix_iter_peek_prev(&iter, radix);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	KUNIT_EXPECT_EQ(test, iter.pos, OBJS_PER_PAGE * 3 - 1);
	KUNIT_EXPECT_EQ(test, *entry, 44);
}

static void genradix_test_free_should_reset_tree(struct kunit *test)
{
	genradix_u32 *radix;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	genradix_test_set(test, &radix->tree, 0, 1);
	genradix_test_set(test, &radix->tree, 1UL << 20, 2);

	genradix_free(radix);
	KUNIT_EXPECT_NULL(test, radix->tree.root);
	KUNIT_EXPECT_NULL(test, genradix_ptr(radix, 0));
	KUNIT_EXPECT_NULL(test, genradix_ptr(radix, 1UL << 20));
}

static void genradix_test_non_power_of_two_entries(struct kunit *test)
{
	test_entry_radix *radix;
	struct test_entry *entry;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	entry = genradix_ptr_alloc(radix, 169, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	memcpy(entry->data, "abc", sizeof(entry->data));

	entry = genradix_ptr_alloc(radix, 170, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, entry);
	memcpy(entry->data, "def", sizeof(entry->data));

	KUNIT_EXPECT_MEMEQ(test, genradix_ptr(radix, 169)->data, "abc", 3);
	KUNIT_EXPECT_MEMEQ(test, genradix_ptr(radix, 170)->data, "def", 3);
}

struct genradix_test_worker {
	struct __genradix *tree;
	struct completion *done;
	unsigned int tag;
	int result;
};

static int genradix_test_worker_fn(void *data)
{
	struct genradix_test_worker *worker = data;
	unsigned int page;

	for (page = 0; page < PAGES_PER_WORKER; page++) {
		size_t page_index = worker->tag * PAGES_PER_WORKER + page;
		size_t offset = page_index * GENRADIX_NODE_SIZE;
		u32 value = (worker->tag << 16) | page;
		u32 *entry;

		entry = __genradix_ptr_alloc(worker->tree, offset, NULL,
					     GFP_KERNEL);
		if (!entry) {
			worker->result = -ENOMEM;
			break;
		}

		*entry = value;
		if (__genradix_ptr(worker->tree, offset) != entry) {
			worker->result = -ENOENT;
			break;
		}
		if (*entry != value) {
			worker->result = -EIO;
			break;
		}
	}

	complete(worker->done);
	while (!kthread_should_stop())
		cond_resched();

	return 0;
}

static void genradix_test_concurrent_allocations(struct kunit *test)
{
	genradix_u32 *radix;
	struct genradix_test_worker workers[NR_WORKERS];
	struct task_struct *tasks[NR_WORKERS] = {};
	struct completion done;
	unsigned int started = 0;
	unsigned int i;
	unsigned long timeout;

	radix = kunit_kzalloc(test, sizeof(*radix), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, radix);
	genradix_test_init(test, &radix->tree);
	init_completion(&done);
	timeout = msecs_to_jiffies(WORKER_TIMEOUT_MS);

	for (i = 0; i < NR_WORKERS; i++) {
		workers[i].tree = &radix->tree;
		workers[i].done = &done;
		workers[i].tag = i + 1;
		workers[i].result = 0;
		tasks[i] = kthread_run(genradix_test_worker_fn, &workers[i],
				       "genradix-test-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	for (i = 0; i < started; i++) {
		if (!wait_for_completion_timeout(&done, timeout)) {
			KUNIT_FAIL(test, "generic radix tree worker timed out");
			break;
		}
	}

	for (i = 0; i < started; i++)
		KUNIT_EXPECT_EQ(test, kthread_stop(tasks[i]), 0);

	KUNIT_ASSERT_EQ(test, started, NR_WORKERS);
	for (i = 0; i < NR_WORKERS; i++)
		KUNIT_ASSERT_EQ(test, workers[i].result, 0);

	for (i = 0; i < NR_WORKERS; i++) {
		unsigned int page;

		for (page = 0; page < PAGES_PER_WORKER; page++) {
			size_t page_index = workers[i].tag * PAGES_PER_WORKER + page;
			u32 *entry = __genradix_ptr(&radix->tree,
						    page_index * GENRADIX_NODE_SIZE);

			KUNIT_ASSERT_NOT_NULL(test, entry);
			KUNIT_EXPECT_EQ(test, *entry, (workers[i].tag << 16) | page);
		}
	}
}

static struct kunit_case genradix_test_cases[] = {
	KUNIT_CASE(genradix_test_empty),
	KUNIT_CASE(genradix_test_alloc_should_zero_entry),
	KUNIT_CASE(genradix_test_sparse_and_deep_indices),
	KUNIT_CASE(genradix_test_prealloc_should_create_zeroed_pages),
	KUNIT_CASE(genradix_test_preallocated_node_should_be_used),
	KUNIT_CASE(genradix_test_forward_iter_should_skip_missing_pages),
	KUNIT_CASE(genradix_test_reverse_iter_should_skip_missing_pages),
	KUNIT_CASE(genradix_test_free_should_reset_tree),
	KUNIT_CASE(genradix_test_non_power_of_two_entries),
	KUNIT_CASE(genradix_test_concurrent_allocations),
	{}
};

static struct kunit_suite genradix_test_suite = {
	.name = "genradix",
	.test_cases = genradix_test_cases,
};

kunit_test_suite(genradix_test_suite);
