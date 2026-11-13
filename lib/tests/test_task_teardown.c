// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched/task.h>

#define TASK_TEARDOWN_TEST_ROUNDS 16

static int task_teardown_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void task_teardown_test_put_kthread(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;
	int round;

	for (round = 0; round < TASK_TEARDOWN_TEST_ROUNDS; round++) {
		init_completion(&started);
		task = kthread_run(task_teardown_test_thread, &started,
				   "task-teardown-%d", round);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
		KUNIT_ASSERT_NE(test, 0,
				wait_for_completion_timeout(&started, HZ));
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();
}

static struct kunit_case task_teardown_test_cases[] = {
	KUNIT_CASE(task_teardown_test_put_kthread),
	{}
};

static struct kunit_suite task_teardown_test_suite = {
	.name = "task_teardown",
	.test_cases = task_teardown_test_cases,
};

kunit_test_suite(task_teardown_test_suite);

MODULE_DESCRIPTION("KUnit tests for final task resource teardown");
MODULE_LICENSE("GPL");
