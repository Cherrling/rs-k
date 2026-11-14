// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/posix-timers.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/signal-dequeue-rust.h>
#include <linux/signal.h>
#include <linux/spinlock.h>
#include <linux/string.h>

struct signal_dequeue_test_saved {
	sigset_t blocked;
	sigset_t real_blocked;
	unsigned long jobctl;
};

static unsigned int signal_dequeue_test_queue_count(struct sigpending *pending)
{
	struct sigqueue *queue;
	unsigned int count = 0;

	list_for_each_entry(queue, &pending->list, list)
		count++;

	return count;
}

static void signal_dequeue_test_save(struct signal_dequeue_test_saved *saved)
{
	spin_lock_irq(&current->sighand->siglock);
	saved->blocked = current->blocked;
	saved->real_blocked = current->real_blocked;
	saved->jobctl = current->jobctl;
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_dequeue_test_reset(void)
{
	flush_signals(current);

	spin_lock_irq(&current->sighand->siglock);
	sigemptyset(&current->blocked);
	sigemptyset(&current->real_blocked);
	current->jobctl &= ~JOBCTL_STOP_DEQUEUED;
	spin_unlock_irq(&current->sighand->siglock);

#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	rust_signal_dequeue_test_reset();
#endif
}

static void signal_dequeue_test_restore(const struct signal_dequeue_test_saved *saved)
{
	flush_signals(current);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = saved->blocked;
	current->real_blocked = saved->real_blocked;
	current->jobctl = saved->jobctl;
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_dequeue_test_pending(struct sigpending *pending, int sig)
{
	spin_lock_irq(&current->sighand->siglock);
	sigaddset(&pending->signal, sig);
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_dequeue_test_next_signal(struct kunit *test)
{
	struct sigpending pending;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	memset(&pending, 0, sizeof(pending));
	sigemptyset(&mask);
	sigaddset(&pending.signal, SIGUSR1);
	sigaddset(&pending.signal, SIGSYS);

	ret = next_signal(&pending, &mask);
	KUNIT_EXPECT_EQ(test, SIGSYS, ret);

	sigdelset(&pending.signal, SIGSYS);
	ret = next_signal(&pending, &mask);
	KUNIT_EXPECT_EQ(test, SIGUSR1, ret);

	sigaddset(&mask, SIGUSR1);
	ret = next_signal(&pending, &mask);
	KUNIT_EXPECT_EQ(test, 0, ret);
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 3, rust_signal_dequeue_test_next_entry_count());
	KUNIT_EXPECT_EQ(test, 2, rust_signal_dequeue_test_next_first_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_next_synchronous_count());
#endif
}

static void signal_dequeue_test_local(struct kunit *test)
{
	struct kernel_siginfo info;
	enum pid_type type = PIDTYPE_TGID;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	signal_dequeue_test_pending(&current->pending, SIGUSR1);
	clear_siginfo(&info);
	sigemptyset(&mask);

	ret = signal_dequeue_test_call(&mask, &info, &type);

	KUNIT_EXPECT_EQ(test, SIGUSR1, ret);
	KUNIT_EXPECT_EQ(test, PIDTYPE_PID, type);
	KUNIT_EXPECT_EQ(test, SIGUSR1, info.si_signo);
	KUNIT_EXPECT_EQ(test, SI_USER, info.si_code);
	KUNIT_EXPECT_FALSE(test, sigismember(&current->pending.signal, SIGUSR1));
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_dequeue_entry_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_local_count());
	KUNIT_EXPECT_EQ(test, 0, rust_signal_dequeue_test_shared_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_deliver_count());
#endif
}

static void signal_dequeue_test_shared(struct kunit *test)
{
	struct kernel_siginfo info;
	enum pid_type type = PIDTYPE_PID;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	signal_dequeue_test_pending(&current->signal->shared_pending, SIGUSR2);
	clear_siginfo(&info);
	sigemptyset(&mask);

	ret = signal_dequeue_test_call(&mask, &info, &type);

	KUNIT_EXPECT_EQ(test, SIGUSR2, ret);
	KUNIT_EXPECT_EQ(test, PIDTYPE_TGID, type);
	KUNIT_EXPECT_EQ(test, SIGUSR2, info.si_signo);
	KUNIT_EXPECT_FALSE(test,
			   sigismember(&current->signal->shared_pending.signal,
				       SIGUSR2));
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_shared_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_deliver_count());
#endif
}

static void signal_dequeue_test_blocked(struct kunit *test)
{
	struct kernel_siginfo info;
	enum pid_type type;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	signal_dequeue_test_pending(&current->pending, SIGUSR1);
	signal_dequeue_test_pending(&current->signal->shared_pending, SIGUSR1);
	clear_siginfo(&info);
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);

	ret = signal_dequeue_test_call(&mask, &info, &type);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_TRUE(test, sigismember(&current->pending.signal, SIGUSR1));
	KUNIT_EXPECT_TRUE(test,
			  sigismember(&current->signal->shared_pending.signal,
				      SIGUSR1));
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_shared_count());
	KUNIT_EXPECT_EQ(test, 1, rust_signal_dequeue_test_empty_count());
#endif
}

static void signal_dequeue_test_stop_marker(struct kunit *test)
{
	struct kernel_siginfo info;
	enum pid_type type;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	signal_dequeue_test_pending(&current->pending, SIGSTOP);
	clear_siginfo(&info);
	sigemptyset(&mask);

	ret = signal_dequeue_test_call(&mask, &info, &type);

	KUNIT_EXPECT_EQ(test, SIGSTOP, ret);
	KUNIT_EXPECT_NE(test, 0, current->jobctl & JOBCTL_STOP_DEQUEUED);
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_stop_marker_count());
#endif
}

static void signal_dequeue_test_synchronous(struct kunit *test)
{
	struct kernel_siginfo info;
	unsigned int queues;
	int ret;

	signal_dequeue_test_reset();
	ret = signal_dequeue_test_queue_sync(SIGSEGV, SI_KERNEL);
	spin_lock_irq(&current->sighand->siglock);
	queues = signal_dequeue_test_queue_count(&current->pending);
	spin_unlock_irq(&current->sighand->siglock);

	KUNIT_EXPECT_EQ(test, 0, ret);
	KUNIT_EXPECT_EQ(test, 1, queues);
	KUNIT_EXPECT_TRUE(test, sigismember(&current->pending.signal, SIGSEGV));

	clear_siginfo(&info);
	ret = signal_dequeue_synchronous_test_call(&info);
	KUNIT_EXPECT_EQ(test, SIGSEGV, ret);
	KUNIT_EXPECT_EQ(test, SIGSEGV, info.si_signo);
	KUNIT_EXPECT_EQ(test, SI_KERNEL, info.si_code);
	KUNIT_EXPECT_FALSE(test, sigismember(&current->pending.signal, SIGSEGV));
	KUNIT_EXPECT_EQ(test, 0,
			signal_dequeue_test_queue_count(&current->pending));
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_synchronous_entry_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_synchronous_clear_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_synchronous_deliver_count());
#endif
}

static void signal_dequeue_test_synchronous_miss(struct kunit *test)
{
	struct kernel_siginfo info;
	enum pid_type type;
	sigset_t mask;
	int ret;

	signal_dequeue_test_reset();
	clear_siginfo(&info);
	ret = signal_dequeue_synchronous_test_call(&info);
	KUNIT_EXPECT_EQ(test, 0, ret);

	signal_dequeue_test_pending(&current->pending, SIGUSR1);
	sigemptyset(&mask);
	ret = signal_dequeue_test_call(&mask, &info, &type);
	KUNIT_EXPECT_EQ(test, SIGUSR1, ret);
	ret = signal_dequeue_synchronous_test_call(&info);
	KUNIT_EXPECT_EQ(test, 0, ret);
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 2,
			rust_signal_dequeue_test_synchronous_entry_count());
	KUNIT_EXPECT_EQ(test, 2,
			rust_signal_dequeue_test_synchronous_precheck_miss_count());
#endif
}

static void signal_dequeue_test_synchronous_duplicate(struct kunit *test)
{
	struct kernel_siginfo info;
	int ret;

	signal_dequeue_test_reset();
	KUNIT_EXPECT_EQ(test, 0,
			signal_dequeue_test_queue_sync(SIGSEGV, SI_KERNEL));
	KUNIT_EXPECT_EQ(test, 0,
			signal_dequeue_test_queue_sync(SIGSEGV, SI_KERNEL));

	clear_siginfo(&info);
	ret = signal_dequeue_synchronous_test_call(&info);
	KUNIT_EXPECT_EQ(test, SIGSEGV, ret);
	KUNIT_EXPECT_TRUE(test, sigismember(&current->pending.signal, SIGSEGV));
	KUNIT_EXPECT_EQ(test, 1,
			signal_dequeue_test_queue_count(&current->pending));

	clear_siginfo(&info);
	ret = signal_dequeue_synchronous_test_call(&info);
	KUNIT_EXPECT_EQ(test, SIGSEGV, ret);
	KUNIT_EXPECT_FALSE(test, sigismember(&current->pending.signal, SIGSEGV));
	KUNIT_EXPECT_EQ(test, 0,
			signal_dequeue_test_queue_count(&current->pending));
#ifdef CONFIG_RUST_SIGNAL_DEQUEUE
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_synchronous_duplicate_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_dequeue_test_synchronous_clear_count());
	KUNIT_EXPECT_EQ(test, 2,
			rust_signal_dequeue_test_synchronous_deliver_count());
#endif
}

static void signal_dequeue_test_suite_run(struct kunit *test)
{
	struct signal_dequeue_test_saved saved;

	signal_dequeue_test_save(&saved);
	signal_dequeue_test_next_signal(test);
	signal_dequeue_test_local(test);
	signal_dequeue_test_shared(test);
	signal_dequeue_test_blocked(test);
	signal_dequeue_test_stop_marker(test);
	signal_dequeue_test_synchronous(test);
	signal_dequeue_test_synchronous_miss(test);
	signal_dequeue_test_synchronous_duplicate(test);
	signal_dequeue_test_restore(&saved);
}

static struct kunit_case signal_dequeue_test_cases[] = {
	KUNIT_CASE(signal_dequeue_test_suite_run),
	{}
};

static struct kunit_suite signal_dequeue_test_suite = {
	.name = "signal_dequeue",
	.test_cases = signal_dequeue_test_cases,
};

kunit_test_suite(signal_dequeue_test_suite);

MODULE_DESCRIPTION("KUnit tests for signal dequeue");
MODULE_LICENSE("GPL");
