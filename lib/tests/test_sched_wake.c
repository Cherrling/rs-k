// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched-rust.h>
#include <linux/slab.h>

static int sched_wake_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);
	while (!kthread_should_stop())
		schedule_timeout_interruptible(msecs_to_jiffies(10));

	return 0;
}

#ifdef CONFIG_RUST_SCHED_WAKE_STATE
static void sched_wake_test_state_match(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1,
			rust_sched_state_match_select(TASK_INTERRUPTIBLE,
						      TASK_RUNNING,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, -1,
			rust_sched_state_match_select(TASK_RTLOCK_WAIT,
						      TASK_INTERRUPTIBLE,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, 0,
			rust_sched_state_match_select(TASK_WAKING, TASK_RUNNING,
						      TASK_NORMAL));
	KUNIT_EXPECT_EQ(test, 1,
			rust_sched_state_match_select(TASK_INTERRUPTIBLE,
						      TASK_UNINTERRUPTIBLE,
						      TASK_NORMAL));
}

static void sched_wake_test_saved_state_transition(struct kunit *test)
{
	struct task_struct *task;
	int success;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	task->__state = TASK_RTLOCK_WAIT;
	task->saved_state = TASK_INTERRUPTIBLE;
	success = 0;
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 1, success);
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, task->saved_state);

	task->__state = TASK_UNINTERRUPTIBLE;
	task->saved_state = TASK_RUNNING;
	success = 0;
	KUNIT_EXPECT_TRUE(test,
			  rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 1, success);
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, task->saved_state);

	task->__state = TASK_WAKING;
	success = 1;
	KUNIT_EXPECT_FALSE(test,
			   rust_sched_ttwu_state_match(task, TASK_NORMAL, &success));
	KUNIT_EXPECT_EQ(test, 0, success);

	kfree(task);
}
#endif

static void sched_wake_test_current(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0, wake_up_process(current));
	KUNIT_EXPECT_EQ(test, TASK_RUNNING, current->__state);
}

static void sched_wake_test_stopped_task(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;

	init_completion(&started);
	task = kthread_create(sched_wake_test_thread, &started,
			      "sched-wake-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);

	KUNIT_EXPECT_EQ(test, 1, wake_up_process(task));
	wait_for_completion(&started);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop(task));
}

static struct kunit_case sched_wake_test_cases[] = {
#ifdef CONFIG_RUST_SCHED_WAKE_STATE
	KUNIT_CASE(sched_wake_test_state_match),
	KUNIT_CASE(sched_wake_test_saved_state_transition),
#endif
	KUNIT_CASE(sched_wake_test_current),
	KUNIT_CASE(sched_wake_test_stopped_task),
	{}
};

static struct kunit_suite sched_wake_test_suite = {
	.name = "sched_wake",
	.test_cases = sched_wake_test_cases,
};

kunit_test_suite(sched_wake_test_suite);
