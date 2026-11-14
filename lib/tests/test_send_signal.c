// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/send-signal-rust.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/wait.h>
#include <uapi/linux/wait.h>

#define SEND_SIGNAL_TEST_WORKERS 2

struct send_signal_test_context;

struct send_signal_test_worker {
	struct send_signal_test_context *context;
	unsigned int index;
};

struct send_signal_test_context {
	struct completion leader_started;
	struct completion worker_started[SEND_SIGNAL_TEST_WORKERS];
	struct completion worker_done[SEND_SIGNAL_TEST_WORKERS];
	struct completion release_workers;
	struct send_signal_test_worker worker[SEND_SIGNAL_TEST_WORKERS];
	struct task_struct *leader;
	struct task_struct *worker_task[SEND_SIGNAL_TEST_WORKERS];
};

static int send_signal_test_worker_fn(void *data)
{
	struct send_signal_test_worker *worker = data;
	struct send_signal_test_context *context = worker->context;

	context->worker_task[worker->index] = get_task_struct(current);
	complete(&context->worker_started[worker->index]);
	wait_for_completion(&context->release_workers);
	complete(&context->worker_done[worker->index]);
	kthread_exit(0);
}

static int send_signal_test_leader(void *data)
{
	struct send_signal_test_context *context = data;
	unsigned int index;

	context->leader = get_task_struct(current);
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		context->worker[index].context = context;
		context->worker[index].index = index;
		if (kernel_thread(send_signal_test_worker_fn,
				  &context->worker[index], "send-signal-w",
				  CLONE_THREAD | CLONE_SIGHAND | CLONE_FS |
					  CLONE_FILES | SIGCHLD) <= 0)
			complete(&context->worker_started[index]);
	}
	complete(&context->leader_started);
	wait_for_completion(&context->release_workers);
	kthread_exit(0);
}

static void send_signal_test_reset(struct kunit *test,
				   struct send_signal_test_context *context)
{
	struct signal_struct *signal = context->leader->signal;
	unsigned int index;

	flush_signals(context->leader);
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			flush_signals(context->worker_task[index]);
	}

	spin_lock_irq(&context->leader->sighand->siglock);
	signal->flags = 0;
	signal->group_exit_code = 0;
	signal->group_stop_count = 0;
	signal->curr_target = context->leader;
	context->leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGTERM - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGCONT - 1].sa.sa_handler = SIG_DFL;
	context->leader->sighand->action[SIGRTMIN - 1].sa.sa_handler = SIG_DFL;
	sigemptyset(&context->leader->blocked);
	sigemptyset(&context->leader->real_blocked);
	context->leader->flags &= ~PF_EXITING;
	context->leader->flags |= PF_KTHREAD;
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		struct task_struct *worker = context->worker_task[index];

		sigemptyset(&worker->blocked);
		sigemptyset(&worker->real_blocked);
		worker->flags &= ~PF_EXITING;
	}
	spin_unlock_irq(&context->leader->sighand->siglock);

#ifdef CONFIG_RUST_SEND_SIGNAL
	rust_send_signal_test_reset();
#endif
}

static void send_signal_test_block_all(struct send_signal_test_context *context,
				       int sig)
{
	unsigned int index;

	spin_lock_irq(&context->leader->sighand->siglock);
	sigaddset(&context->leader->blocked, sig);
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++)
		sigaddset(&context->worker_task[index]->blocked, sig);
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static void send_signal_test_set_kthread(struct send_signal_test_context *context,
					 bool enabled)
{
	spin_lock_irq(&context->leader->sighand->siglock);
	if (enabled)
		context->leader->flags |= PF_KTHREAD;
	else
		context->leader->flags &= ~PF_KTHREAD;
	spin_unlock_irq(&context->leader->sighand->siglock);
}

static unsigned int send_signal_test_queue_count(struct sigpending *pending)
{
	struct sigqueue *queue;
	unsigned int count = 0;

	list_for_each_entry(queue, &pending->list, list)
		count++;

	return count;
}

static struct sigqueue *send_signal_test_first_queue(struct sigpending *pending)
{
	if (list_empty(&pending->list))
		return NULL;

	return list_first_entry(&pending->list, struct sigqueue, list);
}

static void send_signal_test_exit_drop(struct kunit *test,
				       struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	bool pending;
	unsigned int queues;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGUSR1);

	spin_lock_irq(&leader->sighand->siglock);
	leader->flags |= PF_EXITING;
	spin_unlock_irq(&leader->sighand->siglock);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_PRIV, leader,
				    PIDTYPE_PID, true);

	spin_lock_irq(&leader->sighand->siglock);
	pending = sigismember(&leader->pending.signal, SIGUSR1);
	queues = send_signal_test_queue_count(&leader->pending);
	leader->flags &= ~PF_EXITING;
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_FALSE(test, pending);
	KUNIT_EXPECT_EQ(test, 0, queues);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_exit_drop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_trace_count());
#endif
}

static void send_signal_test_prepare_drop(struct kunit *test,
					  struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	bool pending;
	unsigned int queues;
	int ret;

	send_signal_test_reset(test, context);
	spin_lock_irq(&leader->sighand->siglock);
	leader->sighand->action[SIGUSR1 - 1].sa.sa_handler = SIG_IGN;
	spin_unlock_irq(&leader->sighand->siglock);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PID, false);

	spin_lock_irq(&leader->sighand->siglock);
	pending = sigismember(&leader->pending.signal, SIGUSR1);
	queues = send_signal_test_queue_count(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_FALSE(test, pending);
	KUNIT_EXPECT_EQ(test, 0, queues);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_prepare_drop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_trace_count());
#endif
}

static void send_signal_test_noinfo_queue(struct kunit *test,
					  struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	struct sigqueue *queue;
	bool pending;
	unsigned int queues;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGUSR1);
	send_signal_test_set_kthread(context, false);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PID, false);

	spin_lock_irq(&leader->sighand->siglock);
	pending = sigismember(&leader->pending.signal, SIGUSR1);
	queues = send_signal_test_queue_count(&leader->pending);
	queue = send_signal_test_first_queue(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_TRUE(test, pending);
	KUNIT_EXPECT_EQ(test, 1, queues);
	KUNIT_ASSERT_NOT_NULL(test, queue);
	KUNIT_EXPECT_EQ(test, SIGUSR1, queue->info.si_signo);
	KUNIT_EXPECT_EQ(test, SI_USER, queue->info.si_code);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PID, false);

	spin_lock_irq(&leader->sighand->siglock);
	queues = send_signal_test_queue_count(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 1, queues);
	send_signal_test_set_kthread(context, true);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 2, rust_send_signal_test_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_legacy_drop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_pending_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_complete_count());
	KUNIT_EXPECT_EQ(test, 2, rust_send_signal_test_trace_count());
#endif
}

static void send_signal_test_private_info(struct kunit *test,
					  struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	struct sigqueue *queue;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGUSR1);
	send_signal_test_set_kthread(context, false);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_PRIV, leader,
				    PIDTYPE_PID, true);

	spin_lock_irq(&leader->sighand->siglock);
	queue = send_signal_test_first_queue(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_ASSERT_NOT_NULL(test, queue);
	KUNIT_EXPECT_EQ(test, SIGUSR1, queue->info.si_signo);
	KUNIT_EXPECT_EQ(test, SI_KERNEL, queue->info.si_code);
	KUNIT_EXPECT_EQ(test, 0, queue->info.si_pid);
	KUNIT_EXPECT_EQ(test, 0, queue->info.si_uid);
	send_signal_test_set_kthread(context, true);
}

static void send_signal_test_realtime_queues(struct kunit *test,
					     struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	struct kernel_siginfo info;
	struct sigqueue *queue;
	unsigned int queues;
	unsigned int index = 0;
	int first_code = 0;
	int last_code = 0;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGRTMIN);
	send_signal_test_set_kthread(context, false);

	for (index = 0; index < 3; index++) {
		clear_siginfo(&info);
		info.si_signo = SIGRTMIN;
		info.si_errno = 0;
		info.si_code = SI_QUEUE;
		info.si_int = index;
		ret = send_signal_test_call(SIGRTMIN, &info, leader,
					    PIDTYPE_PID, false);
		KUNIT_EXPECT_EQ(test, 0, ret);
	}

	spin_lock_irq(&leader->sighand->siglock);
	queues = send_signal_test_queue_count(&leader->pending);
	index = 0;
	list_for_each_entry(queue, &leader->pending.list, list) {
		if (!index)
			first_code = queue->info.si_code;
		last_code = queue->info.si_code;
		index++;
	}
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 3, queues);
	KUNIT_EXPECT_EQ(test, SI_QUEUE, first_code);
	KUNIT_EXPECT_EQ(test, SI_QUEUE, last_code);
	send_signal_test_set_kthread(context, true);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 3, rust_send_signal_test_pending_count());
	KUNIT_EXPECT_EQ(test, 3, rust_send_signal_test_complete_count());
#endif
}

static void send_signal_test_skip_allocation(struct kunit *test,
					     struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	bool pending;
	unsigned int queues;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGKILL);

	ret = send_signal_test_call(SIGKILL, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PID, false);

	spin_lock_irq(&leader->sighand->siglock);
	pending = sigismember(&leader->pending.signal, SIGKILL);
	queues = send_signal_test_queue_count(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_TRUE(test, pending);
	KUNIT_EXPECT_EQ(test, 0, queues);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_alloc_skip_count());
#endif
}

static void send_signal_test_tgid_queue(struct kunit *test,
					struct send_signal_test_context *context)
{
	struct task_struct *leader = context->leader;
	struct signal_struct *signal = leader->signal;
	bool shared_pending;
	unsigned int local_queues;
	unsigned int shared_queues;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGUSR1);
	send_signal_test_set_kthread(context, false);

	ret = send_signal_test_call(SIGUSR1, SEND_SIG_NOINFO, leader,
				    PIDTYPE_TGID, false);

	spin_lock_irq(&leader->sighand->siglock);
	shared_pending = sigismember(&signal->shared_pending.signal, SIGUSR1);
	shared_queues = send_signal_test_queue_count(&signal->shared_pending);
	local_queues = send_signal_test_queue_count(&leader->pending);
	spin_unlock_irq(&leader->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_TRUE(test, shared_pending);
	KUNIT_EXPECT_EQ(test, 1, shared_queues);
	KUNIT_EXPECT_EQ(test, 0, local_queues);
	send_signal_test_set_kthread(context, true);
}

static void send_signal_test_multiprocess(struct kunit *test,
					  struct send_signal_test_context *context)
{
	struct multiprocess_signals delayed;
	struct task_struct *leader = context->leader;
	bool cont_removed;
	bool stop_added;
	bool stop_removed;
	bool cont_added;
	int ret;

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGSTOP);
	send_signal_test_block_all(context, SIGCONT);
	send_signal_test_set_kthread(context, false);

	sigemptyset(&delayed.signal);
	sigaddset(&delayed.signal, SIGCONT);
	spin_lock_irq(&leader->sighand->siglock);
	hlist_add_head(&delayed.node, &leader->signal->multiprocess);
	spin_unlock_irq(&leader->sighand->siglock);

	ret = send_signal_test_call(SIGSTOP, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PGID, false);
	KUNIT_EXPECT_EQ(test, 0, ret);

	spin_lock_irq(&leader->sighand->siglock);
	cont_removed = !sigismember(&delayed.signal, SIGCONT);
	stop_added = sigismember(&delayed.signal, SIGSTOP);
	spin_unlock_irq(&leader->sighand->siglock);
	KUNIT_EXPECT_TRUE(test, cont_removed);
	KUNIT_EXPECT_TRUE(test, stop_added);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 0, rust_send_signal_test_prepare_drop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_pending_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_complete_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_multiprocess_count());
#endif

	send_signal_test_reset(test, context);
	send_signal_test_block_all(context, SIGCONT);
	sigemptyset(&delayed.signal);
	sigaddset(&delayed.signal, SIGSTOP);
	spin_lock_irq(&leader->sighand->siglock);
	hlist_del_init(&delayed.node);
	hlist_add_head(&delayed.node, &leader->signal->multiprocess);
	spin_unlock_irq(&leader->sighand->siglock);

	ret = send_signal_test_call(SIGCONT, SEND_SIG_NOINFO, leader,
				    PIDTYPE_PGID, false);
	KUNIT_EXPECT_EQ(test, 0, ret);

	spin_lock_irq(&leader->sighand->siglock);
	stop_removed = !sigismember(&delayed.signal, SIGSTOP);
	cont_added = sigismember(&delayed.signal, SIGCONT);
	hlist_del_init(&delayed.node);
	spin_unlock_irq(&leader->sighand->siglock);
	KUNIT_EXPECT_TRUE(test, stop_removed);
	KUNIT_EXPECT_TRUE(test, cont_added);

	send_signal_test_set_kthread(context, true);
#ifdef CONFIG_RUST_SEND_SIGNAL
	KUNIT_EXPECT_EQ(test, 0, rust_send_signal_test_prepare_drop_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_pending_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_complete_count());
	KUNIT_EXPECT_EQ(test, 1, rust_send_signal_test_multiprocess_count());
#endif
}

static void send_signal_test_teardown(struct kunit *test,
				      struct send_signal_test_context *context,
				      pid_t leader_pid)
{
	long reaped = 0;
	unsigned int index;
	int round;

	complete_all(&context->release_workers);
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
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

	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		if (context->worker_task[index])
			put_task_struct(context->worker_task[index]);
	}
	if (context->leader)
		put_task_struct(context->leader);
}

static void send_signal_test_group(struct kunit *test)
{
	struct send_signal_test_context context;
	pid_t leader_pid;
	unsigned int index;
	bool ready = true;

	memset(&context, 0, sizeof(context));
	init_completion(&context.leader_started);
	init_completion(&context.release_workers);
	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		init_completion(&context.worker_started[index]);
		init_completion(&context.worker_done[index]);
	}

	leader_pid = kernel_thread(send_signal_test_leader, &context,
				   "send-signal-l",
				   CLONE_FS | CLONE_FILES | SIGCHLD);
	if (leader_pid <= 0 ||
	    !wait_for_completion_timeout(&context.leader_started, HZ)) {
		KUNIT_FAIL(test, "leader did not start");
		if (leader_pid > 0)
			send_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	for (index = 0; index < SEND_SIGNAL_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&context.worker_started[index],
						 HZ) ||
		    !context.worker_task[index]) {
			KUNIT_FAIL(test, "worker %u did not start", index);
			ready = false;
			break;
		}
	}
	if (!ready) {
		send_signal_test_teardown(test, &context, leader_pid);
		return;
	}

	send_signal_test_exit_drop(test, &context);
	send_signal_test_prepare_drop(test, &context);
	send_signal_test_noinfo_queue(test, &context);
	send_signal_test_private_info(test, &context);
	send_signal_test_realtime_queues(test, &context);
	send_signal_test_tgid_queue(test, &context);
	send_signal_test_multiprocess(test, &context);
	send_signal_test_skip_allocation(test, &context);

	send_signal_test_reset(test, &context);
	send_signal_test_teardown(test, &context, leader_pid);
}

static struct kunit_case send_signal_test_cases[] = {
	KUNIT_CASE(send_signal_test_group),
	{}
};

static struct kunit_suite send_signal_test_suite = {
	.name = "send_signal",
	.test_cases = send_signal_test_cases,
};

kunit_test_suite(send_signal_test_suite);

MODULE_DESCRIPTION("KUnit tests for locked signal queue insertion");
MODULE_LICENSE("GPL");
