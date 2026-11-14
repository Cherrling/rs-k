// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/processor.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/signal-wake-select-rust.h>
#include <linux/spinlock.h>

enum signal_wake_select_test_kind {
	SIGNAL_WAKE_SELECT_FATAL,
	SIGNAL_WAKE_SELECT_PTRACE,
};

struct signal_wake_select_test_result {
	unsigned long jobctl;
	bool pending;
};

static int signal_wake_select_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);
	while (!kthread_should_stop())
		cpu_relax();

	return 0;
}

static struct signal_wake_select_test_result
signal_wake_select_test_call(struct task_struct *task,
			     enum signal_wake_select_test_kind kind,
			     bool value, unsigned long jobctl)
{
	struct signal_wake_select_test_result result;

#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	rust_signal_wake_select_test_reset();
#endif

	spin_lock_irq(&task->sighand->siglock);
	task->jobctl = jobctl;
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
	if (kind == SIGNAL_WAKE_SELECT_FATAL)
		signal_wake_up(task, value);
	else
		ptrace_signal_wake_up(task, value);
	result.jobctl = task->jobctl;
	result.pending = task_sigpending(task);
	spin_unlock_irq(&task->sighand->siglock);

	return result;
}

static void signal_wake_select_test_group(struct kunit *test)
{
	struct completion started;
	struct signal_wake_select_test_result result;
	struct task_struct *task;
	unsigned long both = JOBCTL_STOPPED | JOBCTL_TRACED;
	unsigned long frozen = both | JOBCTL_PTRACE_FROZEN;
	unsigned long listening = both | JOBCTL_LISTENING;
	int stop_result;

	init_completion(&started);
	task = kthread_create(signal_wake_select_test_thread, &started,
			      "signal-wake-select");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to create running task");
		return;
	}

	wake_up_process(task);
	if (!wait_for_completion_timeout(&started, HZ)) {
		KUNIT_FAIL(test, "running task did not start");
		kthread_stop(task);
		return;
	}

	result = signal_wake_select_test_call(task, SIGNAL_WAKE_SELECT_FATAL,
					      true, listening);
	KUNIT_EXPECT_EQ(test, JOBCTL_LISTENING, result.jobctl);
	KUNIT_EXPECT_TRUE(test, result.pending);
#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_fatal_entry_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_select_test_frozen_skip_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_fatal_clear_count());
	KUNIT_EXPECT_EQ(test, TASK_WAKEKILL | __TASK_TRACED,
			rust_signal_wake_select_test_last_state());
#endif

	result = signal_wake_select_test_call(task, SIGNAL_WAKE_SELECT_FATAL,
					      true, frozen);
	KUNIT_EXPECT_EQ(test, frozen, result.jobctl);
	KUNIT_EXPECT_TRUE(test, result.pending);
#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_fatal_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_frozen_skip_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_select_test_fatal_clear_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_wake_select_test_last_state());
#endif

	result = signal_wake_select_test_call(task, SIGNAL_WAKE_SELECT_FATAL,
					      false, both);
	KUNIT_EXPECT_EQ(test, both, result.jobctl);
	KUNIT_EXPECT_TRUE(test, result.pending);
#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_fatal_entry_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_select_test_frozen_skip_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_select_test_fatal_clear_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_wake_select_test_last_state());
#endif

	result = signal_wake_select_test_call(task, SIGNAL_WAKE_SELECT_PTRACE,
					      true, both);
	KUNIT_EXPECT_EQ(test, JOBCTL_STOPPED, result.jobctl);
	KUNIT_EXPECT_TRUE(test, result.pending);
#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_ptrace_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_ptrace_resume_count());
	KUNIT_EXPECT_EQ(test, __TASK_TRACED,
			rust_signal_wake_select_test_last_state());
#endif

	result = signal_wake_select_test_call(task, SIGNAL_WAKE_SELECT_PTRACE,
					      false, both);
	KUNIT_EXPECT_EQ(test, both, result.jobctl);
	KUNIT_EXPECT_TRUE(test, result.pending);
#ifdef CONFIG_RUST_SIGNAL_WAKE_SELECT
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_wake_select_test_ptrace_entry_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_wake_select_test_ptrace_resume_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_wake_select_test_last_state());
#endif

	spin_lock_irq(&task->sighand->siglock);
	task->jobctl = 0;
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
	spin_unlock_irq(&task->sighand->siglock);

	stop_result = kthread_stop(task);
	KUNIT_EXPECT_EQ(test, 0, stop_result);
}

static struct kunit_case signal_wake_select_test_cases[] = {
	KUNIT_CASE(signal_wake_select_test_group),
	{}
};

static struct kunit_suite signal_wake_select_test_suite = {
	.name = "signal_wake_select",
	.test_cases = signal_wake_select_test_cases,
};

kunit_test_suite(signal_wake_select_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal wakeup state selection");
MODULE_LICENSE("GPL");
