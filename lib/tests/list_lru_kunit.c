// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/list_lru.h>
#include <linux/lockdep.h>
#include <linux/spinlock.h>

#define LIST_LRU_TEST_NID 0
#define LIST_LRU_TEST_WORKERS 4
#define LIST_LRU_TEST_ITEMS_PER_WORKER 16
#define LIST_LRU_TEST_ITERATIONS 10000
#define LIST_LRU_TEST_TIMEOUT_MS 30000

struct list_lru_test_item {
	struct list_head node;
};

struct list_lru_test_walk_ctx {
	struct kunit *test;
	enum lru_status status;
	unsigned int calls;
};

static void list_lru_test_init_items(struct list_lru_test_item *items,
				     unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		INIT_LIST_HEAD(&items[i].node);
}

static enum lru_status
list_lru_test_walk_cb(struct list_head *item, struct list_lru_one *l,
		      void *arg)
{
	struct list_lru_test_walk_ctx *ctx = arg;

	ctx->calls++;
	switch (ctx->status) {
	case LRU_REMOVED:
		list_lru_isolate(l, item);
		break;
	case LRU_REMOVED_RETRY:
		list_lru_isolate(l, item);
		spin_unlock(&l->lock);
		break;
	case LRU_ROTATE:
	case LRU_SKIP:
		break;
	case LRU_RETRY:
	case LRU_STOP:
		spin_unlock(&l->lock);
		break;
	default:
		KUNIT_FAIL(ctx->test, "test callback received an unknown status");
		spin_unlock(&l->lock);
		return LRU_STOP;
	}

	return ctx->status;
}

static void list_lru_test_init_ctx(struct kunit *test,
				   struct list_lru_test_walk_ctx *ctx,
				   enum lru_status status)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->test = test;
	ctx->status = status;
}

static void list_lru_test_expect_unlocked(struct kunit *test,
					  struct list_lru *lru)
{
#ifdef CONFIG_LOCKDEP
	struct spinlock *lock = &lru->node[LIST_LRU_TEST_NID].lru.lock;

	KUNIT_EXPECT_NE(test, lockdep_is_held(lock), LOCK_STATE_HELD);
#endif
}

static void list_lru_test_add_delete(struct kunit *test)
{
	struct list_lru_test_item items[2];
	struct list_lru lru;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);

	KUNIT_EXPECT_TRUE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_FALSE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			1UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 1L);

	KUNIT_EXPECT_FALSE(test, list_lru_add(&lru, &items[0].node,
					      LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			1UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 1L);

	KUNIT_EXPECT_TRUE(test, list_lru_add_irq(&lru, &items[1].node,
						 LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			2UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);

	KUNIT_EXPECT_TRUE(test, list_lru_del(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			1UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 1L);

	KUNIT_EXPECT_FALSE(test, list_lru_del(&lru, &items[0].node,
					      LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_del(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			0UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 0L);

	list_lru_destroy(&lru);
}

static void list_lru_test_isolate(struct kunit *test)
{
	struct list_lru_test_item items[2];
	struct list_lru_one *l;
	struct mem_cgroup *memcg = NULL;
	struct list_lru lru;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	l = list_lru_lock(&lru, LIST_LRU_TEST_NID, &memcg);
	list_lru_isolate(l, &items[0].node);
	list_lru_unlock(l);

	KUNIT_EXPECT_TRUE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_FALSE(test, list_empty(&items[1].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			1UL);
	/*
	 * An isolate callback does not own the node counter. The walker
	 * decrements it only when the callback returns LRU_REMOVED.
	 */
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);

	list_lru_destroy(&lru);
}

static void list_lru_test_isolate_move(struct kunit *test)
{
	struct list_lru_test_item items[2];
	struct list_head moved;
	struct list_lru_one *l;
	struct mem_cgroup *memcg = NULL;
	struct list_lru lru;

	INIT_LIST_HEAD(&moved);
	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	l = list_lru_lock(&lru, LIST_LRU_TEST_NID, &memcg);
	list_lru_isolate_move(l, &items[0].node, &moved);
	list_lru_unlock(l);

	KUNIT_EXPECT_PTR_EQ(test, moved.next, &items[0].node);
	KUNIT_EXPECT_PTR_EQ(test, moved.prev, &items[0].node);
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			1UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);

	list_lru_destroy(&lru);
}

static void list_lru_test_count_clamp(struct kunit *test)
{
	struct list_lru_test_item item;
	struct list_lru_one *l;
	struct mem_cgroup *memcg = NULL;
	struct list_lru lru;

	list_lru_test_init_items(&item, 1);
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &item.node,
					     LIST_LRU_TEST_NID, NULL));

	l = list_lru_lock(&lru, LIST_LRU_TEST_NID, &memcg);
	WRITE_ONCE(l->nr_items, -1);
	list_lru_unlock(l);
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			0UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 1L);

	l = list_lru_lock(&lru, LIST_LRU_TEST_NID, &memcg);
	WRITE_ONCE(l->nr_items, 1);
	list_lru_unlock(l);
	KUNIT_EXPECT_TRUE(test, list_lru_del(&lru, &item.node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			0UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 0L);

	list_lru_destroy(&lru);
}

static void list_lru_test_walk_rotate_skip(struct kunit *test)
{
	struct list_lru_test_walk_ctx ctx;
	struct list_lru_test_item items[2];
	struct list_lru lru;
	unsigned long nr_to_walk;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	list_lru_test_init_ctx(test, &ctx, LRU_ROTATE);
	nr_to_walk = 2;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 0UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 2U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 0UL);
	KUNIT_EXPECT_PTR_EQ(test, lru.node[LIST_LRU_TEST_NID].lru.list.next,
			    &items[0].node);
	KUNIT_EXPECT_PTR_EQ(test, lru.node[LIST_LRU_TEST_NID].lru.list.prev,
			    &items[1].node);
	list_lru_test_expect_unlocked(test, &lru);

	list_lru_test_init_ctx(test, &ctx, LRU_SKIP);
	nr_to_walk = 2;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 0UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 2U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 0UL);
	KUNIT_EXPECT_PTR_EQ(test, lru.node[LIST_LRU_TEST_NID].lru.list.next,
			    &items[0].node);
	KUNIT_EXPECT_PTR_EQ(test, lru.node[LIST_LRU_TEST_NID].lru.list.prev,
			    &items[1].node);
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			2UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);

	list_lru_destroy(&lru);
}

static void list_lru_test_walk_removed(struct kunit *test)
{
	struct list_lru_test_walk_ctx ctx;
	struct list_lru_test_item items[2];
	struct list_lru lru;
	unsigned long nr_to_walk;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	list_lru_test_init_ctx(test, &ctx, LRU_REMOVED);
	nr_to_walk = 2;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 2UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 2U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 0UL);
	KUNIT_EXPECT_TRUE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_TRUE(test, list_empty(&items[1].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			0UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 0L);
	list_lru_test_expect_unlocked(test, &lru);

	list_lru_destroy(&lru);
}

static void list_lru_test_walk_retry(struct kunit *test)
{
	struct list_lru_test_walk_ctx ctx;
	struct list_lru_test_item items[2];
	struct list_lru lru;
	unsigned long nr_to_walk;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	list_lru_test_init_ctx(test, &ctx, LRU_RETRY);
	nr_to_walk = 1;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 0UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 1U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 0UL);
	KUNIT_EXPECT_FALSE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_FALSE(test, list_empty(&items[1].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			2UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);
	list_lru_test_expect_unlocked(test, &lru);

	list_lru_destroy(&lru);
}

static void list_lru_test_walk_removed_retry(struct kunit *test)
{
	struct list_lru_test_walk_ctx ctx;
	struct list_lru_test_item items[2];
	struct list_lru lru;
	unsigned long nr_to_walk;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	list_lru_test_init_ctx(test, &ctx, LRU_REMOVED_RETRY);
	nr_to_walk = 2;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 2UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 2U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 0UL);
	KUNIT_EXPECT_TRUE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_TRUE(test, list_empty(&items[1].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			0UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 0L);
	list_lru_test_expect_unlocked(test, &lru);

	list_lru_destroy(&lru);
}

static void list_lru_test_walk_stop(struct kunit *test)
{
	struct list_lru_test_walk_ctx ctx;
	struct list_lru_test_item items[2];
	struct list_lru lru;
	unsigned long nr_to_walk;

	list_lru_test_init_items(items, ARRAY_SIZE(items));
	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[0].node,
					     LIST_LRU_TEST_NID, NULL));
	KUNIT_EXPECT_TRUE(test, list_lru_add(&lru, &items[1].node,
					     LIST_LRU_TEST_NID, NULL));

	list_lru_test_init_ctx(test, &ctx, LRU_STOP);
	nr_to_walk = 2;
	KUNIT_EXPECT_EQ(test, list_lru_walk_one(&lru, LIST_LRU_TEST_NID, NULL,
						list_lru_test_walk_cb, &ctx,
						&nr_to_walk), 0UL);
	KUNIT_EXPECT_EQ(test, ctx.calls, 1U);
	KUNIT_EXPECT_EQ(test, nr_to_walk, 1UL);
	KUNIT_EXPECT_FALSE(test, list_empty(&items[0].node));
	KUNIT_EXPECT_FALSE(test, list_empty(&items[1].node));
	KUNIT_EXPECT_EQ(test, list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
			2UL);
	KUNIT_EXPECT_EQ(test, list_lru_count_node(&lru, LIST_LRU_TEST_NID), 2L);
	list_lru_test_expect_unlocked(test, &lru);

	list_lru_destroy(&lru);
}

struct list_lru_test_worker {
	struct list_lru *lru;
	struct list_lru_test_item *items;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static int list_lru_test_worker_fn(void *data)
{
	struct list_lru_test_worker *worker = data;
	int i;

	list_lru_test_init_items(worker->items,
				 LIST_LRU_TEST_ITEMS_PER_WORKER);
	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < LIST_LRU_TEST_ITERATIONS; i++) {
		struct list_head *item = &worker->items[i %
			LIST_LRU_TEST_ITEMS_PER_WORKER].node;

		if (!list_lru_add(worker->lru, item, LIST_LRU_TEST_NID, NULL)) {
			atomic_inc(worker->errors);
			continue;
		}
		if (list_lru_add(worker->lru, item, LIST_LRU_TEST_NID, NULL))
			atomic_inc(worker->errors);
		if (!list_lru_del(worker->lru, item, LIST_LRU_TEST_NID, NULL))
			atomic_inc(worker->errors);
		if (list_lru_del(worker->lru, item, LIST_LRU_TEST_NID, NULL))
			atomic_inc(worker->errors);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void list_lru_test_concurrent(struct kunit *test)
{
	struct list_lru_test_item items[LIST_LRU_TEST_WORKERS]
				      [LIST_LRU_TEST_ITEMS_PER_WORKER];
	struct task_struct *tasks[LIST_LRU_TEST_WORKERS] = {};
	struct list_lru_test_worker workers[LIST_LRU_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct list_lru lru;
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	KUNIT_ASSERT_EQ(test, list_lru_init(&lru), 0);
	for (i = 0; i < LIST_LRU_TEST_WORKERS; i++) {
		workers[i].lru = &lru;
		workers[i].items = items[i];
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(list_lru_test_worker_fn, &workers[i],
				       "list-lru-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start list_lru worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(LIST_LRU_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < LIST_LRU_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != LIST_LRU_TEST_WORKERS) {
		KUNIT_FAIL(test, "list_lru workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < LIST_LRU_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != LIST_LRU_TEST_WORKERS)
		KUNIT_FAIL(test, "list_lru workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	if (started == LIST_LRU_TEST_WORKERS &&
	    atomic_read(&done) == LIST_LRU_TEST_WORKERS) {
		KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
		KUNIT_EXPECT_EQ(test,
				list_lru_count_one(&lru, LIST_LRU_TEST_NID, NULL),
				0UL);
		KUNIT_EXPECT_EQ(test,
				list_lru_count_node(&lru, LIST_LRU_TEST_NID), 0L);
		list_lru_test_expect_unlocked(test, &lru);
	}

	list_lru_destroy(&lru);
}

static struct kunit_case list_lru_test_cases[] = {
	KUNIT_CASE(list_lru_test_add_delete),
	KUNIT_CASE(list_lru_test_isolate),
	KUNIT_CASE(list_lru_test_isolate_move),
	KUNIT_CASE(list_lru_test_count_clamp),
	KUNIT_CASE(list_lru_test_walk_rotate_skip),
	KUNIT_CASE(list_lru_test_walk_removed),
	KUNIT_CASE(list_lru_test_walk_retry),
	KUNIT_CASE(list_lru_test_walk_removed_retry),
	KUNIT_CASE(list_lru_test_walk_stop),
	KUNIT_CASE(list_lru_test_concurrent),
	{}
};

static struct kunit_suite list_lru_test_suite = {
	.name = "list_lru",
	.test_cases = list_lru_test_cases,
};

kunit_test_suites(&list_lru_test_suite);

MODULE_DESCRIPTION("list_lru KUnit test suite");
MODULE_LICENSE("GPL");
