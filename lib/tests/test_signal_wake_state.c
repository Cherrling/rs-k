// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/errno.h>
#include <linux/kthread.h>
#include <linux/processor.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal-wake-state-rust.h>
#include <linux/spinlock.h>

struct signal_wake_state_test_context {
	struct completion started;
	struct completion woken;
	struct completion release;
	int wait_result;
};

static int signal_wake_state_test_sleeping_thread(void *data)
{
	struct signal_wake_state_test_context *context = data;

	complete(&context->started);
	context->wait_result = wait_for_completion_interruptible(&context->release);
	complete(&context->woken);

	return 0;
}

static int signal_wake_state_test_running_thread(void *data)
{
	struct completion *started = data;

	complete(started);
	while (!kthread_should_stop())
		cpu_relax();

	return 0;
}

static bool signal_wake_state_test_wait_for_sleep(struct task_struct *task)
{
	unsigned int round;

	for (round = 0; round < 2 * HZ; round++) {
		if (READ_ONCE(task->__state) & TASK_INTERRUPTIBLE)
			return true;
		schedule_timeout_interruptible(1);
	}

	return false;
}

static bool signal_wake_state_test_call(struct task_struct *task,
					unsigned int state)
{
	bool pending;

	spin_lock_irq(&task->sighand->siglock);
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
	signal_wake_up_state(task, state);
	pending = task_sigpending(task);
	spin_unlock_irq(&task->sighand->siglock);

	return pending;
}

static void signal_wake_state_test_sleeping(struct kunit *test)
{
	struct signal_wake_state_test_context context;
	struct task_struct *task;
	unsigned int state = TASK_WAKEKILL | __TASK_TRACED;
#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE
	unsigned int wake_state = state | TASK_INTERRUPTIBLE;
#endif
	bool pending;
	int stop_result;

	init_completion(&context.started);
	init_completion(&context.woken);
	init_completion(&context.release);
	context.wait_result = 0;

	task = kthread_create(signal_wake_state_test_sleeping_thread, &context,
			      "signal-wake-sleep");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	wake_up_process(task);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.started, HZ));
	KUNIT_ASSERT_TRUE(test, signal_wake_state_test_wait_for_sleep(task));

#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE
	rust_signal_wake_state_test_reset();
#endif
	pending = signal_wake_state_test_call(task, state);
	KUNIT_EXPECT_TRUE(test, pending);
	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.woken, HZ));
	KUNIT_EXPECT_EQ(test, -ERESTARTSYS, context.wait_result);

#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE
	KUNIT_EXPECT_EQ(test, 1, rust_signal_wake_state_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_state_test_set_pending_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_state_test_wake_attempt_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_state_test_wake_success_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_wake_state_test_kick_count());
	KUNIT_EXPECT_EQ(test, wake_state,
			rust_signal_wake_state_test_last_wake_state());
#endif

	stop_result = kthread_stop(task);
	KUNIT_EXPECT_EQ(test, 0, stop_result);
}

static void signal_wake_state_test_running(struct kunit *test)
{
	struct completion started;
	struct task_struct *task;
	bool pending;
	int stop_result;

	init_completion(&started);
	task = kthread_create(signal_wake_state_test_running_thread, &started,
			      "signal-wake-run");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	wake_up_process(task);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&started, HZ));

#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE
	rust_signal_wake_state_test_reset();
#endif
	pending = signal_wake_state_test_call(task, 0);
	KUNIT_EXPECT_TRUE(test, pending);

#ifdef CONFIG_RUST_SIGNAL_WAKE_STATE
	KUNIT_EXPECT_EQ(test, 1, rust_signal_wake_state_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_state_test_set_pending_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_state_test_wake_attempt_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_state_test_wake_success_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_wake_state_test_kick_count());
	KUNIT_EXPECT_EQ(test, TASK_INTERRUPTIBLE,
			rust_signal_wake_state_test_last_wake_state());
#endif

	stop_result = kthread_stop(task);
	KUNIT_EXPECT_EQ(test, 0, stop_result);
}

static struct kunit_case signal_wake_state_test_cases[] = {
	KUNIT_CASE(signal_wake_state_test_sleeping),
	KUNIT_CASE(signal_wake_state_test_running),
	{}
};

static struct kunit_suite signal_wake_state_test_suite = {
	.name = "signal_wake_state",
	.test_cases = signal_wake_state_test_cases,
};

kunit_test_suite(signal_wake_state_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal wakeup state");
MODULE_LICENSE("GPL");
