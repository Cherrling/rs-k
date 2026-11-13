// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/task-exit-notify-rust.h>
#include <linux/wait.h>
#include <uapi/linux/wait.h>

#define TASK_EXIT_NOTIFY_TEST_ROUNDS 24

struct task_exit_notify_reparent_context {
	struct completion child_created;
	struct completion parent_exit;
	struct completion child_started;
	struct completion child_stop;
	struct completion child_done;
	pid_t child_pid;
};

static int task_exit_notify_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void task_exit_notify_test_kthreads(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;
	int round;

	for (round = 0; round < TASK_EXIT_NOTIFY_TEST_ROUNDS; round++) {
		init_completion(&started);
		task = kthread_run(task_exit_notify_test_thread, &started,
				   "task-exit-notify-%d", round);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
		KUNIT_ASSERT_NE(test, 0,
				wait_for_completion_timeout(&started, HZ));
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();
#ifdef CONFIG_RUST_TASK_EXIT_NOTIFY
	KUNIT_EXPECT_GT(test, rust_task_exit_notify_test_count(), 0);
#endif
}

static int task_exit_notify_test_child(void *data)
{
	struct task_exit_notify_reparent_context *context = data;

	current->signal->autoreap = true;
	complete(&context->child_started);
	wait_for_completion(&context->child_stop);
	complete(&context->child_done);

	kthread_exit(0);
}

static int task_exit_notify_test_parent(void *data)
{
	struct task_exit_notify_reparent_context *context = data;

	context->child_pid = kernel_thread(task_exit_notify_test_child, context,
					   "task-exit-child",
					   CLONE_FS | CLONE_FILES | SIGCHLD);
	complete(&context->child_created);
	wait_for_completion(&context->parent_exit);

	return 0;
}

static void task_exit_notify_test_reparent(struct kunit *test)
{
	struct task_exit_notify_reparent_context context;
	struct task_struct *task;

	init_completion(&context.child_created);
	init_completion(&context.parent_exit);
	init_completion(&context.child_started);
	init_completion(&context.child_stop);
	init_completion(&context.child_done);

	task = kthread_run(task_exit_notify_test_parent, &context,
			   "task-exit-parent");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	get_task_struct(task);

	if (!wait_for_completion_timeout(&context.child_created, HZ)) {
		complete(&context.parent_exit);
		kthread_stop_put(task);
		KUNIT_FAIL(test, "parent did not report its child");
		return;
	}
	KUNIT_EXPECT_GT(test, context.child_pid, 0);
	if (context.child_pid <= 0) {
		complete(&context.parent_exit);
		kthread_stop_put(task);
		return;
	}
	if (!wait_for_completion_timeout(&context.child_started, HZ)) {
		complete(&context.parent_exit);
		kthread_stop_put(task);
		complete(&context.child_stop);
		KUNIT_FAIL(test, "child did not start");
		return;
	}

	complete(&context.parent_exit);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	complete(&context.child_stop);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.child_done, HZ));

	rcu_barrier();
#ifdef CONFIG_RUST_TASK_EXIT_NOTIFY
	KUNIT_EXPECT_GT(test, rust_task_exit_notify_test_reparent_count(), 0);
#endif
}

#if defined(CONFIG_RUST_TASK_EXIT_NOTIFY) && defined(CONFIG_PID_NS)
struct task_exit_notify_pidns_context {
	struct completion init_started;
	struct completion thread_started;
	struct completion exit;
	struct completion thread_stop;
	struct completion thread_done;
	struct signal_struct *signal;
	pid_t thread_pid;
	bool init_is_child_reaper;
	bool thread_shared_signal;
};

static int task_exit_notify_test_pidns_thread(void *data)
{
	struct task_exit_notify_pidns_context *context = data;

	context->thread_shared_signal = current->signal == context->signal;
	complete(&context->thread_started);
	wait_for_completion(&context->exit);
	wait_for_completion(&context->thread_stop);
	complete(&context->thread_done);
	kthread_exit(0);
}

static int task_exit_notify_test_pidns_init(void *data)
{
	struct task_exit_notify_pidns_context *context = data;

	context->signal = current->signal;
	context->init_is_child_reaper =
		task_active_pid_ns(current)->child_reaper == current;
	context->thread_pid =
		kernel_thread(task_exit_notify_test_pidns_thread, context,
			      "task-exit-pidns-t",
			      CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
				      CLONE_FILES | SIGCHLD);
	complete(&context->init_started);
	wait_for_completion(&context->exit);
	kthread_exit(0);
}

static void task_exit_notify_test_pidns(struct kunit *test)
{
	struct task_exit_notify_pidns_context context;
	u32 child_reaper_count;
	u32 pid_ns_zap_count;
	pid_t init_pid;
	long reaped = 0;
	int round;

	init_completion(&context.init_started);
	init_completion(&context.thread_started);
	init_completion(&context.exit);
	init_completion(&context.thread_stop);
	init_completion(&context.thread_done);

	child_reaper_count = rust_task_exit_notify_test_child_reaper_count();
	pid_ns_zap_count = rust_task_exit_notify_test_pid_ns_zap_count();
	init_pid = kernel_thread(task_exit_notify_test_pidns_init, &context,
				 "task-exit-pidns-i",
				 CLONE_NEWPID | CLONE_FS | CLONE_FILES | SIGCHLD);
	KUNIT_ASSERT_GT(test, init_pid, 0);

	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.init_started, HZ));
	KUNIT_EXPECT_GT(test, context.thread_pid, 0);
	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.thread_started, HZ));
	KUNIT_EXPECT_TRUE(test, context.init_is_child_reaper);
	KUNIT_EXPECT_TRUE(test, context.thread_shared_signal);

	complete_all(&context.exit);
	for (round = 0; round < 2 * HZ; round++) {
		if (rust_task_exit_notify_test_child_reaper_count() >
		    child_reaper_count)
			break;
		schedule_timeout_interruptible(1);
	}
	KUNIT_EXPECT_GT(test,
			rust_task_exit_notify_test_child_reaper_count(),
			child_reaper_count);

	complete(&context.thread_stop);
	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.thread_done, HZ));

	for (round = 0; round < 2 * HZ; round++) {
		reaped = kernel_wait4(init_pid, NULL, WNOHANG, NULL);
		if (reaped == init_pid)
			break;
		if (reaped == -ECHILD)
			break;
		if (reaped == -EINTR)
			reaped = 0;
		schedule_timeout_interruptible(1);
	}
	KUNIT_EXPECT_TRUE(test, reaped == init_pid || reaped == -ECHILD);
	KUNIT_EXPECT_GT(test, rust_task_exit_notify_test_pid_ns_zap_count(),
			pid_ns_zap_count);
}
#endif

#ifdef CONFIG_RUST_TASK_EXIT_NOTIFY
static void task_exit_notify_test_policy(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_TASK_EXIT_NOTIFY_PTRACE,
			rust_task_exit_notify_test_autoreap_mode(true, true));
	KUNIT_EXPECT_EQ(test, RUST_TASK_EXIT_NOTIFY_PTRACE,
			rust_task_exit_notify_test_autoreap_mode(true, false));
	KUNIT_EXPECT_EQ(test, RUST_TASK_EXIT_NOTIFY_LEADER,
			rust_task_exit_notify_test_autoreap_mode(false, true));
	KUNIT_EXPECT_EQ(test, RUST_TASK_EXIT_NOTIFY_THREAD,
			rust_task_exit_notify_test_autoreap_mode(false, false));

	KUNIT_EXPECT_EQ(test, 7,
			rust_task_exit_notify_test_ptrace_signal(true, false, 7));
	KUNIT_EXPECT_EQ(test, SIGCHLD,
			rust_task_exit_notify_test_ptrace_signal(false, false, 7));
	KUNIT_EXPECT_EQ(test, SIGCHLD,
			rust_task_exit_notify_test_ptrace_signal(true, true, 7));
}
#endif

static struct kunit_case task_exit_notify_test_cases[] = {
	KUNIT_CASE(task_exit_notify_test_kthreads),
	KUNIT_CASE(task_exit_notify_test_reparent),
#if defined(CONFIG_RUST_TASK_EXIT_NOTIFY) && defined(CONFIG_PID_NS)
	KUNIT_CASE(task_exit_notify_test_pidns),
#endif
#ifdef CONFIG_RUST_TASK_EXIT_NOTIFY
	KUNIT_CASE(task_exit_notify_test_policy),
#endif
	{}
};

static struct kunit_suite task_exit_notify_test_suite = {
	.name = "task_exit_notify",
	.test_cases = task_exit_notify_test_cases,
};

kunit_test_suite(task_exit_notify_test_suite);

MODULE_DESCRIPTION("KUnit tests for task exit parent notification");
MODULE_LICENSE("GPL");
