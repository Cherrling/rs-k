// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/signal-retarget-rust.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <uapi/linux/wait.h>

#define SIGNAL_RETARGET_TEST_WORKERS 3

struct signal_retarget_context;

struct signal_retarget_test_worker {
	struct signal_retarget_context *context;
	unsigned int index;
};

struct signal_retarget_context {
	struct completion leader_started;
	struct completion worker_started[SIGNAL_RETARGET_TEST_WORKERS];
	struct completion worker_done[SIGNAL_RETARGET_TEST_WORKERS];
	struct completion release_workers;
	struct signal_retarget_test_worker worker[SIGNAL_RETARGET_TEST_WORKERS];
	struct task_struct *leader;
	struct task_struct *worker_task[SIGNAL_RETARGET_TEST_WORKERS];
};

static int signal_retarget_test_worker_fn(void *data)
{
	struct signal_retarget_test_worker *worker = data;
	struct signal_retarget_context *context = worker->context;

	context->worker_task[worker->index] = get_task_struct(current);
	complete(&context->worker_started[worker->index]);
	wait_for_completion(&context->release_workers);
	complete(&context->worker_done[worker->index]);
	kthread_exit(0);
}

static int signal_retarget_test_leader(void *data)
{
	struct signal_retarget_context *context = data;
	unsigned int index;

	context->leader = get_task_struct(current);
	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		context->worker[index].context = context;
		context->worker[index].index = index;
		if (kernel_thread(signal_retarget_test_worker_fn,
				  &context->worker[index], "signal-retarget-w",
				  CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
					  CLONE_FILES | SIGCHLD) <= 0)
			complete(&context->worker_started[index]);
	}
	complete(&context->leader_started);
	wait_for_completion(&context->release_workers);
	kthread_exit(0);
}

static void signal_retarget_reset_state(struct signal_retarget_context *context)
{
	struct task_struct *task;
	unsigned int index;

	spin_lock_irq(&context->leader->sighand->siglock);
	sigemptyset(&context->leader->signal->shared_pending.signal);
	context->leader->signal->flags &= ~SIGNAL_GROUP_EXIT;
	sigemptyset(&context->leader->blocked);
	clear_tsk_thread_flag(context->leader, TIF_SIGPENDING);
	context->leader->flags &= ~PF_EXITING;

	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		task = context->worker_task[index];
		sigemptyset(&task->blocked);
		clear_tsk_thread_flag(task, TIF_SIGPENDING);
		task->flags &= ~PF_EXITING;
	}
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static void signal_retarget_teardown(struct kunit *test,
				     struct signal_retarget_context *context,
				     pid_t leader_pid)
{
	long reaped = 0;
	unsigned int index;
	int round;

	complete_all(&context->release_workers);
	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		if (!context->worker_task[index])
			continue;
		KUNIT_EXPECT_NE(test, 0,
				wait_for_completion_timeout(&context->worker_done[index],
							    HZ));
	}

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

	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			put_task_struct(context->worker_task[index]);
	}
	if (context->leader)
		put_task_struct(context->leader);
}

static void signal_retarget_test_wake(struct kunit *test,
				      struct signal_retarget_context *context)
{
	struct task_struct *exiting = context->worker_task[0];
	struct task_struct *blocked = context->worker_task[1];
	struct task_struct *target = context->worker_task[2];
	bool blocked_pending;
	bool exiting_pending;
	bool target_pending;
	sigset_t which;

	signal_retarget_reset_state(context);
	sigemptyset(&which);
	sigaddset(&which, SIGWINCH);
	sigaddset(&which, SIGUSR1);

	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&context->leader->signal->shared_pending.signal, SIGWINCH);
	sigaddset(&context->leader->signal->shared_pending.signal, SIGUSR1);
	exiting->flags |= PF_EXITING;
	sigaddset(&blocked->blocked, SIGWINCH);
	sigaddset(&blocked->blocked, SIGUSR1);
	sigaddset(&target->blocked, SIGUSR1);
	spin_unlock_irq(&context->leader->sighand->siglock);

#ifdef CONFIG_RUST_SIGNAL_RETARGET
	rust_signal_retarget_test_reset();
#endif
	signal_retarget_test_call(context->leader, &which);

	spin_lock_irq(&context->leader->sighand->siglock);
	exiting_pending = task_sigpending(exiting);
	blocked_pending = task_sigpending(blocked);
	target_pending = task_sigpending(target);
	spin_unlock_irq(&context->leader->sighand->siglock);

	KUNIT_EXPECT_FALSE(test, exiting_pending);
	KUNIT_EXPECT_FALSE(test, blocked_pending);
	KUNIT_EXPECT_TRUE(test, target_pending);

#ifdef CONFIG_RUST_SIGNAL_RETARGET
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_entry_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_group_exit_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_empty_count());
	KUNIT_EXPECT_EQ(test, SIGNAL_RETARGET_TEST_WORKERS,
			rust_signal_retarget_test_thread_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_exiting_skip_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_blocked_skip_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_wake_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_complete_count());
#endif
	signal_retarget_reset_state(context);
}

static void signal_retarget_test_group_exit(struct kunit *test,
					    struct signal_retarget_context *context)
{
	bool pending = false;
	unsigned int index;
	sigset_t which;

	signal_retarget_reset_state(context);
	sigemptyset(&which);
	sigaddset(&which, SIGWINCH);

	spin_lock_irq(&context->leader->sighand->siglock);
	context->leader->signal->flags |= SIGNAL_GROUP_EXIT;
	sigaddset(&context->leader->signal->shared_pending.signal, SIGWINCH);
	spin_unlock_irq(&context->leader->sighand->siglock);

#ifdef CONFIG_RUST_SIGNAL_RETARGET
	rust_signal_retarget_test_reset();
#endif
	signal_retarget_test_call(context->leader, &which);

	spin_lock_irq(&context->leader->sighand->siglock);
	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++)
		pending |= task_sigpending(context->worker_task[index]);
	spin_unlock_irq(&context->leader->sighand->siglock);

	KUNIT_EXPECT_FALSE(test, pending);
#ifdef CONFIG_RUST_SIGNAL_RETARGET
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_group_exit_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_thread_count());
#endif
	signal_retarget_reset_state(context);
}

static void signal_retarget_test_empty(struct kunit *test,
				       struct signal_retarget_context *context)
{
	bool pending = false;
	unsigned int index;
	sigset_t which;

	signal_retarget_reset_state(context);
	sigemptyset(&which);
	sigaddset(&which, SIGWINCH);

#ifdef CONFIG_RUST_SIGNAL_RETARGET
	rust_signal_retarget_test_reset();
#endif
	signal_retarget_test_call(context->leader, &which);

	spin_lock_irq(&context->leader->sighand->siglock);
	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++)
		pending |= task_sigpending(context->worker_task[index]);
	spin_unlock_irq(&context->leader->sighand->siglock);

	KUNIT_EXPECT_FALSE(test, pending);
#ifdef CONFIG_RUST_SIGNAL_RETARGET
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_empty_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_thread_count());
#endif
	signal_retarget_reset_state(context);
}

static void signal_retarget_test_already_pending(struct kunit *test,
						 struct signal_retarget_context *context)
{
	struct task_struct *target = context->worker_task[2];
	bool target_pending;
	sigset_t which;

	signal_retarget_reset_state(context);
	sigemptyset(&which);
	sigaddset(&which, SIGWINCH);

	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&context->leader->signal->shared_pending.signal, SIGWINCH);
	sigaddset(&context->worker_task[0]->blocked, SIGWINCH);
	sigaddset(&context->worker_task[1]->blocked, SIGWINCH);
	set_tsk_thread_flag(target, TIF_SIGPENDING);
	spin_unlock_irq(&context->leader->sighand->siglock);

#ifdef CONFIG_RUST_SIGNAL_RETARGET
	rust_signal_retarget_test_reset();
#endif
	signal_retarget_test_call(context->leader, &which);

	spin_lock_irq(&context->leader->sighand->siglock);
	target_pending = task_sigpending(target);
	spin_unlock_irq(&context->leader->sighand->siglock);

	KUNIT_EXPECT_TRUE(test, target_pending);
#ifdef CONFIG_RUST_SIGNAL_RETARGET
	KUNIT_EXPECT_EQ(test, 1, rust_signal_retarget_test_entry_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_retarget_test_wake_count());
#endif
	signal_retarget_reset_state(context);
}

static void signal_retarget_test_group(struct kunit *test)
{
	struct signal_retarget_context context;
	pid_t leader_pid;
	unsigned int index;
	bool ready = true;

	memset(&context, 0, sizeof(context));
	init_completion(&context.leader_started);
	init_completion(&context.release_workers);
	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		init_completion(&context.worker_started[index]);
		init_completion(&context.worker_done[index]);
	}

	leader_pid = kernel_thread(signal_retarget_test_leader, &context,
				   "signal-retarget-l",
				   CLONE_FS | CLONE_FILES | SIGCHLD);
	KUNIT_ASSERT_GT(test, leader_pid, 0);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.leader_started, HZ));

	for (index = 0; index < SIGNAL_RETARGET_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&context.worker_started[index],
						 HZ) ||
		    !context.worker_task[index]) {
			KUNIT_FAIL(test, "worker %u did not start", index);
			ready = false;
			break;
		}
	}

	if (!ready) {
		signal_retarget_teardown(test, &context, leader_pid);
		return;
	}

	signal_retarget_test_wake(test, &context);
	signal_retarget_test_group_exit(test, &context);
	signal_retarget_test_empty(test, &context);
	signal_retarget_test_already_pending(test, &context);

	signal_retarget_teardown(test, &context, leader_pid);
}

static struct kunit_case signal_retarget_test_cases[] = {
	KUNIT_CASE(signal_retarget_test_group),
	{}
};

static struct kunit_suite signal_retarget_test_suite = {
	.name = "signal_retarget",
	.test_cases = signal_retarget_test_cases,
};

kunit_test_suite(signal_retarget_test_suite);

MODULE_DESCRIPTION("KUnit tests for shared pending signal retargeting");
MODULE_LICENSE("GPL");
