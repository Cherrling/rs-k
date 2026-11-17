// SPDX-License-Identifier: GPL-2.0
/*
 * KUnit tests for priority-sorted intrusive lists.
 */

#include <kunit/test.h>

#include <linux/limits.h>
#include <linux/module.h>
#include <linux/plist.h>

#define PLIST_TEST_ORDERING_NODES 10
#define PLIST_TEST_RANDOM_NODES 241
#define PLIST_TEST_RANDOM_ITERATIONS 2000
#define PLIST_TEST_MAX_BUCKETS 64

struct plist_test_node {
	struct plist_node node;
	unsigned int serial;
};

static struct plist_test_node *plist_test_alloc_nodes(struct kunit *test,
						      size_t count)
{
	struct plist_test_node *nodes;
	size_t i;

	nodes = kunit_kcalloc(test, count, sizeof(*nodes), GFP_KERNEL);
	if (!nodes)
		return NULL;

	for (i = 0; i < count; i++) {
		nodes[i].serial = i;
		plist_node_init(&nodes[i].node, 0);
	}

	return nodes;
}

static int plist_test_model_find(const unsigned int *model, size_t count,
				 unsigned int serial)
{
	size_t i;

	for (i = 0; i < count; i++) {
		if (model[i] == serial)
			return i;
	}

	return -1;
}

static void plist_test_model_insert(struct plist_test_node *nodes,
				    unsigned int *model, size_t *count,
				    unsigned int serial)
{
	size_t pos = 0;

	while (pos < *count &&
	       nodes[model[pos]].node.prio <= nodes[serial].node.prio)
		pos++;

	memmove(&model[pos + 1], &model[pos],
		(*count - pos) * sizeof(*model));
	model[pos] = serial;
	(*count)++;
}

static void plist_test_model_remove(unsigned int *model, size_t *count,
				    unsigned int serial)
{
	int pos = plist_test_model_find(model, *count, serial);

	if (pos < 0)
		return;

	memmove(&model[pos], &model[pos + 1],
		(*count - pos - 1) * sizeof(*model));
	(*count)--;
}

static void plist_test_model_requeue(struct plist_test_node *nodes,
				     unsigned int *model, size_t *count,
				     unsigned int serial)
{
	int pos = plist_test_model_find(model, *count, serial);

	if (pos < 0 || pos + 1 >= *count)
		return;
	if (nodes[model[pos + 1]].node.prio != nodes[serial].node.prio)
		return;

	plist_test_model_remove(model, count, serial);
	plist_test_model_insert(nodes, model, count, serial);
}

static bool plist_test_validate(struct kunit *test, struct plist_head *head,
				const unsigned int *expected, size_t count)
{
	struct plist_node *buckets[PLIST_TEST_MAX_BUCKETS];
	unsigned int actual[PLIST_TEST_RANDOM_NODES];
	struct list_head *pos;
	struct list_head *prev = &head->node_list;
	size_t bucket_count = 0;
	int last_prio = 0;
	size_t i;

	if (count == 0) {
		KUNIT_EXPECT_TRUE(test, plist_head_empty(head));
		KUNIT_EXPECT_PTR_EQ(test, head->node_list.next, &head->node_list);
		KUNIT_EXPECT_PTR_EQ(test, head->node_list.prev, &head->node_list);
		return plist_head_empty(head);
	}

	if (plist_head_empty(head)) {
		KUNIT_FAIL(test, "list is unexpectedly empty");
		return false;
	}

	for (i = 0, pos = head->node_list.next; i < count;
	     i++, pos = pos->next) {
		struct plist_test_node *entry;
		struct plist_node *node;

		if (pos == &head->node_list) {
			KUNIT_FAIL(test, "list ended after %zu of %zu nodes", i,
				   count);
			return false;
		}

		entry = container_of(pos, struct plist_test_node, node.node_list);
		node = &entry->node;
		actual[i] = entry->serial;
		KUNIT_EXPECT_PTR_EQ(test, pos->prev, prev);

		if (i == 0 || node->prio != last_prio) {
			if (bucket_count >= PLIST_TEST_MAX_BUCKETS) {
				KUNIT_FAIL(test, "too many priority buckets");
				return false;
			}
			buckets[bucket_count++] = node;
			if (i != 0)
				KUNIT_EXPECT_FALSE(test,
						   list_empty(&node->prio_list));
		} else {
			KUNIT_EXPECT_TRUE(test, list_empty(&node->prio_list));
		}

		if (i != 0)
			KUNIT_EXPECT_GE(test, node->prio, last_prio);
		last_prio = node->prio;
		prev = pos;
	}

	KUNIT_EXPECT_PTR_EQ(test, pos, &head->node_list);
	KUNIT_EXPECT_PTR_EQ(test, head->node_list.prev, prev);
	KUNIT_EXPECT_PTR_EQ(test, prev->next, &head->node_list);

	if (bucket_count == 0)
		return false;
	if (bucket_count > 1)
		KUNIT_EXPECT_FALSE(test, list_empty(&buckets[0]->prio_list));

	pos = &buckets[0]->prio_list;
	for (i = 0; i < bucket_count; i++) {
		struct plist_node *bucket;
		struct plist_node *next;
		struct list_head *next_pos;

		if (pos != &buckets[i]->prio_list) {
			KUNIT_FAIL(test, "priority bucket %zu is out of order", i);
			return false;
		}

		bucket = container_of(pos, struct plist_node, prio_list);
		next_pos = pos->next;
		KUNIT_EXPECT_PTR_EQ(test, next_pos->prev, pos);
		next = container_of(next_pos, struct plist_node, prio_list);
		if (i + 1 < bucket_count)
			KUNIT_EXPECT_LT(test, bucket->prio, next->prio);
		else
			KUNIT_EXPECT_PTR_EQ(test, next, buckets[0]);
		pos = next_pos;
	}

	KUNIT_EXPECT_PTR_EQ(test, pos, &buckets[0]->prio_list);
	if (expected)
		KUNIT_EXPECT_MEMEQ(test, actual, expected,
				   count * sizeof(*actual));

	return true;
}

static void plist_test_ordering(struct kunit *test)
{
	static const int priorities[PLIST_TEST_ORDERING_NODES] = {
		100, 0, 100, 50, INT_MIN, INT_MAX, 50, 100, 0, INT_MIN,
	};
	struct plist_test_node *nodes;
	struct plist_head head;
	unsigned int model[PLIST_TEST_ORDERING_NODES];
	size_t count = 0;
	int first_prio;
	int last_prio;
	size_t i;

	nodes = plist_test_alloc_nodes(test, PLIST_TEST_ORDERING_NODES);
	KUNIT_ASSERT_NOT_NULL(test, nodes);
	plist_head_init(&head);
	KUNIT_ASSERT_TRUE(test, plist_test_validate(test, &head, NULL, 0));

	for (i = 0; i < PLIST_TEST_ORDERING_NODES; i++) {
		nodes[i].node.prio = priorities[i];
		plist_add(&nodes[i].node, &head);
		plist_test_model_insert(nodes, model, &count, i);
		KUNIT_ASSERT_TRUE(test,
				  plist_test_validate(test, &head, model, count));
	}

	first_prio = plist_first(&head)->prio;
	last_prio = plist_last(&head)->prio;
	KUNIT_EXPECT_EQ(test, first_prio, INT_MIN);
	KUNIT_EXPECT_EQ(test, last_prio, INT_MAX);

	/*
	 * Removing the first node of the highest-priority bucket must promote
	 * the next node in that bucket to bucket representative.
	 */
	plist_del(&nodes[4].node, &head);
	plist_test_model_remove(model, &count, 4);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));
	KUNIT_EXPECT_PTR_EQ(test, plist_first(&head), &nodes[9].node);

	plist_del(&nodes[9].node, &head);
	plist_test_model_remove(model, &count, 9);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	/* Move the first of three equal-priority nodes behind both peers. */
	plist_requeue(&nodes[0].node, &head);
	plist_test_model_requeue(nodes, model, &count, 0);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	/* Requeue the middle entry and then the already-last entry. */
	plist_requeue(&nodes[7].node, &head);
	plist_test_model_requeue(nodes, model, &count, 7);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));
	plist_requeue(&nodes[7].node, &head);
	plist_test_model_requeue(nodes, model, &count, 7);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	/* Requeue adjacent equal-priority nodes near the front. */
	plist_requeue(&nodes[1].node, &head);
	plist_test_model_requeue(nodes, model, &count, 1);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	/* Requeue the last node in the complete list. */
	plist_requeue(&nodes[5].node, &head);
	plist_test_model_requeue(nodes, model, &count, 5);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	/* Remove the only entry in the lowest-priority bucket. */
	plist_del(&nodes[5].node, &head);
	plist_test_model_remove(model, &count, 5);
	KUNIT_ASSERT_TRUE(test,
			  plist_test_validate(test, &head, model, count));

	for (i = 0; i < PLIST_TEST_ORDERING_NODES; i++) {
		if (plist_test_model_find(model, count, i) < 0)
			continue;
		plist_del(&nodes[i].node, &head);
		plist_test_model_remove(model, &count, i);
		KUNIT_ASSERT_TRUE(test,
				  plist_test_validate(test, &head, model, count));
	}
}

static void plist_test_random_model(struct kunit *test)
{
	struct plist_test_node *nodes;
	bool *present;
	struct plist_head head;
	unsigned int model[PLIST_TEST_RANDOM_NODES];
	unsigned int state = 0x193939;
	size_t count = 0;
	size_t i;

	nodes = plist_test_alloc_nodes(test, PLIST_TEST_RANDOM_NODES);
	KUNIT_ASSERT_NOT_NULL(test, nodes);
	present = kunit_kcalloc(test, PLIST_TEST_RANDOM_NODES,
				sizeof(*present), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, present);
	plist_head_init(&head);

	for (i = 0; i < PLIST_TEST_RANDOM_NODES; i++) {
		state = state * 193939 % 47629;
		nodes[i].node.prio = state % 32;
	}

	for (i = 0; i < PLIST_TEST_RANDOM_ITERATIONS; i++) {
		unsigned int serial;

		state = state * 193939 % 47629;
		serial = state % PLIST_TEST_RANDOM_NODES;
		if (!present[serial]) {
			plist_add(&nodes[serial].node, &head);
			present[serial] = true;
			plist_test_model_insert(nodes, model, &count, serial);
		} else {
			plist_del(&nodes[serial].node, &head);
			present[serial] = false;
			plist_test_model_remove(model, &count, serial);
		}

		if (present[serial]) {
			plist_requeue(&nodes[serial].node, &head);
			plist_test_model_requeue(nodes, model, &count, serial);
		}

		if (!plist_test_validate(test, &head, model, count)) {
			KUNIT_FAIL(test, "model mismatch after iteration %zu", i);
			return;
		}
	}

	for (i = 0; i < PLIST_TEST_RANDOM_NODES; i++) {
		if (!present[i])
			continue;
		plist_del(&nodes[i].node, &head);
		present[i] = false;
		plist_test_model_remove(model, &count, i);
		if (!plist_test_validate(test, &head, model, count)) {
			KUNIT_FAIL(test, "model mismatch after draining node %zu", i);
			return;
		}
	}
}

static struct kunit_case plist_test_cases[] = {
	KUNIT_CASE(plist_test_ordering),
	KUNIT_CASE(plist_test_random_model),
	{}
};

static struct kunit_suite plist_test_suite = {
	.name = "plist",
	.test_cases = plist_test_cases,
};

kunit_test_suite(plist_test_suite);

MODULE_DESCRIPTION("Priority-sorted intrusive list KUnit tests");
MODULE_LICENSE("GPL");
