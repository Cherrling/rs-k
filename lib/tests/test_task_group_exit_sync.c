// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/task-group-exit-sync-rust.h>
#include <uapi/linux/wait.h>

#define TASK_GROUP_EXIT_SYNC_TEST_ROUNDS 16

static int task_group_exit_sync_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void task_group_exit_sync_test_kthreads(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;
#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC
	u32 entry_count = 0;
	u32 group_exit_count = 0;
#endif
	int round;

#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC
	entry_count = rust_task_group_exit_sync_test_entry_count();
	group_exit_count = rust_task_group_exit_sync_test_group_exit_count();
#endif

	for (round = 0; round < TASK_GROUP_EXIT_SYNC_TEST_ROUNDS; round++) {
		init_completion(&started);
		task = kthread_run(task_group_exit_sync_test_thread, &started,
				   "task-group-exit-%d", round);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
		KUNIT_ASSERT_NE(test, 0,
				wait_for_completion_timeout(&started, HZ));
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();
#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC
	KUNIT_EXPECT_GT(test, rust_task_group_exit_sync_test_entry_count(),
			entry_count);
	KUNIT_EXPECT_GT(test,
			rust_task_group_exit_sync_test_group_exit_count(),
			group_exit_count);
#endif
}

#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC
struct task_group_exit_sync_group_context {
	struct completion leader_started;
	struct completion worker_started;
	struct completion leader_exit;
	struct completion worker_exit;
	pid_t worker_pid;
};

static int task_group_exit_sync_test_group_worker(void *data)
{
	struct task_group_exit_sync_group_context *context = data;

	complete(&context->worker_started);
	wait_for_completion(&context->worker_exit);
	kthread_exit(0);
}

static int task_group_exit_sync_test_group_leader(void *data)
{
	struct task_group_exit_sync_group_context *context = data;

	context->worker_pid =
		kernel_thread(task_group_exit_sync_test_group_worker, context,
			      "exit-sync-worker",
			      CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
				      CLONE_FILES | SIGCHLD);
	complete(&context->leader_started);
	wait_for_completion(&context->leader_exit);
	kthread_exit(RUST_TASK_GROUP_EXIT_SYNC_KUNIT_CODE);
}

static void task_group_exit_sync_test_group(struct kunit *test)
{
	struct task_group_exit_sync_group_context context;
	u32 entry_count;
	u32 group_exit_count;
	u32 pending_count;
	u32 post_coredump_count;
	pid_t leader_pid;
	long reaped = 0;
	int round;

	init_completion(&context.leader_started);
	init_completion(&context.worker_started);
	init_completion(&context.leader_exit);
	init_completion(&context.worker_exit);

	entry_count = rust_task_group_exit_sync_test_entry_count();
	group_exit_count = rust_task_group_exit_sync_test_group_exit_count();
	pending_count = rust_task_group_exit_sync_test_pending_count();
	post_coredump_count =
		rust_task_group_exit_sync_test_post_coredump_count();

	leader_pid = kernel_thread(task_group_exit_sync_test_group_leader,
				   &context, "exit-sync-leader",
				   CLONE_FS | CLONE_FILES | SIGCHLD);
	KUNIT_ASSERT_GT(test, leader_pid, 0);

	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.leader_started, HZ));
	KUNIT_EXPECT_GT(test, context.worker_pid, 0);
	KUNIT_EXPECT_NE(test, 0,
			wait_for_completion_timeout(&context.worker_started, HZ));

	complete(&context.worker_exit);
	for (round = 0; round < 2 * HZ; round++) {
		if (rust_task_group_exit_sync_test_pending_count() > pending_count)
			break;
		schedule_timeout_interruptible(1);
	}
	KUNIT_EXPECT_GT(test, rust_task_group_exit_sync_test_pending_count(),
			pending_count);

	complete(&context.leader_exit);
	for (round = 0; round < 2 * HZ; round++) {
		if (rust_task_group_exit_sync_test_entry_count() >=
			    entry_count + 2 &&
		    rust_task_group_exit_sync_test_group_exit_count() >=
			    group_exit_count + 1 &&
		    rust_task_group_exit_sync_test_last_group_exit_code() ==
			    RUST_TASK_GROUP_EXIT_SYNC_KUNIT_CODE)
			break;
		schedule_timeout_interruptible(1);
	}

	KUNIT_EXPECT_GE(test, rust_task_group_exit_sync_test_entry_count(),
			entry_count + 2);
	KUNIT_EXPECT_GE(test,
			rust_task_group_exit_sync_test_group_exit_count(),
			group_exit_count + 1);

	KUNIT_EXPECT_EQ(test, RUST_TASK_GROUP_EXIT_SYNC_KUNIT_CODE,
			rust_task_group_exit_sync_test_last_group_exit_code());
	KUNIT_EXPECT_NE(test, 0,
			rust_task_group_exit_sync_test_last_group_exit_flags() &
				SIGNAL_GROUP_EXIT);
	KUNIT_EXPECT_EQ(test, 0,
			rust_task_group_exit_sync_test_last_group_stop_count());
	KUNIT_EXPECT_GE(test,
			rust_task_group_exit_sync_test_post_coredump_count(),
			post_coredump_count + 2);

	for (round = 0; round < 2 * HZ; round++) {
		reaped = kernel_wait4(leader_pid, NULL, WNOHANG, NULL);
		if (reaped == leader_pid || reaped == -ECHILD)
			break;
		if (reaped == -EINTR)
			reaped = 0;
		schedule_timeout_interruptible(1);
	}
	KUNIT_EXPECT_TRUE(test, reaped == leader_pid || reaped == -ECHILD);
	rcu_barrier();
}

static void task_group_exit_sync_test_policy(struct kunit *test)
{
	KUNIT_EXPECT_TRUE(test,
			  rust_task_group_exit_sync_test_should_start(0, false));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_group_exit_sync_test_should_start(0, true));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_group_exit_sync_test_should_start(1, false));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_group_exit_sync_test_should_start(-1, false));
	KUNIT_EXPECT_TRUE(test,
			  rust_task_group_exit_sync_test_should_enter_coredump(true));
	KUNIT_EXPECT_FALSE(test,
			   rust_task_group_exit_sync_test_should_enter_coredump(false));
}
#endif

static struct kunit_case task_group_exit_sync_test_cases[] = {
	KUNIT_CASE(task_group_exit_sync_test_kthreads),
#ifdef CONFIG_RUST_TASK_GROUP_EXIT_SYNC
	KUNIT_CASE(task_group_exit_sync_test_group),
	KUNIT_CASE(task_group_exit_sync_test_policy),
#endif
	{}
};

static struct kunit_suite task_group_exit_sync_test_suite = {
	.name = "task_group_exit_sync",
	.test_cases = task_group_exit_sync_test_cases,
};

kunit_test_suite(task_group_exit_sync_test_suite);

MODULE_DESCRIPTION("KUnit tests for task thread-group exit synchronization");
MODULE_LICENSE("GPL");
