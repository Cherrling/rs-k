// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/task-release-rust.h>

#define TASK_RELEASE_TEST_ROUNDS 24

static int task_release_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void task_release_test_kthreads(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;
	int round;

	for (round = 0; round < TASK_RELEASE_TEST_ROUNDS; round++) {
		init_completion(&started);
		task = kthread_run(task_release_test_thread, &started,
				   "task-release-%d", round);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
		KUNIT_ASSERT_NE(test, 0,
				wait_for_completion_timeout(&started, HZ));
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();
#ifdef CONFIG_RUST_TASK_RELEASE
	KUNIT_EXPECT_GT(test, rust_task_release_test_release_count(), 0);
#endif
}

#ifdef CONFIG_RUST_TASK_RELEASE
static void task_release_test_leader_policy(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test,
			  rust_task_release_test_should_notify_leader(false, true,
								      EXIT_ZOMBIE));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_release_test_should_notify_leader(true, true,
								       EXIT_ZOMBIE));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_release_test_should_notify_leader(false, false,
								       EXIT_ZOMBIE));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_release_test_should_notify_leader(false, true,
								       EXIT_DEAD));
	KUNIT_EXPECT_EQ(test, 73,
			rust_task_release_test_exit_code(SIGNAL_GROUP_EXIT, 73, 11));
	KUNIT_EXPECT_EQ(test, 11,
			rust_task_release_test_exit_code(0, 73, 11));
}
#endif

static struct kunit_case task_release_test_cases[] = {
	KUNIT_CASE(task_release_test_kthreads),
#ifdef CONFIG_RUST_TASK_RELEASE
	KUNIT_CASE(task_release_test_leader_policy),
#endif
	{}
};

static struct kunit_suite task_release_test_suite = {
	.name = "task_release",
	.test_cases = task_release_test_cases,
};

kunit_test_suite(task_release_test_suite);

MODULE_DESCRIPTION("KUnit tests for zombie task release");
MODULE_LICENSE("GPL");
