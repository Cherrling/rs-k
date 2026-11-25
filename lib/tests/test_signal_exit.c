// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/signal-exit-rust.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <uapi/linux/wait.h>

struct signal_exit_test_context {
	struct completion leader_started;
	struct completion worker_started;
	struct completion target_started;
	struct completion release_worker;
	struct completion release_target;
	struct completion target_observed;
	struct completion release_cleanup;
	struct completion target_done;
	struct completion release_leader;
	struct task_struct *leader;
	struct task_struct *worker;
	struct task_struct *target;
	int send_result;
	int target_result;
};

static int signal_exit_test_worker(void *data)
{
	struct signal_exit_test_context *context = data;
	sigset_t blocked;

	context->worker = get_task_struct(current);
	complete(&context->worker_started);
	wait_for_completion(&context->release_worker);

	sigemptyset(&blocked);
	sigaddset(&blocked, SIGUSR2);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = blocked;
	spin_unlock_irq(&current->sighand->siglock);
	context->send_result =
		send_sig_info(SIGUSR2, SEND_SIG_PRIV, current);

	spin_lock_irq(&current->sighand->siglock);
	sigaddset(&current->signal->shared_pending.signal, SIGUSR1);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	current->jobctl |= JOBCTL_STOP_PENDING | JOBCTL_STOP_CONSUME;
	current->signal->group_stop_count = 1;
	current->signal->flags &= ~SIGNAL_STOP_STOPPED;
	spin_unlock_irq(&current->sighand->siglock);

	kthread_exit(0);
}

static int signal_exit_test_target(void *data)
{
	struct signal_exit_test_context *context = data;

	context->target = get_task_struct(current);
	complete(&context->target_started);
	context->target_result =
		wait_for_completion_interruptible(&context->release_target);
	complete(&context->target_observed);
	wait_for_completion(&context->release_cleanup);
	complete(&context->target_done);
	kthread_exit(0);
}

static int signal_exit_test_leader(void *data)
{
	struct signal_exit_test_context *context = data;
	unsigned long flags = CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
			      CLONE_FILES | SIGCHLD;

	context->leader = get_task_struct(current);
	if (kernel_thread(signal_exit_test_worker, context,
			  "signal-exit-w", flags) <= 0)
		complete(&context->worker_started);
	if (kernel_thread(signal_exit_test_target, context,
			  "signal-exit-t", flags) <= 0)
		complete(&context->target_started);
	complete(&context->leader_started);
	wait_for_completion(&context->release_leader);
	kthread_exit(0);
}

static bool signal_exit_test_wait_task_dead(struct task_struct *task)
{
	unsigned int round;

	for (round = 0; round < 2 * HZ; round++) {
		if (READ_ONCE(task->exit_state) == EXIT_DEAD)
			return true;
		schedule_timeout_interruptible(1);
	}

	return false;
}

static void signal_exit_test_cleanup_group(struct signal_exit_test_context *context)
{
	if (!context->leader)
		return;

	spin_lock_irq(&context->leader->sighand->siglock);
	sigemptyset(&context->leader->signal->shared_pending.signal);
	context->leader->signal->flags &= ~SIGNAL_STOP_STOPPED;
	context->leader->signal->group_stop_count = 0;
	if (context->target)
		clear_tsk_thread_flag(context->target, TIF_SIGPENDING);
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static void signal_exit_test_teardown(struct kunit *test,
				      struct signal_exit_test_context *context,
				      pid_t leader_pid)
{
	long reaped = 0;
	int round;

	complete_all(&context->release_worker);
	complete_all(&context->release_target);
	complete_all(&context->release_cleanup);
	complete_all(&context->release_leader);

	if (context->worker)
		KUNIT_EXPECT_TRUE(test,
				  signal_exit_test_wait_task_dead(context->worker));
	if (context->target)
		KUNIT_EXPECT_NE(test, 0,
				wait_for_completion_timeout(&context->target_done,
							    2 * HZ));

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

#ifdef CONFIG_RUST_EXIT_SIGNALS
	rust_signal_exit_test_reset();
#endif
	if (context->worker)
		put_task_struct(context->worker);
	if (context->target)
		put_task_struct(context->target);
	if (context->leader)
		put_task_struct(context->leader);
}

static void signal_exit_test_group(struct kunit *test)
{
	struct signal_exit_test_context context;
	pid_t leader_pid;
	bool ready = true;
#ifdef CONFIG_RUST_EXIT_SIGNALS
	int round;
#endif

	memset(&context, 0, sizeof(context));
	init_completion(&context.leader_started);
	init_completion(&context.worker_started);
	init_completion(&context.target_started);
	init_completion(&context.release_worker);
	init_completion(&context.release_target);
	init_completion(&context.target_observed);
	init_completion(&context.release_cleanup);
	init_completion(&context.target_done);
	init_completion(&context.release_leader);

	leader_pid = kernel_thread(signal_exit_test_leader, &context,
				   "signal-exit-l", CLONE_FS | CLONE_FILES | SIGCHLD);
	KUNIT_ASSERT_GT(test, leader_pid, 0);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.leader_started, HZ));

	if (!wait_for_completion_timeout(&context.worker_started, HZ) ||
	    !context.worker) {
		KUNIT_FAIL(test, "worker did not start");
		ready = false;
	}
	if (!wait_for_completion_timeout(&context.target_started, HZ) ||
	    !context.target) {
		KUNIT_FAIL(test, "target did not start");
		ready = false;
	}
	if (!ready) {
		signal_exit_test_teardown(test, &context, leader_pid);
		return;
	}

#ifdef CONFIG_RUST_EXIT_SIGNALS
	rust_signal_exit_test_reset();
	rust_signal_exit_test_arm(context.worker);
#endif
	complete(&context.release_worker);

	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.target_observed,
						    2 * HZ));
	KUNIT_EXPECT_EQ(test, -ERESTARTSYS, context.target_result);
	KUNIT_EXPECT_GE(test, context.send_result, 0);
	KUNIT_ASSERT_TRUE(test,
			  signal_exit_test_wait_task_dead(context.worker));

#ifdef CONFIG_RUST_EXIT_SIGNALS
	for (round = 0; round < HZ; round++) {
		if (rust_signal_exit_test_notify_count() == 1)
			break;
		schedule_timeout_interruptible(1);
	}
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_exit_test_threadgroup_begin_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_exit_test_threadgroup_end_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_pf_exiting_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_dequeue_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_retarget_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_group_stop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_exit_test_notify_count());
#endif

	signal_exit_test_cleanup_group(&context);
	complete(&context.release_cleanup);
	signal_exit_test_teardown(test, &context, leader_pid);
}

static struct kunit_case signal_exit_test_cases[] = {
	KUNIT_CASE(signal_exit_test_group),
	{}
};

static struct kunit_suite signal_exit_test_suite = {
	.name = "signal_exit",
	.test_cases = signal_exit_test_cases,
};

kunit_test_suite(signal_exit_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal-aware task exit");
MODULE_LICENSE("GPL");
