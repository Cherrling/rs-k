// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/cgroup.h>
#include <linux/sched/jobctl.h>
#include <linux/sched/signal.h>
#include <linux/signal-pending-recalc-rust.h>
#include <linux/signal.h>
#include <linux/spinlock.h>

struct signal_pending_recalc_test_saved {
	sigset_t blocked;
	sigset_t real_blocked;
	sigset_t pending;
	sigset_t shared_pending;
	unsigned long jobctl;
	bool pending_flag;
#ifdef CONFIG_CGROUPS
	bool frozen;
#endif
};

static void signal_pending_recalc_test_save(struct signal_pending_recalc_test_saved *saved)
{
	spin_lock_irq(&current->sighand->siglock);
	saved->blocked = current->blocked;
	saved->real_blocked = current->real_blocked;
	saved->pending = current->pending.signal;
	saved->shared_pending = current->signal->shared_pending.signal;
	saved->jobctl = current->jobctl;
	saved->pending_flag = test_tsk_thread_flag(current, TIF_SIGPENDING);
#ifdef CONFIG_CGROUPS
	saved->frozen = current->frozen;
#endif
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_pending_recalc_test_reset(void)
{
	flush_signals(current);

	spin_lock_irq(&current->sighand->siglock);
	sigemptyset(&current->blocked);
	sigemptyset(&current->real_blocked);
	sigemptyset(&current->pending.signal);
	sigemptyset(&current->signal->shared_pending.signal);
	current->jobctl &= ~(JOBCTL_PENDING_MASK | JOBCTL_TRAP_FREEZE);
	clear_tsk_thread_flag(current, TIF_SIGPENDING);
#ifdef CONFIG_CGROUPS
	current->frozen = false;
#endif
	spin_unlock_irq(&current->sighand->siglock);

#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	rust_signal_pending_recalc_test_reset();
#endif
}

static void
signal_pending_recalc_test_restore(const struct signal_pending_recalc_test_saved *saved)
{
	flush_signals(current);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = saved->blocked;
	current->real_blocked = saved->real_blocked;
	current->pending.signal = saved->pending;
	current->signal->shared_pending.signal = saved->shared_pending;
	current->jobctl = saved->jobctl;
	if (saved->pending_flag)
		set_tsk_thread_flag(current, TIF_SIGPENDING);
	else
		clear_tsk_thread_flag(current, TIF_SIGPENDING);
#ifdef CONFIG_CGROUPS
	current->frozen = saved->frozen;
#endif
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_pending_recalc_test_set_pending(struct sigpending *pending, int sig)
{
	spin_lock_irq(&current->sighand->siglock);
	sigaddset(&pending->signal, sig);
	spin_unlock_irq(&current->sighand->siglock);
}

static void signal_pending_recalc_test_run_tsk(struct kunit *test)
{
	bool ret;

	signal_pending_recalc_test_reset();
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_EXPECT_FALSE(test,
			   test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_set_count());
#endif

	signal_pending_recalc_test_reset();
	signal_pending_recalc_test_set_pending(&current->pending, SIGUSR1);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
#endif

	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	sigaddset(&current->pending.signal, SIGUSR1);
	sigaddset(&current->blocked, SIGUSR1);
	spin_unlock_irq(&current->sighand->siglock);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_EXPECT_FALSE(test,
			   test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_set_count());
#endif

	signal_pending_recalc_test_reset();
	signal_pending_recalc_test_set_pending(&current->signal->shared_pending, SIGUSR2);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
#endif

	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	current->jobctl |= JOBCTL_TRAP_FREEZE;
	spin_unlock_irq(&current->sighand->siglock);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
#endif

	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	current->jobctl |= JOBCTL_STOP_PENDING;
	spin_unlock_irq(&current->sighand->siglock);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_TRUE(test, ret);
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
#endif

#ifdef CONFIG_CGROUPS
	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	current->frozen = true;
	spin_unlock_irq(&current->sighand->siglock);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_TRUE(test, ret);
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
#endif
#endif

	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	spin_unlock_irq(&current->sighand->siglock);
	ret = signal_pending_recalc_test_tsk();
	KUNIT_EXPECT_FALSE(test, ret);
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
}

static void signal_pending_recalc_test_run_current(struct kunit *test)
{
	signal_pending_recalc_test_reset();
	spin_lock_irq(&current->sighand->siglock);
	set_tsk_thread_flag(current, TIF_SIGPENDING);
	spin_unlock_irq(&current->sighand->siglock);

	signal_pending_recalc_test();
	KUNIT_EXPECT_FALSE(test,
			   test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_recalc_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_set_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_freezing_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_clear_check_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_clear_count());
#endif

	signal_pending_recalc_test_reset();
	signal_pending_recalc_test_set_pending(&current->pending, SIGUSR1);
	signal_pending_recalc_test();
	KUNIT_EXPECT_TRUE(test,
			  test_tsk_thread_flag(current, TIF_SIGPENDING));
#ifdef CONFIG_RUST_SIGNAL_PENDING_RECALC
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_recalc_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_tsk_count());
	KUNIT_EXPECT_EQ(test, 1,
			rust_signal_pending_recalc_test_set_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_freezing_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_clear_check_count());
	KUNIT_EXPECT_EQ(test, 0,
			rust_signal_pending_recalc_test_clear_count());
#endif
}

static void signal_pending_recalc_test_suite_run(struct kunit *test)
{
	struct signal_pending_recalc_test_saved saved;

	signal_pending_recalc_test_save(&saved);
	signal_pending_recalc_test_run_tsk(test);
	signal_pending_recalc_test_run_current(test);
	signal_pending_recalc_test_restore(&saved);
}

static struct kunit_case signal_pending_recalc_test_cases[] = {
	KUNIT_CASE(signal_pending_recalc_test_suite_run),
	{}
};

static struct kunit_suite signal_pending_recalc_test_suite = {
	.name = "signal_pending_recalc",
	.test_cases = signal_pending_recalc_test_cases,
};

kunit_test_suite(signal_pending_recalc_test_suite);

MODULE_DESCRIPTION("KUnit tests for pending-signal recalculation");
MODULE_LICENSE("GPL");
