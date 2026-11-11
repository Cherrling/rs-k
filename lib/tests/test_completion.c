// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/completion.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/limits.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

#define COMPLETION_TEST_WORKERS 4
#define COMPLETION_TEST_TIMEOUT_MS 30000

struct completion_test_waiter {
	struct completion *completion;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *wake_count;
	int *wake_order;
	int index;
};

struct completion_test_producer {
	struct completion *completion;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
};

static bool completion_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(COMPLETION_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int completion_test_waiter_count(struct completion *completion)
{
	unsigned long flags;
	int count;

	raw_spin_lock_irqsave(&completion->wait.lock, flags);
	count = list_count_nodes(&completion->wait.task_list);
	raw_spin_unlock_irqrestore(&completion->wait.lock, flags);

	return count;
}

static bool completion_test_wait_for_waiters(struct completion *completion,
					     int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(COMPLETION_TEST_TIMEOUT_MS);

	while (completion_test_waiter_count(completion) < target &&
	       time_before(jiffies, deadline))
		cond_resched();

	return completion_test_waiter_count(completion) >= target;
}

static void completion_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int completion_test_waiter(void *data)
{
	struct completion_test_waiter *waiter = data;

	atomic_inc(waiter->ready);
	wait_for_completion(waiter->completion);

	if (waiter->wake_order) {
		int slot = atomic_inc_return(waiter->wake_count) - 1;

		waiter->wake_order[slot] = waiter->index;
	}
	atomic_inc(waiter->done);
	completion_test_idle_until_stopped();
	return 0;
}

static int completion_test_producer(void *data)
{
	struct completion_test_producer *producer = data;

	atomic_inc(producer->ready);
	while (!atomic_read(producer->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	complete(producer->completion);
	atomic_inc(producer->done);
	completion_test_idle_until_stopped();
	return 0;
}

static void completion_test_init_waiter(struct completion_test_waiter *waiter,
					struct completion *completion,
					atomic_t *ready, atomic_t *done,
					atomic_t *wake_count, int *wake_order,
					int index)
{
	waiter->completion = completion;
	waiter->ready = ready;
	waiter->done = done;
	waiter->wake_count = wake_count;
	waiter->wake_order = wake_order;
	waiter->index = index;
}

static void completion_test_stop_workers(struct task_struct **tasks,
					 struct completion *completion,
					 int count)
{
	int i;

	complete_all(completion);
	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void completion_test_count_and_timeout(struct kunit *test)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	unsigned long timeout = msecs_to_jiffies(1);

	KUNIT_EXPECT_FALSE(test, completion_done(&completion));
	KUNIT_EXPECT_FALSE(test, try_wait_for_completion(&completion));

	complete(&completion);
	KUNIT_EXPECT_TRUE(test, completion_done(&completion));
	KUNIT_EXPECT_TRUE(test, try_wait_for_completion(&completion));
	KUNIT_EXPECT_FALSE(test, try_wait_for_completion(&completion));

	KUNIT_EXPECT_EQ(test, wait_for_completion_timeout(&completion, timeout),
			0UL);
	KUNIT_EXPECT_TRUE(test, list_empty(&completion.wait.task_list));

	complete(&completion);
	KUNIT_EXPECT_EQ(test, wait_for_completion_timeout(&completion, timeout),
			1UL);
	KUNIT_EXPECT_FALSE(test, completion_done(&completion));
}

static void completion_test_api_variants(struct kunit *test)
{
	DECLARE_COMPLETION_ONSTACK(completion);

	complete_on_current_cpu(&completion);
	KUNIT_EXPECT_TRUE(test, try_wait_for_completion(&completion));

	complete(&completion);
	KUNIT_EXPECT_EQ(test, wait_for_completion_interruptible(&completion), 0);

	complete(&completion);
	KUNIT_EXPECT_EQ(test, wait_for_completion_killable(&completion), 0);

	complete(&completion);
	KUNIT_EXPECT_EQ(test, wait_for_completion_state(&completion,
							TASK_INTERRUPTIBLE), 0);

	complete(&completion);
	wait_for_completion_io(&completion);
	KUNIT_EXPECT_FALSE(test, completion_done(&completion));

	complete(&completion);
	KUNIT_EXPECT_EQ(test,
			wait_for_completion_killable_timeout(&completion,
							     msecs_to_jiffies(1)),
			1L);

	KUNIT_EXPECT_EQ(test,
			wait_for_completion_io_timeout(&completion,
						       msecs_to_jiffies(1)),
			0UL);
	KUNIT_EXPECT_TRUE(test, list_empty(&completion.wait.task_list));
}

static void completion_test_interruptible_signal(struct kunit *test)
{
	DECLARE_COMPLETION_ONSTACK(completion);
	long ret;

	allow_kernel_signal(SIGUSR1);
	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, current, 1), 0);
	KUNIT_EXPECT_TRUE(test, signal_pending(current));

	ret = wait_for_completion_interruptible_timeout(&completion,
							msecs_to_jiffies(1));
	flush_signals(current);
	disallow_signal(SIGUSR1);

	KUNIT_EXPECT_EQ(test, ret, -ERESTARTSYS);
	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done), 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&completion.wait.task_list));
}

static void completion_test_fifo_complete_one(struct kunit *test)
{
	const int workers_count = 3;
	struct task_struct *tasks[3] = {};
	struct completion_test_waiter waiters[3];
	DECLARE_COMPLETION_ONSTACK(completion);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int wake_order[3] = {};
	int started = 0;
	int i;

	for (i = 0; i < workers_count; i++) {
		completion_test_init_waiter(&waiters[i], &completion, &ready,
					    &done, &wake_count, wake_order, i);
		tasks[i] = kthread_run(completion_test_waiter, &waiters[i],
				       "completion-fifo-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start completion waiter");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!completion_test_wait_for(&ready, i + 1) ||
		    !completion_test_wait_for_waiters(&completion, i + 1)) {
			KUNIT_FAIL(test, "completion waiter did not become ready");
			goto cleanup;
		}
	}

	for (i = 0; i < workers_count; i++) {
		complete(&completion);
		if (!completion_test_wait_for(&done, i + 1)) {
			KUNIT_FAIL(test, "completion waiter did not wake");
			goto cleanup;
		}
		KUNIT_EXPECT_EQ(test, wake_order[i], i);
	}
	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done), 0U);
	KUNIT_EXPECT_TRUE(test, list_empty(&completion.wait.task_list));

cleanup:
	completion_test_stop_workers(tasks, &completion, started);
}

static void completion_test_complete_all(struct kunit *test)
{
	struct task_struct *tasks[COMPLETION_TEST_WORKERS] = {};
	struct completion_test_waiter waiters[COMPLETION_TEST_WORKERS];
	DECLARE_COMPLETION_ONSTACK(completion);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int started = 0;
	int i;

	for (i = 0; i < COMPLETION_TEST_WORKERS; i++) {
		completion_test_init_waiter(&waiters[i], &completion, &ready,
					    &done, &wake_count, NULL, i);
		tasks[i] = kthread_run(completion_test_waiter, &waiters[i],
				       "completion-all-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start completion waiter");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!completion_test_wait_for(&ready, COMPLETION_TEST_WORKERS) ||
	    !completion_test_wait_for_waiters(&completion,
					      COMPLETION_TEST_WORKERS)) {
		KUNIT_FAIL(test, "completion waiters did not become ready");
		goto cleanup;
	}

	complete_all(&completion);
	if (!completion_test_wait_for(&done, COMPLETION_TEST_WORKERS)) {
		KUNIT_FAIL(test, "completion waiters did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done), UINT_MAX);
	KUNIT_EXPECT_TRUE(test, completion_done(&completion));
	KUNIT_EXPECT_TRUE(test, try_wait_for_completion(&completion));
	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done), UINT_MAX);
	KUNIT_EXPECT_EQ(test,
			wait_for_completion_timeout(&completion,
						    msecs_to_jiffies(1)),
			1UL);
	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done), UINT_MAX);

	reinit_completion(&completion);
	KUNIT_EXPECT_FALSE(test, completion_done(&completion));
	KUNIT_EXPECT_FALSE(test, try_wait_for_completion(&completion));

cleanup:
	completion_test_stop_workers(tasks, &completion, started);
}

static void completion_test_concurrent_producers(struct kunit *test)
{
	struct task_struct *tasks[COMPLETION_TEST_WORKERS] = {};
	struct completion_test_producer producers[COMPLETION_TEST_WORKERS];
	DECLARE_COMPLETION_ONSTACK(completion);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	int started = 0;
	int i;

	for (i = 0; i < COMPLETION_TEST_WORKERS; i++) {
		producers[i].completion = &completion;
		producers[i].ready = &ready;
		producers[i].start = &start;
		producers[i].done = &done;
		tasks[i] = kthread_run(completion_test_producer, &producers[i],
				       "completion-producer-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start completion producer");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!completion_test_wait_for(&ready, COMPLETION_TEST_WORKERS)) {
		KUNIT_FAIL(test, "completion producers did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!completion_test_wait_for(&done, COMPLETION_TEST_WORKERS)) {
		KUNIT_FAIL(test, "completion producers did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, READ_ONCE(completion.done),
			COMPLETION_TEST_WORKERS);
	KUNIT_EXPECT_TRUE(test, completion_done(&completion));
	for (i = 0; i < COMPLETION_TEST_WORKERS; i++)
		KUNIT_EXPECT_TRUE(test, try_wait_for_completion(&completion));
	KUNIT_EXPECT_FALSE(test, try_wait_for_completion(&completion));

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);
}

static struct kunit_case completion_test_cases[] = {
	KUNIT_CASE(completion_test_count_and_timeout),
	KUNIT_CASE(completion_test_api_variants),
	KUNIT_CASE(completion_test_interruptible_signal),
	KUNIT_CASE(completion_test_fifo_complete_one),
	KUNIT_CASE(completion_test_complete_all),
	KUNIT_CASE(completion_test_concurrent_producers),
	{}
};

static struct kunit_suite completion_test_suite = {
	.name = "completion",
	.test_cases = completion_test_cases,
};

kunit_test_suites(&completion_test_suite);

MODULE_DESCRIPTION("completion KUnit test suite");
MODULE_LICENSE("GPL");
