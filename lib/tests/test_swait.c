// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/swait.h>

#define SWAIT_TEST_WORKERS 4
#define SWAIT_TEST_TIMEOUT_MS 30000

struct swait_test_worker {
	struct swait_queue_head *queue;
	struct swait_queue wait;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *wake_count;
	int *wake_order;
	int index;
};

static bool swait_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(SWAIT_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int swait_test_worker(void *data)
{
	struct swait_test_worker *worker = data;

	INIT_LIST_HEAD(&worker->wait.task_list);
	prepare_to_swait_exclusive(worker->queue, &worker->wait,
				   TASK_UNINTERRUPTIBLE);
	atomic_inc(worker->ready);
	schedule();
	finish_swait(worker->queue, &worker->wait);

	if (worker->wake_order) {
		int slot = atomic_inc_return(worker->wake_count) - 1;

		worker->wake_order[slot] = worker->index;
	}
	atomic_inc(worker->done);

	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);

	return 0;
}

static void swait_test_init_worker(struct swait_test_worker *worker,
				   struct swait_queue_head *queue,
				   atomic_t *ready, atomic_t *done,
				   atomic_t *wake_count, int *wake_order,
				   int index)
{
	worker->queue = queue;
	worker->ready = ready;
	worker->done = done;
	worker->wake_count = wake_count;
	worker->wake_order = wake_order;
	worker->index = index;
}

static void swait_test_stop_workers(struct task_struct **tasks,
				    struct swait_queue_head *queue, int count)
{
	int i;

	swake_up_all(queue);
	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void swait_test_queue_lifecycle(struct kunit *test)
{
	struct swait_queue_head queue;
	struct swait_queue wait;
	unsigned long flags;
	bool active;
	bool empty;
	bool running;
	long ret;

	init_swait_queue_head(&queue);
	INIT_LIST_HEAD(&wait.task_list);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

	swake_up_one(&queue);
	swake_up_all(&queue);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

	ret = prepare_to_swait_event(&queue, &wait, TASK_UNINTERRUPTIBLE);
	active = swait_active(&queue);
	empty = list_empty(&wait.task_list);
	finish_swait(&queue, &wait);
	running = READ_ONCE(current->__state) == TASK_RUNNING;
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_TRUE(test, active);
	KUNIT_EXPECT_FALSE(test, empty);
	KUNIT_EXPECT_TRUE(test, running);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));
	KUNIT_EXPECT_TRUE(test, list_empty(&wait.task_list));

	prepare_to_swait_exclusive(&queue, &wait, TASK_UNINTERRUPTIBLE);
	active = swait_active(&queue);
	finish_swait(&queue, &wait);
	KUNIT_EXPECT_TRUE(test, active);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

	raw_spin_lock_irqsave(&queue.lock, flags);
	__prepare_to_swait(&queue, &wait);
	raw_spin_unlock_irqrestore(&queue.lock, flags);
	active = swait_active(&queue);

	raw_spin_lock_irqsave(&queue.lock, flags);
	__finish_swait(&queue, &wait);
	raw_spin_unlock_irqrestore(&queue.lock, flags);
	running = READ_ONCE(current->__state) == TASK_RUNNING;
	KUNIT_EXPECT_TRUE(test, active);
	KUNIT_EXPECT_TRUE(test, running);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));
}

static void swait_test_event_signal(struct kunit *test)
{
	struct swait_queue_head queue;
	struct swait_queue wait;
	long ret;

	init_swait_queue_head(&queue);
	INIT_LIST_HEAD(&wait.task_list);

	allow_kernel_signal(SIGUSR1);
	ret = prepare_to_swait_event(&queue, &wait, TASK_UNINTERRUPTIBLE);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&queue.task_list));
	KUNIT_EXPECT_FALSE(test, list_empty(&wait.task_list));

	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, current, 1), 0);
	KUNIT_EXPECT_TRUE(test, signal_pending(current));
	ret = prepare_to_swait_event(&queue, &wait, TASK_INTERRUPTIBLE);
	flush_signals(current);
	disallow_signal(SIGUSR1);

	KUNIT_EXPECT_EQ(test, ret, -ERESTARTSYS);
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));
	KUNIT_EXPECT_TRUE(test, list_empty(&wait.task_list));
	finish_swait(&queue, &wait);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void swait_test_fifo_wake_one(struct kunit *test)
{
	const int workers_count = 3;
	struct task_struct *tasks[3] = {};
	struct swait_test_worker workers[3];
	struct swait_queue_head queue;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int wake_order[3] = {};
	int started = 0;
	int i;

	init_swait_queue_head(&queue);
	for (i = 0; i < workers_count; i++) {
		swait_test_init_worker(&workers[i], &queue, &ready, &done,
				       &wake_count, wake_order, i);
		tasks[i] = kthread_run(swait_test_worker, &workers[i],
				       "swait-fifo-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start swait worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!swait_test_wait_for(&ready, i + 1)) {
			KUNIT_FAIL(test, "swait worker did not become ready");
			goto cleanup;
		}
	}

	KUNIT_EXPECT_FALSE(test, list_empty(&queue.task_list));
	for (i = 0; i < workers_count; i++) {
		swake_up_one(&queue);
		if (!swait_test_wait_for(&done, i + 1)) {
			KUNIT_FAIL(test, "swait worker did not wake");
			goto cleanup;
		}
		KUNIT_EXPECT_EQ(test, wake_order[i], i);
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

cleanup:
	swait_test_stop_workers(tasks, &queue, started);
}

static void swait_test_wake_all(struct kunit *test)
{
	struct task_struct *tasks[SWAIT_TEST_WORKERS] = {};
	struct swait_test_worker workers[SWAIT_TEST_WORKERS];
	struct swait_queue_head queue;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int started = 0;
	int i;

	init_swait_queue_head(&queue);
	for (i = 0; i < SWAIT_TEST_WORKERS; i++) {
		swait_test_init_worker(&workers[i], &queue, &ready, &done,
				       &wake_count, NULL, i);
		tasks[i] = kthread_run(swait_test_worker, &workers[i],
				       "swait-all-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start swait worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!swait_test_wait_for(&ready, SWAIT_TEST_WORKERS)) {
		KUNIT_FAIL(test, "swait workers did not become ready");
		goto cleanup;
	}
	KUNIT_EXPECT_FALSE(test, list_empty(&queue.task_list));

	swake_up_all(&queue);
	if (!swait_test_wait_for(&done, SWAIT_TEST_WORKERS)) {
		KUNIT_FAIL(test, "swait workers did not finish");
		goto cleanup;
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

cleanup:
	swait_test_stop_workers(tasks, &queue, started);
}

static void swait_test_wake_all_locked(struct kunit *test)
{
	struct task_struct *tasks[SWAIT_TEST_WORKERS] = {};
	struct swait_test_worker workers[SWAIT_TEST_WORKERS];
	struct swait_queue_head queue;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	unsigned long flags;
	int started = 0;
	int i;

	init_swait_queue_head(&queue);
	for (i = 0; i < SWAIT_TEST_WORKERS; i++) {
		swait_test_init_worker(&workers[i], &queue, &ready, &done,
				       &wake_count, NULL, i);
		tasks[i] = kthread_run(swait_test_worker, &workers[i],
				       "swait-locked-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start swait worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!swait_test_wait_for(&ready, SWAIT_TEST_WORKERS)) {
		KUNIT_FAIL(test, "swait workers did not become ready");
		goto cleanup;
	}
	KUNIT_EXPECT_FALSE(test, list_empty(&queue.task_list));

	raw_spin_lock_irqsave(&queue.lock, flags);
	swake_up_all_locked(&queue);
	raw_spin_unlock_irqrestore(&queue.lock, flags);

	if (!swait_test_wait_for(&done, SWAIT_TEST_WORKERS)) {
		KUNIT_FAIL(test, "swait workers did not finish");
		goto cleanup;
	}
	KUNIT_EXPECT_TRUE(test, list_empty(&queue.task_list));

cleanup:
	swait_test_stop_workers(tasks, &queue, started);
}

static struct kunit_case swait_test_cases[] = {
	KUNIT_CASE(swait_test_queue_lifecycle),
	KUNIT_CASE(swait_test_event_signal),
	KUNIT_CASE(swait_test_fifo_wake_one),
	KUNIT_CASE(swait_test_wake_all),
	KUNIT_CASE(swait_test_wake_all_locked),
	{}
};

static struct kunit_suite swait_test_suite = {
	.name = "swait",
	.test_cases = swait_test_cases,
};

kunit_test_suites(&swait_test_suite);

MODULE_DESCRIPTION("simple wait queue KUnit test suite");
MODULE_LICENSE("GPL");
