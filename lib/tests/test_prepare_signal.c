// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/prepare-signal-rust.h>
#include <linux/ptrace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <uapi/linux/wait.h>

#define PREPARE_SIGNAL_TEST_WORKERS 2

struct prepare_signal_test_context;

struct prepare_signal_test_worker {
	struct prepare_signal_test_context *context;
	unsigned int index;
};

struct prepare_signal_test_context {
	struct completion leader_started;
	struct completion worker_started[PREPARE_SIGNAL_TEST_WORKERS];
	struct completion worker_done[PREPARE_SIGNAL_TEST_WORKERS];
	struct completion release_workers;
	struct prepare_signal_test_worker worker[PREPARE_SIGNAL_TEST_WORKERS];
	struct task_struct *leader;
	struct task_struct *worker_task[PREPARE_SIGNAL_TEST_WORKERS];
};

static int prepare_signal_test_worker_fn(void *data)
{
	struct prepare_signal_test_worker *worker = data;
	struct prepare_signal_test_context *context = worker->context;

	context->worker_task[worker->index] = get_task_struct(current);
	complete(&context->worker_started[worker->index]);
	wait_for_completion(&context->release_workers);
	complete(&context->worker_done[worker->index]);
	kthread_exit(0);
}

static int prepare_signal_test_leader(void *data)
{
	struct prepare_signal_test_context *context = data;
	unsigned int index;

	context->leader = get_task_struct(current);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		context->worker[index].context = context;
		context->worker[index].index = index;
		if (kernel_thread(prepare_signal_test_worker_fn,
				  &context->worker[index], "prepare-signal-w",
				  CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
					  CLONE_FILES | SIGCHLD) <= 0)
			complete(&context->worker_started[index]);
	}
	complete(&context->leader_started);
	wait_for_completion(&context->release_workers);
	kthread_exit(0);
}

static void prepare_signal_test_reset_task(struct task_struct *task)
{
	sigemptyset(&task->blocked);
	sigemptyset(&task->real_blocked);
	clear_tsk_thread_flag(task, TIF_SIGPENDING);
	task->jobctl = 0;
	task->ptrace = 0;
}

static void prepare_signal_test_reset(struct kunit *test,
				      struct prepare_signal_test_context *context)
{
	struct signal_struct *signal = context->leader->signal;
	unsigned int index;

	flush_signals(context->leader);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			flush_signals(context->worker_task[index]);
	}

	spin_lock_irq(&context->leader->sighand->siglock);
	signal->flags = 0;
	signal->core_state = NULL;
	signal->group_exit_code = 0;
	signal->group_stop_count = 0;
	signal->curr_target = context->leader;
	context->leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGCONT - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGSTOP - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGTSTP - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGTTIN - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGTTOU - 1].sa.sa_handler = SIG_DFL;
	prepare_signal_test_reset_task(context->leader);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++)
		prepare_signal_test_reset_task(context->worker_task[index]);
	spin_unlock_irq(&context->leader->sighand->siglock);

#ifdef CONFIG_RUST_PREPARE_SIGNAL
	rust_prepare_signal_test_reset();
#endif
}

static void prepare_signal_test_set_kthread(struct prepare_signal_test_context *context,
					    bool enabled)
{
	spin_lock_irq(&context->leader->sighand->siglock);
	if (enabled)
		context->leader->flags |= PF_KTHREAD;
	else
		context->leader->flags &= ~PF_KTHREAD;
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static void prepare_signal_test_pending(struct prepare_signal_test_context *context,
					int sig)
{
	struct signal_struct *signal = context->leader->signal;
	unsigned int index;

	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&signal->shared_pending.signal, sig);
	sigaddset(&context->leader->pending.signal, sig);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++)
		sigaddset(&context->worker_task[index]->pending.signal, sig);
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static bool prepare_signal_test_pending_any(struct prepare_signal_test_context *context,
					    int sig)
{
	struct signal_struct *signal = context->leader->signal;
	bool pending;
	unsigned int index;

	spin_lock_irq(&context->leader->sighand->siglock);
	pending = sigismember(&signal->shared_pending.signal, sig) ||
		  sigismember(&context->leader->pending.signal, sig);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++)
		pending = pending ||
			  sigismember(&context->worker_task[index]->pending.signal,
				      sig);
	spin_unlock_irq(&context->leader->sighand->siglock);

	return pending;
}

static void prepare_signal_test_group_exit(struct kunit *test,
					   struct prepare_signal_test_context *context)
{
	struct core_state core_state;
	struct signal_struct *signal = context->leader->signal;
	bool ret;

	prepare_signal_test_reset(test, context);
	spin_lock_irq(&context->leader->sighand->siglock);
	signal->flags = SIGNAL_GROUP_EXIT;
	spin_unlock_irq(&context->leader->sighand->siglock);
	ret = prepare_signal_test_call(SIGUSR1, context->leader, false);
	KUNIT_EXPECT_FALSE(test, ret);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_prepare_signal_test_group_exit_drop_count());
#endif

	memset(&core_state, 0, sizeof(core_state));
	prepare_signal_test_reset(test, context);
	spin_lock_irq(&context->leader->sighand->siglock);
	signal->flags = SIGNAL_GROUP_EXIT;
	signal->core_state = &core_state;
	spin_unlock_irq(&context->leader->sighand->siglock);
	ret = prepare_signal_test_call(SIGUSR1, context->leader, false);
	KUNIT_EXPECT_FALSE(test, ret);
	ret = prepare_signal_test_call(SIGKILL, context->leader, false);
	KUNIT_EXPECT_TRUE(test, ret);
	spin_lock_irq(&context->leader->sighand->siglock);
	signal->core_state = NULL;
	spin_unlock_irq(&context->leader->sighand->siglock);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 2, rust_prepare_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_prepare_signal_test_group_exit_drop_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_prepare_signal_test_coredump_kill_count());
#endif
}

static void prepare_signal_test_stop(struct kunit *test,
				     struct prepare_signal_test_context *context)
{
	bool ret;

	prepare_signal_test_reset(test, context);
	prepare_signal_test_pending(context, SIGCONT);
	ret = prepare_signal_test_call(SIGSTOP, context->leader, true);
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_FALSE(test, prepare_signal_test_pending_any(context, SIGCONT));
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_kernel_stop_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_prepare_signal_test_ignore_decision_count());
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_deliver_count());
#endif
}

static void prepare_signal_test_set_stop_state(struct prepare_signal_test_context *context)
{
	static const int stop_signals[] = { SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU };
	struct signal_struct *signal = context->leader->signal;
	unsigned int index;
	unsigned int stop;

	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&context->leader->blocked, SIGCONT);
	for (stop = 0; stop < ARRAY_SIZE(stop_signals); stop++) {
		sigaddset(&signal->shared_pending.signal, stop_signals[stop]);
		sigaddset(&context->leader->pending.signal, stop_signals[stop]);
		for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++)
			sigaddset(&context->worker_task[index]->pending.signal,
				  stop_signals[stop]);
	}

	context->leader->jobctl = JOBCTL_STOP_PENDING | JOBCTL_STOP_CONSUME |
				  JOBCTL_STOP_DEQUEUED | JOBCTL_STOPPED;
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++)
		context->worker_task[index]->jobctl =
			JOBCTL_STOP_PENDING | JOBCTL_STOP_CONSUME |
			JOBCTL_STOP_DEQUEUED | JOBCTL_STOPPED;
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static bool prepare_signal_test_stop_state_clear(struct prepare_signal_test_context *context)
{
	static const int stop_signals[] = { SIGSTOP, SIGTSTP, SIGTTIN, SIGTTOU };
	struct signal_struct *signal = context->leader->signal;
	bool clear = true;
	unsigned int index;
	unsigned int stop;

	spin_lock_irq(&context->leader->sighand->siglock);
	for (stop = 0; stop < ARRAY_SIZE(stop_signals); stop++)
		clear = clear &&
			!sigismember(&signal->shared_pending.signal,
				      stop_signals[stop]) &&
			!sigismember(&context->leader->pending.signal,
				      stop_signals[stop]);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		for (stop = 0; stop < ARRAY_SIZE(stop_signals); stop++)
			clear = clear &&
				!sigismember(&context->worker_task[index]->pending.signal,
					      stop_signals[stop]);
		clear = clear && !(context->worker_task[index]->jobctl &
				  (JOBCTL_STOP_PENDING | JOBCTL_STOPPED));
	}
	clear = clear && !(context->leader->jobctl &
			  (JOBCTL_STOP_PENDING | JOBCTL_STOPPED));
	spin_unlock_irq(&context->leader->sighand->siglock);

	return clear;
}

static void prepare_signal_test_continue(struct kunit *test,
					 struct prepare_signal_test_context *context)
{
	struct signal_struct *signal = context->leader->signal;
	unsigned int flags;
	int group_exit_code;
	int group_stop_count;
	bool ret;

	prepare_signal_test_reset(test, context);
	prepare_signal_test_set_stop_state(context);
	spin_lock_irq(&context->leader->sighand->siglock);
	signal->flags = SIGNAL_STOP_STOPPED;
	signal->group_exit_code = SIGTERM;
	signal->group_stop_count = 3;
	spin_unlock_irq(&context->leader->sighand->siglock);

	ret = prepare_signal_test_call(SIGCONT, context->leader, false);
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test, prepare_signal_test_stop_state_clear(context));
	spin_lock_irq(&context->leader->sighand->siglock);
	flags = signal->flags;
	group_exit_code = signal->group_exit_code;
	group_stop_count = signal->group_stop_count;
	spin_unlock_irq(&context->leader->sighand->siglock);
	KUNIT_EXPECT_EQ(test, SIGNAL_STOP_CONTINUED | SIGNAL_CLD_CONTINUED, flags);
	KUNIT_EXPECT_EQ(test, 0, group_exit_code);
	KUNIT_EXPECT_EQ(test, 0, group_stop_count);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_continue_count());
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_stopped_notify_count());
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_deliver_count());
#endif

	prepare_signal_test_reset(test, context);
	prepare_signal_test_set_stop_state(context);
	spin_lock_irq(&context->leader->sighand->siglock);
	signal->group_stop_count = 2;
	spin_unlock_irq(&context->leader->sighand->siglock);
	ret = prepare_signal_test_call(SIGCONT, context->leader, false);
	KUNIT_EXPECT_TRUE(test, ret);
	spin_lock_irq(&context->leader->sighand->siglock);
	flags = signal->flags;
	spin_unlock_irq(&context->leader->sighand->siglock);
	KUNIT_EXPECT_EQ(test, SIGNAL_STOP_CONTINUED | SIGNAL_CLD_STOPPED, flags);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1,
			rust_prepare_signal_test_group_stop_notify_count());
#endif
}

static void prepare_signal_test_ptrace_continue(struct kunit *test,
						struct prepare_signal_test_context *context)
{
	struct task_struct *worker = context->worker_task[0];
	struct signal_struct *signal = context->leader->signal;
	bool ret;

	prepare_signal_test_reset(test, context);
	prepare_signal_test_set_stop_state(context);
	spin_lock_irq(&context->leader->sighand->siglock);
	worker->ptrace = PT_SEIZED;
	signal->flags = SIGNAL_STOP_STOPPED;
	spin_unlock_irq(&context->leader->sighand->siglock);

	ret = prepare_signal_test_call(SIGCONT, context->leader, false);
	KUNIT_EXPECT_TRUE(test, ret);
	spin_lock_irq(&context->leader->sighand->siglock);
	KUNIT_EXPECT_TRUE(test, !!(worker->jobctl & JOBCTL_TRAP_NOTIFY));
	spin_unlock_irq(&context->leader->sighand->siglock);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_seized_count());
#endif
}

static void prepare_signal_test_ignored(struct kunit *test,
					struct prepare_signal_test_context *context)
{
	bool ret;

	prepare_signal_test_reset(test, context);
	spin_lock_irq(&context->leader->sighand->siglock);
	context->leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_IGN;
	spin_unlock_irq(&context->leader->sighand->siglock);
	ret = prepare_signal_test_call(SIGUSR1, context->leader, false);
	KUNIT_EXPECT_FALSE(test, ret);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_ignore_drop_count());
#endif

	prepare_signal_test_reset(test, context);
	spin_lock_irq(&context->leader->sighand->siglock);
	context->leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_IGN;
	sigaddset(&context->leader->blocked, SIGUSR1);
	spin_unlock_irq(&context->leader->sighand->siglock);
	ret = prepare_signal_test_call(SIGUSR1, context->leader, false);
	KUNIT_EXPECT_TRUE(test, ret);
#ifdef CONFIG_RUST_PREPARE_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_prepare_signal_test_deliver_count());
#endif
}

static void prepare_signal_test_teardown(struct kunit *test,
					 struct prepare_signal_test_context *context,
					 pid_t leader_pid)
{
	long reaped = 0;
	unsigned int index;
	int round;

	complete_all(&context->release_workers);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
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

	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			put_task_struct(context->worker_task[index]);
	}
	if (context->leader)
		put_task_struct(context->leader);
}

static void prepare_signal_test_group(struct kunit *test)
{
	struct prepare_signal_test_context context;
	pid_t leader_pid;
	unsigned int index;
	bool ready = true;

	memset(&context, 0, sizeof(context));
	init_completion(&context.leader_started);
	init_completion(&context.release_workers);
	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		init_completion(&context.worker_started[index]);
		init_completion(&context.worker_done[index]);
	}

	leader_pid = kernel_thread(prepare_signal_test_leader, &context,
				   "prepare-signal-l",
				   CLONE_FS | CLONE_FILES | SIGCHLD);
	if (leader_pid <= 0 ||
	    !wait_for_completion_timeout(&context.leader_started, HZ)) {
		KUNIT_FAIL(test, "leader did not start");
		if (leader_pid > 0)
			prepare_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	for (index = 0; index < PREPARE_SIGNAL_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&context.worker_started[index],
						 HZ) ||
		    !context.worker_task[index]) {
			KUNIT_FAIL(test, "worker %u did not start", index);
			ready = false;
			break;
		}
	}
	if (!ready) {
		prepare_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	prepare_signal_test_set_kthread(&context, false);
	prepare_signal_test_group_exit(test, &context);
	prepare_signal_test_stop(test, &context);
	prepare_signal_test_continue(test, &context);
	prepare_signal_test_ptrace_continue(test, &context);
	prepare_signal_test_ignored(test, &context);

	prepare_signal_test_set_kthread(&context, true);
	prepare_signal_test_reset(test, &context);
	prepare_signal_test_teardown(test, &context, leader_pid);
}

static struct kunit_case prepare_signal_test_cases[] = {
	KUNIT_CASE(prepare_signal_test_group),
	{}
};

static struct kunit_suite prepare_signal_test_suite = {
	.name = "prepare_signal",
	.test_cases = prepare_signal_test_cases,
};

kunit_test_suite(prepare_signal_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal preparation");
MODULE_LICENSE("GPL");
