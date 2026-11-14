// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/complete-signal-rust.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <uapi/linux/wait.h>

#define COMPLETE_SIGNAL_TEST_WORKERS 3

struct complete_signal_test_context;

struct complete_signal_test_worker {
	struct complete_signal_test_context *context;
	unsigned int index;
};

struct complete_signal_test_context {
	struct completion leader_started;
	struct completion worker_started[COMPLETE_SIGNAL_TEST_WORKERS];
	struct completion worker_done[COMPLETE_SIGNAL_TEST_WORKERS];
	struct completion release_workers;
	struct complete_signal_test_worker worker[COMPLETE_SIGNAL_TEST_WORKERS];
	struct task_struct *leader;
	struct task_struct *worker_task[COMPLETE_SIGNAL_TEST_WORKERS];
};

static int complete_signal_test_worker_fn(void *data)
{
	struct complete_signal_test_worker *worker = data;
	struct complete_signal_test_context *context = worker->context;

	context->worker_task[worker->index] = get_task_struct(current);
	complete(&context->worker_started[worker->index]);
	wait_for_completion(&context->release_workers);
	complete(&context->worker_done[worker->index]);
	kthread_exit(0);
}

static int complete_signal_test_leader(void *data)
{
	struct complete_signal_test_context *context = data;
	unsigned int index;

	context->leader = get_task_struct(current);
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		context->worker[index].context = context;
		context->worker[index].index = index;
		if (kernel_thread(complete_signal_test_worker_fn,
				  &context->worker[index], "complete-signal-w",
				  CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
					  CLONE_FILES | SIGCHLD) <= 0)
			complete(&context->worker_started[index]);
	}
	complete(&context->leader_started);
	wait_for_completion(&context->release_workers);
	kthread_exit(0);
}

static void complete_signal_reset_task(struct task_struct *task)
{
	sigemptyset(&task->blocked);
	sigemptyset(&task->real_blocked);
	sigemptyset(&task->pending.signal);
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
	task->flags &= ~PF_EXITING;
	task->jobctl = 0;
}

static void complete_signal_reset_state(struct complete_signal_test_context *context)
{
	struct signal_struct *signal;
	unsigned int index;

	spin_lock_irq(&context->leader->sighand->siglock);
	signal = context->leader->signal;
	sigemptyset(&signal->shared_pending.signal);
	signal->flags = 0;
	signal->group_exit_code = 0;
	signal->group_stop_count = 0;
	signal->curr_target = context->leader;
	context->leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_IGN;
	context->leader->sighand->action[SIGTERM - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGSEGV - 1].sa.sa_handler = SIG_DFL;

	complete_signal_reset_task(context->leader);
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++)
		complete_signal_reset_task(context->worker_task[index]);
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static void complete_signal_prepare(struct complete_signal_test_context *context,
				    int sig)
{
	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&context->leader->signal->shared_pending.signal, sig);
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static bool complete_signal_task_pending(struct task_struct *task)
{
	bool pending;

	spin_lock_irq(&task->sighand->siglock);
	pending = task_sigpending(task);
	spin_unlock_irq(&task->sighand->siglock);

	return pending;
}

static bool complete_signal_worker_pending(struct complete_signal_test_context *context,
					   unsigned int worker)
{
	return complete_signal_task_pending(context->worker_task[worker]);
}

static bool complete_signal_all_workers_clear(struct complete_signal_test_context *context)
{
	unsigned int index;

	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		if (complete_signal_worker_pending(context, index))
			return false;
	}

	return true;
}

static void complete_signal_test_teardown(struct kunit *test,
					  struct complete_signal_test_context *context,
					  pid_t leader_pid)
{
	long reaped = 0;
	unsigned int index;
	int round;

	complete_all(&context->release_workers);
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
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

	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			put_task_struct(context->worker_task[index]);
	}
	if (context->leader)
		put_task_struct(context->leader);
}

static void complete_signal_test_group(struct kunit *test)
{
	struct complete_signal_test_context context;
	struct signal_struct *signal;
	struct task_struct *leader;
	pid_t leader_pid;
	unsigned int index;
	bool all_pending;
	bool ready = true;
	bool shared_pending;
	int group_exit_code;
	int group_stop_count;
	unsigned int group_flags;

	memset(&context, 0, sizeof(context));
	init_completion(&context.leader_started);
	init_completion(&context.release_workers);
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		init_completion(&context.worker_started[index]);
		init_completion(&context.worker_done[index]);
	}

	leader_pid = kernel_thread(complete_signal_test_leader, &context,
				   "complete-signal-l",
				   CLONE_FS | CLONE_FILES | SIGCHLD);
	if (leader_pid <= 0 ||
	    !wait_for_completion_timeout(&context.leader_started, HZ)) {
		KUNIT_FAIL(test, "leader did not start");
		if (leader_pid > 0)
			complete_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&context.worker_started[index],
						 HZ) ||
		    !context.worker_task[index]) {
			KUNIT_FAIL(test, "worker %u did not start", index);
			ready = false;
			break;
		}
	}
	if (!ready) {
		complete_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	leader = context.leader;
	signal = leader->signal;

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGUSR1);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGUSR1, leader, PIDTYPE_TGID);
	KUNIT_EXPECT_TRUE(test, complete_signal_task_pending(leader));
	KUNIT_EXPECT_TRUE(test, complete_signal_all_workers_clear(&context));
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_suggested_count());
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_final_wake_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGUSR1);
	spin_lock_irq(&leader->sighand->siglock);
	sigaddset(&leader->blocked, SIGUSR1);
	spin_unlock_irq(&leader->sighand->siglock);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGUSR1, leader, PIDTYPE_PID);
	KUNIT_EXPECT_FALSE(test, complete_signal_task_pending(leader));
	KUNIT_EXPECT_TRUE(test, complete_signal_all_workers_clear(&context));
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_single_return_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGUSR1);
	spin_lock_irq(&leader->sighand->siglock);
	sigaddset(&leader->blocked, SIGUSR1);
	sigaddset(&context.worker_task[0]->blocked, SIGUSR1);
	sigaddset(&context.worker_task[1]->blocked, SIGUSR1);
	signal->curr_target = context.worker_task[0];
	spin_unlock_irq(&leader->sighand->siglock);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGUSR1, leader, PIDTYPE_TGID);
	KUNIT_EXPECT_TRUE(test, complete_signal_worker_pending(&context, 2));
	KUNIT_EXPECT_PTR_EQ(test, context.worker_task[2], signal->curr_target);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_scan_count());
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_target_update_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGUSR1);
	spin_lock_irq(&leader->sighand->siglock);
	sigaddset(&leader->blocked, SIGUSR1);
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++)
		sigaddset(&context.worker_task[index]->blocked, SIGUSR1);
	signal->curr_target = context.worker_task[0];
	spin_unlock_irq(&leader->sighand->siglock);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGUSR1, leader, PIDTYPE_TGID);
	KUNIT_EXPECT_FALSE(test, complete_signal_task_pending(leader));
	KUNIT_EXPECT_TRUE(test, complete_signal_all_workers_clear(&context));
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_no_candidate_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGTERM);
	spin_lock_irq(&leader->sighand->siglock);
	leader->jobctl = JOBCTL_STOP_PENDING;
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++)
		context.worker_task[index]->jobctl = JOBCTL_STOP_PENDING;
	spin_unlock_irq(&leader->sighand->siglock);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGTERM, leader, PIDTYPE_TGID);

	spin_lock_irq(&leader->sighand->siglock);
	group_flags = signal->flags;
	group_exit_code = signal->group_exit_code;
	group_stop_count = signal->group_stop_count;
	shared_pending = sigismember(&signal->shared_pending.signal, SIGTERM);
	all_pending = task_sigpending(leader) &&
		      sigismember(&leader->pending.signal, SIGKILL) &&
		      !leader->jobctl;
	for (index = 0; index < COMPLETE_SIGNAL_TEST_WORKERS; index++) {
		struct task_struct *worker = context.worker_task[index];

		all_pending = all_pending && task_sigpending(worker) &&
			      sigismember(&worker->pending.signal, SIGKILL) &&
			      !worker->jobctl;
	}
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, SIGNAL_GROUP_EXIT, group_flags);
	KUNIT_EXPECT_EQ(test, SIGTERM, group_exit_code);
	KUNIT_EXPECT_EQ(test, 0, group_stop_count);
	KUNIT_EXPECT_TRUE(test, all_pending);
	KUNIT_EXPECT_TRUE(test, shared_pending);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_group_exit_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGSEGV);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGSEGV, leader, PIDTYPE_TGID);
	spin_lock_irq(&leader->sighand->siglock);
	group_flags = signal->flags;
	all_pending = task_sigpending(leader);
	spin_unlock_irq(&leader->sighand->siglock);
	KUNIT_EXPECT_EQ(test, 0, group_flags);
	KUNIT_EXPECT_TRUE(test, all_pending);
	KUNIT_EXPECT_TRUE(test, complete_signal_all_workers_clear(&context));
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_coredump_skip_count());
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_final_wake_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_prepare(&context, SIGTERM);
	spin_lock_irq(&leader->sighand->siglock);
	sigaddset(&leader->real_blocked, SIGTERM);
	spin_unlock_irq(&leader->sighand->siglock);
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	rust_complete_signal_test_reset();
#endif
	complete_signal_test_call(SIGTERM, leader, PIDTYPE_TGID);
	spin_lock_irq(&leader->sighand->siglock);
	group_flags = signal->flags;
	all_pending = task_sigpending(leader);
	spin_unlock_irq(&leader->sighand->siglock);
	KUNIT_EXPECT_EQ(test, 0, group_flags);
	KUNIT_EXPECT_TRUE(test, all_pending);
	KUNIT_EXPECT_TRUE(test, complete_signal_all_workers_clear(&context));
#ifdef CONFIG_RUST_COMPLETE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_complete_signal_test_final_wake_count());
#endif

	complete_signal_reset_state(&context);
	complete_signal_test_teardown(test, &context, leader_pid);
}

static struct kunit_case complete_signal_test_cases[] = {
	KUNIT_CASE(complete_signal_test_group),
	{}
};

static struct kunit_suite complete_signal_test_suite = {
	.name = "complete_signal",
	.test_cases = complete_signal_test_cases,
};

kunit_test_suite(complete_signal_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal completion");
MODULE_LICENSE("GPL");
