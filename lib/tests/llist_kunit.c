// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/llist.h>
#include <linux/slab.h>

#define LLIST_TEST_PRODUCERS 4
#define LLIST_TEST_NODES_PER_PRODUCER 4096
#define LLIST_TEST_NODES (LLIST_TEST_PRODUCERS * LLIST_TEST_NODES_PER_PRODUCER)
#define LLIST_TEST_TIMEOUT_MS 30000

struct llist_test_node {
	struct llist_node node;
	unsigned int id;
};

struct llist_test_worker {
	struct llist_head *head;
	struct llist_test_node *nodes;
	unsigned int first;
	unsigned int count;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
};

static void llist_test_mark(struct llist_test_node *nodes,
			    unsigned int count, u32 *seen,
			    struct llist_node *node,
			    unsigned int *consumed)
{
	struct llist_test_node *entry = llist_entry(node, typeof(*entry), node);

	if (entry->id < count) {
		if (seen[entry->id] != U32_MAX)
			seen[entry->id]++;
	}
	(*consumed)++;
}

static int llist_test_producer(void *data)
{
	struct llist_test_worker *worker = data;
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < worker->count; i++)
		llist_add(&worker->nodes[worker->first + i].node,
			  worker->head);

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void llist_test_del_first(struct kunit *test)
{
	struct llist_test_node nodes[3];
	struct llist_node *node;
	LLIST_HEAD(head);

	KUNIT_EXPECT_PTR_EQ(test, llist_del_first(&head), NULL);

	llist_add(&nodes[0].node, &head);
	llist_add(&nodes[1].node, &head);
	llist_add(&nodes[2].node, &head);

	node = llist_del_first(&head);
	KUNIT_EXPECT_PTR_EQ(test, node, &nodes[2].node);
	node = llist_del_first(&head);
	KUNIT_EXPECT_PTR_EQ(test, node, &nodes[1].node);
	node = llist_del_first(&head);
	KUNIT_EXPECT_PTR_EQ(test, node, &nodes[0].node);
	KUNIT_EXPECT_PTR_EQ(test, llist_del_first(&head), NULL);
}

static void llist_test_del_first_this(struct kunit *test)
{
	struct llist_test_node nodes[3];
	LLIST_HEAD(head);

	KUNIT_EXPECT_FALSE(test, llist_del_first_this(&head, &nodes[0].node));

	llist_add(&nodes[0].node, &head);
	llist_add(&nodes[1].node, &head);
	llist_add(&nodes[2].node, &head);

	KUNIT_EXPECT_FALSE(test, llist_del_first_this(&head, &nodes[0].node));
	KUNIT_EXPECT_TRUE(test, llist_del_first_this(&head, &nodes[2].node));
	KUNIT_EXPECT_TRUE(test, llist_del_first_this(&head, &nodes[1].node));
	KUNIT_EXPECT_FALSE(test, llist_del_first_this(&head, &nodes[1].node));
	KUNIT_EXPECT_TRUE(test, llist_del_first_this(&head, &nodes[0].node));
	KUNIT_EXPECT_TRUE(test, llist_empty(&head));
}

static void llist_test_reverse_order(struct kunit *test)
{
	struct llist_test_node nodes[3];
	struct llist_node *node;
	LLIST_HEAD(head);

	KUNIT_EXPECT_PTR_EQ(test, llist_reverse_order(NULL), NULL);

	llist_add(&nodes[0].node, &head);
	llist_add(&nodes[1].node, &head);
	llist_add(&nodes[2].node, &head);

	node = llist_reverse_order(llist_del_all(&head));
	KUNIT_EXPECT_PTR_EQ(test, node, &nodes[0].node);
	KUNIT_EXPECT_PTR_EQ(test, llist_next(node), &nodes[1].node);
	node = llist_next(node);
	KUNIT_EXPECT_PTR_EQ(test, llist_next(node), &nodes[2].node);
	node = llist_next(node);
	KUNIT_EXPECT_PTR_EQ(test, llist_next(node), NULL);
}

static void llist_test_concurrent_del_first(struct kunit *test)
{
	struct llist_test_worker workers[LLIST_TEST_PRODUCERS];
	struct task_struct *tasks[LLIST_TEST_PRODUCERS] = {};
	struct llist_test_node *nodes;
	struct llist_node *node;
	unsigned long deadline;
	unsigned int consumed = 0;
	unsigned int duplicates = 0;
	unsigned int missing = 0;
	unsigned int started = 0;
	u32 *seen;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	LLIST_HEAD(head);
	int i;

	nodes = kunit_kcalloc(test, LLIST_TEST_NODES, sizeof(*nodes),
			      GFP_KERNEL);
	seen = kunit_kcalloc(test, LLIST_TEST_NODES, sizeof(*seen), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, nodes);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, seen);

	for (i = 0; i < LLIST_TEST_NODES; i++)
		nodes[i].id = i;

	deadline = jiffies + msecs_to_jiffies(LLIST_TEST_TIMEOUT_MS);
	for (i = 0; i < LLIST_TEST_PRODUCERS; i++) {
		workers[i].head = &head;
		workers[i].nodes = nodes;
		workers[i].first = i * LLIST_TEST_NODES_PER_PRODUCER;
		workers[i].count = LLIST_TEST_NODES_PER_PRODUCER;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		tasks[i] = kthread_run(llist_test_producer, &workers[i],
				       "llist-%d", i);
		if (IS_ERR(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start llist producer");
			goto cleanup;
		}
		started++;
	}

	while (atomic_read(&ready) < LLIST_TEST_PRODUCERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != LLIST_TEST_PRODUCERS) {
		KUNIT_FAIL(test, "llist producers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (time_before(jiffies, deadline)) {
		node = llist_del_first(&head);
		if (node)
			llist_test_mark(nodes, LLIST_TEST_NODES, seen, node,
					&consumed);

		if (atomic_read(&done) == LLIST_TEST_PRODUCERS &&
		    llist_empty(&head))
			break;

		if (!node)
			cond_resched();
	}

	if (time_after_eq(jiffies, deadline))
		KUNIT_FAIL(test, "concurrent llist producers timed out");

cleanup:
	atomic_set(&start, 1);
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	while ((node = llist_del_first(&head)))
		llist_test_mark(nodes, LLIST_TEST_NODES, seen, node, &consumed);

	for (i = 0; i < LLIST_TEST_NODES; i++) {
		if (!seen[i])
			missing++;
		else if (seen[i] > 1)
			duplicates++;
	}

	KUNIT_EXPECT_EQ(test, consumed, LLIST_TEST_NODES);
	KUNIT_EXPECT_EQ(test, missing, 0);
	KUNIT_EXPECT_EQ(test, duplicates, 0);
}

static struct kunit_case llist_test_cases[] = {
	KUNIT_CASE(llist_test_del_first),
	KUNIT_CASE(llist_test_del_first_this),
	KUNIT_CASE(llist_test_reverse_order),
	KUNIT_CASE(llist_test_concurrent_del_first),
	{}
};

static struct kunit_suite llist_test_suite = {
	.name = "llist",
	.test_cases = llist_test_cases,
};

kunit_test_suites(&llist_test_suite);

MODULE_DESCRIPTION("lock-less list KUnit test suite");
MODULE_LICENSE("GPL");
