// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/limits.h>
#include <linux/sort.h>
#include <linux/timerqueue.h>

#define TIMERQUEUE_TEST_NODES 10

static int timerqueue_test_cmp_s64(const void *a, const void *b)
{
	s64 left = *(const s64 *)a;
	s64 right = *(const s64 *)b;

	return (left > right) - (left < right);
}

static void timerqueue_test_check_cached(struct kunit *test,
					 struct timerqueue_head *head,
					 const s64 *expected, int count)
{
	struct timerqueue_node *node;
	int i = 0;

	node = timerqueue_getnext(head);
	while (node) {
		KUNIT_ASSERT_LT(test, i, count);
		KUNIT_EXPECT_EQ(test, node->expires, expected[i]);
		node = timerqueue_iterate_next(node);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, count);
	KUNIT_EXPECT_PTR_EQ(test, timerqueue_iterate_next(NULL), NULL);
}

static void timerqueue_test_cached_lifecycle(struct kunit *test)
{
	static const s64 values[TIMERQUEUE_TEST_NODES] = {
		50, 10, 90, 20, 70, 30, 80, 40, 60, 0,
	};
	static const int add_order[TIMERQUEUE_TEST_NODES] = {
		4, 1, 8, 0, 9, 3, 7, 2, 6, 5,
	};
	static const int del_order[TIMERQUEUE_TEST_NODES] = {
		5, 1, 8, 0, 9, 3, 7, 2, 6, 4,
	};
	struct timerqueue_node nodes[TIMERQUEUE_TEST_NODES];
	struct timerqueue_head head;
	s64 expected[TIMERQUEUE_TEST_NODES];
	s64 minimum = 0;
	int remaining = 0;
	int i;

	timerqueue_init_head(&head);
	for (i = 0; i < TIMERQUEUE_TEST_NODES; i++) {
		nodes[i].expires = values[i];
		timerqueue_init(&nodes[i]);
	}

	for (i = 0; i < ARRAY_SIZE(add_order); i++) {
		int index = add_order[i];
		bool new_leftmost;
		int j;

		new_leftmost = !remaining || values[index] < minimum;
		KUNIT_EXPECT_EQ(test, timerqueue_add(&head, &nodes[index]),
				new_leftmost);
		if (new_leftmost)
			minimum = values[index];

		remaining++;
		for (j = 0; j < remaining; j++)
			expected[j] = values[add_order[j]];
		sort(expected, remaining, sizeof(*expected),
		     timerqueue_test_cmp_s64, NULL);
		timerqueue_test_check_cached(test, &head, expected, remaining);
	}

	for (i = 0; i < ARRAY_SIZE(del_order); i++) {
		int index = del_order[i];
		bool expected_nonempty = remaining > 1;
		int j;
		int k;

		KUNIT_EXPECT_EQ(test, timerqueue_del(&head, &nodes[index]),
				expected_nonempty);
		KUNIT_EXPECT_TRUE(test, RB_EMPTY_NODE(&nodes[index].node));
		remaining--;

		for (j = 0, k = 0; j < TIMERQUEUE_TEST_NODES; j++) {
			int n;

			for (n = 0; n <= i; n++)
				if (del_order[n] == j)
					break;
			if (n <= i)
				continue;
			expected[k++] = values[j];
		}
		sort(expected, remaining, sizeof(*expected),
		     timerqueue_test_cmp_s64, NULL);
		timerqueue_test_check_cached(test, &head, expected, remaining);
	}

	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_leftmost, NULL);
	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_root.rb_node, NULL);
}

static void timerqueue_test_signed_boundaries(struct kunit *test)
{
	static const s64 values[] = {
		0, S64_MIN, -1, S64_MAX, 1, 0, 0,
	};
	static const int add_order[] = {
		3, 0, 5, 1, 6, 2, 4,
	};
	static const s64 expected[] = {
		S64_MIN, -1, 0, 0, 0, 1, S64_MAX,
	};
	struct timerqueue_node nodes[ARRAY_SIZE(values)];
	struct timerqueue_head head;
	int i;

	timerqueue_init_head(&head);
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		nodes[i].expires = values[i];
		timerqueue_init(&nodes[i]);
	}

	for (i = 0; i < ARRAY_SIZE(add_order); i++)
		timerqueue_add(&head, &nodes[add_order[i]]);

	timerqueue_test_check_cached(test, &head, expected, ARRAY_SIZE(expected));
	for (i = 0; i < ARRAY_SIZE(nodes); i++)
		timerqueue_del(&head, &nodes[i]);
	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_root.rb_node, NULL);
}

static void timerqueue_test_check_linked(struct kunit *test,
					 struct timerqueue_linked_head *head,
					 const s64 *expected, int count)
{
	struct timerqueue_linked_node *first;
	struct timerqueue_linked_node *node;
	struct timerqueue_linked_node *previous = NULL;
	int i = 0;

	first = timerqueue_linked_first(head);
	node = first;
	while (node) {
		KUNIT_ASSERT_LT(test, i, count);
		KUNIT_EXPECT_EQ(test, node->expires, expected[i]);
		KUNIT_EXPECT_PTR_EQ(test, timerqueue_linked_prev(node), previous);
		if (previous)
			KUNIT_EXPECT_PTR_EQ(test, timerqueue_linked_next(previous),
					    node);
		previous = node;
		node = timerqueue_linked_next(node);
		i++;
	}

	KUNIT_EXPECT_EQ(test, i, count);
	if (previous)
		KUNIT_EXPECT_PTR_EQ(test, timerqueue_linked_next(previous), NULL);
	KUNIT_EXPECT_PTR_EQ(test, head->rb_root.rb_leftmost,
			    count ? &first->node : NULL);
}

static void timerqueue_test_linked_lifecycle(struct kunit *test)
{
	static const s64 values[] = {
		40, 10, 70, 20, 30, 60, 50,
	};
	static const int add_order[] = {
		3, 0, 6, 1, 5, 2, 4,
	};
	static const int del_order[] = {
		2, 5, 0, 6, 1, 4, 3,
	};
	static const s64 expected_first[] = {
		10, 20, 30, 40, 50, 60, 70,
	};
	struct timerqueue_linked_node nodes[ARRAY_SIZE(values)];
	struct timerqueue_linked_head head;
	s64 expected[ARRAY_SIZE(values)];
	s64 minimum = 0;
	int remaining = 0;
	int i;
	int j;

	timerqueue_linked_init_head(&head);
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		nodes[i].expires = values[i];
		timerqueue_linked_init(&nodes[i]);
	}

	for (i = 0; i < ARRAY_SIZE(add_order); i++) {
		int index = add_order[i];
		bool new_leftmost;

		new_leftmost = !remaining || values[index] < minimum;
		KUNIT_EXPECT_EQ(test,
				timerqueue_linked_add(&head, &nodes[index]),
				new_leftmost);
		if (new_leftmost)
			minimum = values[index];
		remaining++;

		for (j = 0; j < remaining; j++)
			expected[j] = values[add_order[j]];
		sort(expected, remaining, sizeof(*expected),
		     timerqueue_test_cmp_s64, NULL);
		timerqueue_test_check_linked(test, &head, expected, remaining);
	}

	timerqueue_test_check_linked(test, &head, expected_first,
				     ARRAY_SIZE(expected_first));

	for (i = 0; i < ARRAY_SIZE(del_order); i++) {
		int index = del_order[i];
		bool expected_nonempty = remaining > 1;
		int k = 0;

		KUNIT_EXPECT_EQ(test,
				timerqueue_linked_del(&head, &nodes[index]),
				expected_nonempty);
		KUNIT_EXPECT_FALSE(test,
				   timerqueue_linked_node_queued(&nodes[index]));
		remaining--;

		for (j = 0; j < ARRAY_SIZE(nodes); j++) {
			int n;

			for (n = 0; n <= i; n++)
				if (del_order[n] == j)
					break;
			if (n <= i)
				continue;
			expected[k++] = values[j];
		}
		sort(expected, remaining, sizeof(*expected),
		     timerqueue_test_cmp_s64, NULL);
		timerqueue_test_check_linked(test, &head, expected, remaining);
	}

	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_leftmost, NULL);
	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_root.rb_node, NULL);
}

static void timerqueue_test_linked_signed_boundaries(struct kunit *test)
{
	static const s64 values[] = {
		0, S64_MIN, -1, S64_MAX, 1, 0, 0,
	};
	static const int add_order[] = {
		3, 0, 5, 1, 6, 2, 4,
	};
	static const s64 expected[] = {
		S64_MIN, -1, 0, 0, 0, 1, S64_MAX,
	};
	struct timerqueue_linked_node nodes[ARRAY_SIZE(values)];
	struct timerqueue_linked_head head;
	int i;

	timerqueue_linked_init_head(&head);
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		nodes[i].expires = values[i];
		timerqueue_linked_init(&nodes[i]);
	}

	for (i = 0; i < ARRAY_SIZE(add_order); i++)
		timerqueue_linked_add(&head, &nodes[add_order[i]]);

	timerqueue_test_check_linked(test, &head, expected, ARRAY_SIZE(expected));
	for (i = 0; i < ARRAY_SIZE(nodes); i++) {
		KUNIT_EXPECT_EQ(test, timerqueue_linked_del(&head, &nodes[i]),
				i < ARRAY_SIZE(nodes) - 1);
		KUNIT_EXPECT_FALSE(test,
				   timerqueue_linked_node_queued(&nodes[i]));
	}
	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_leftmost, NULL);
	KUNIT_EXPECT_PTR_EQ(test, head.rb_root.rb_root.rb_node, NULL);
}

static struct kunit_case timerqueue_test_cases[] = {
	KUNIT_CASE(timerqueue_test_cached_lifecycle),
	KUNIT_CASE(timerqueue_test_signed_boundaries),
	KUNIT_CASE(timerqueue_test_linked_lifecycle),
	KUNIT_CASE(timerqueue_test_linked_signed_boundaries),
	{}
};

static struct kunit_suite timerqueue_test_suite = {
	.name = "timerqueue",
	.test_cases = timerqueue_test_cases,
};

kunit_test_suite(timerqueue_test_suite);

MODULE_DESCRIPTION("KUnit tests for timer queues");
MODULE_LICENSE("GPL");
