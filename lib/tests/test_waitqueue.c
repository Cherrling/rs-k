// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>

#define WAITQUEUE_TEST_TIMEOUT_MS 30000
/* Mirror the scheduler-private wake flags checked by this test. */
#define WAITQUEUE_TEST_WF_SYNC 0x10
#define WAITQUEUE_TEST_WF_CURRENT_CPU 0x40

struct waitqueue_test_record {
	struct wait_queue_entry entry;
	atomic_t calls;
	atomic_t *order_counter;
	int *order;
	int order_index;
	int wake_ret;
	bool remove_on_wake;
	unsigned int last_mode;
	int last_wake_flags;
	void *last_key;
};

struct waitqueue_test_worker {
	struct wait_queue_head *wq_head;
	atomic_t ready;
	atomic_t condition;
	atomic_t done;
};

static int waitqueue_test_wake_func(struct wait_queue_entry *entry,
				    unsigned int mode, int wake_flags, void *key)
{
	struct waitqueue_test_record *record =
		container_of(entry, struct waitqueue_test_record, entry);
	int slot;

	record->last_mode = mode;
	record->last_wake_flags = wake_flags;
	record->last_key = key;
	if (record->order) {
		slot = atomic_inc_return(record->order_counter) - 1;
		record->order[slot] = record->order_index;
	}
	atomic_inc(&record->calls);

	if (record->remove_on_wake)
		list_del_init(&entry->entry);

	return record->wake_ret;
}

static void waitqueue_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int waitqueue_test_worker_thread(void *data)
{
	struct waitqueue_test_worker *worker = data;

	atomic_inc(&worker->ready);
	wait_event_interruptible(*worker->wq_head,
				 atomic_read(&worker->condition));
	atomic_inc(&worker->done);
	waitqueue_test_idle_until_stopped();
	return 0;
}

static bool waitqueue_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(WAITQUEUE_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int waitqueue_test_count(struct wait_queue_head *wq_head)
{
	unsigned long flags;
	int count;

	spin_lock_irqsave(&wq_head->lock, flags);
	count = list_count_nodes(&wq_head->head);
	spin_unlock_irqrestore(&wq_head->lock, flags);
	return count;
}

static bool waitqueue_test_wait_for_active(struct wait_queue_head *wq_head)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(WAITQUEUE_TEST_TIMEOUT_MS);

	while (!waitqueue_test_count(wq_head) &&
	       time_before(jiffies, deadline))
		cond_resched();

	return waitqueue_test_count(wq_head) > 0;
}

static void waitqueue_test_init_record(struct waitqueue_test_record *record,
				       int wake_ret, int order_index,
				       int *order, atomic_t *order_counter)
{
	memset(record, 0, sizeof(*record));
	atomic_set(&record->calls, 0);
	record->wake_ret = wake_ret;
	record->order_index = order_index;
	record->order = order;
	record->order_counter = order_counter;
	init_waitqueue_func_entry(&record->entry, waitqueue_test_wake_func);
	INIT_LIST_HEAD(&record->entry.entry);
}

static void waitqueue_test_add_tail(struct wait_queue_head *wq_head,
				    struct waitqueue_test_record *record,
				    unsigned int flags)
{
	unsigned long lock_flags;

	record->entry.flags = flags;
	spin_lock_irqsave(&wq_head->lock, lock_flags);
	__add_wait_queue_entry_tail(wq_head, &record->entry);
	spin_unlock_irqrestore(&wq_head->lock, lock_flags);
}

static void waitqueue_test_remove(struct wait_queue_head *wq_head,
				  struct waitqueue_test_record *record)
{
	if (!list_empty(&record->entry.entry))
		remove_wait_queue(wq_head, &record->entry);
}

static void waitqueue_test_wake_all_order(struct kunit *test)
{
	struct waitqueue_test_record records[4];
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	atomic_t order_counter = ATOMIC_INIT(0);
	int order[4] = {};
	int key;
	int ret;
	int i;

	for (i = 0; i < 4; i++)
		waitqueue_test_init_record(&records[i], 1, i, order,
					   &order_counter);
	waitqueue_test_add_tail(&wq_head, &records[0], 0);
	waitqueue_test_add_tail(&wq_head, &records[1], WQ_FLAG_EXCLUSIVE);
	waitqueue_test_add_tail(&wq_head, &records[2], 0);
	waitqueue_test_add_tail(&wq_head, &records[3], WQ_FLAG_EXCLUSIVE);

	ret = __wake_up(&wq_head, TASK_INTERRUPTIBLE, 0, &key);

	KUNIT_EXPECT_EQ(test, ret, 2);
	for (i = 0; i < 4; i++) {
		KUNIT_EXPECT_EQ(test, atomic_read(&records[i].calls), 1);
		KUNIT_EXPECT_EQ(test, order[i], i);
		KUNIT_EXPECT_EQ(test, records[i].last_mode,
				TASK_INTERRUPTIBLE);
		KUNIT_EXPECT_EQ(test, records[i].last_wake_flags, 0);
		KUNIT_EXPECT_PTR_EQ(test, records[i].last_key, &key);
		waitqueue_test_remove(&wq_head, &records[i]);
	}
}

static void waitqueue_test_exclusive_budget(struct kunit *test)
{
	struct waitqueue_test_record records[4];
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	int ret;

	waitqueue_test_init_record(&records[0], 1, 0, NULL, NULL);
	waitqueue_test_init_record(&records[1], 0, 1, NULL, NULL);
	waitqueue_test_init_record(&records[2], 1, 2, NULL, NULL);
	waitqueue_test_init_record(&records[3], 1, 3, NULL, NULL);
	waitqueue_test_add_tail(&wq_head, &records[0], 0);
	waitqueue_test_add_tail(&wq_head, &records[1], WQ_FLAG_EXCLUSIVE);
	waitqueue_test_add_tail(&wq_head, &records[2], WQ_FLAG_EXCLUSIVE);
	waitqueue_test_add_tail(&wq_head, &records[3], WQ_FLAG_EXCLUSIVE);

	ret = __wake_up(&wq_head, TASK_NORMAL, 1, NULL);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[0].calls), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[1].calls), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[2].calls), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[3].calls), 0);

	ret = __wake_up(&wq_head, TASK_NORMAL, 2, NULL);
	KUNIT_EXPECT_EQ(test, ret, 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[0].calls), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[1].calls), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[2].calls), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&records[3].calls), 1);

	waitqueue_test_remove(&wq_head, &records[0]);
	waitqueue_test_remove(&wq_head, &records[1]);
	waitqueue_test_remove(&wq_head, &records[2]);
	waitqueue_test_remove(&wq_head, &records[3]);
}

static void waitqueue_test_negative_callback(struct kunit *test)
{
	struct waitqueue_test_record first;
	struct waitqueue_test_record second;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&first, -EIO, 0, NULL, NULL);
	waitqueue_test_init_record(&second, 1, 1, NULL, NULL);
	waitqueue_test_add_tail(&wq_head, &first, 0);
	waitqueue_test_add_tail(&wq_head, &second, 0);

	KUNIT_EXPECT_EQ(test, __wake_up(&wq_head, TASK_NORMAL, 1, NULL), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&first.calls), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&second.calls), 0);

	waitqueue_test_remove(&wq_head, &first);
	waitqueue_test_remove(&wq_head, &second);
}

static void waitqueue_test_wake_flags(struct kunit *test)
{
	struct waitqueue_test_record record;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	unsigned long flags;
	int key;

	waitqueue_test_init_record(&record, 1, 0, NULL, NULL);
	waitqueue_test_add_tail(&wq_head, &record, WQ_FLAG_EXCLUSIVE);

	__wake_up_on_current_cpu(&wq_head, TASK_NORMAL, &key);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags,
			WAITQUEUE_TEST_WF_CURRENT_CPU);
	KUNIT_EXPECT_PTR_EQ(test, record.last_key, &key);

	spin_lock_irqsave(&wq_head.lock, flags);
	__wake_up_locked(&wq_head, TASK_NORMAL, 0);
	spin_unlock_irqrestore(&wq_head.lock, flags);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags, 0);

	__wake_up_sync_key(&wq_head, TASK_NORMAL, &key);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags, WAITQUEUE_TEST_WF_SYNC);
	KUNIT_EXPECT_PTR_EQ(test, record.last_key, &key);

	spin_lock_irqsave(&wq_head.lock, flags);
	__wake_up_locked_key(&wq_head, TASK_NORMAL, &key);
	spin_unlock_irqrestore(&wq_head.lock, flags);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags, 0);
	KUNIT_EXPECT_PTR_EQ(test, record.last_key, &key);

	spin_lock_irqsave(&wq_head.lock, flags);
	__wake_up_locked_sync_key(&wq_head, TASK_NORMAL, &key);
	spin_unlock_irqrestore(&wq_head.lock, flags);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags, WAITQUEUE_TEST_WF_SYNC);
	KUNIT_EXPECT_PTR_EQ(test, record.last_key, &key);

	__wake_up_sync(&wq_head, TASK_NORMAL);
	KUNIT_EXPECT_EQ(test, record.last_wake_flags, WAITQUEUE_TEST_WF_SYNC);
	KUNIT_EXPECT_PTR_EQ(test, record.last_key, NULL);

	__wake_up_sync_key(NULL, TASK_NORMAL, &key);
	KUNIT_EXPECT_EQ(test, atomic_read(&record.calls), 6);
	__wake_up_sync(NULL, TASK_NORMAL);
	KUNIT_EXPECT_EQ(test, atomic_read(&record.calls), 6);

	waitqueue_test_remove(&wq_head, &record);
}

static void waitqueue_test_pollfree(struct kunit *test)
{
	struct waitqueue_test_record record;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&record, 1, 0, NULL, NULL);
	record.remove_on_wake = true;
	waitqueue_test_add_tail(&wq_head, &record, 0);

	wake_up_pollfree(&wq_head);

	KUNIT_EXPECT_EQ(test, atomic_read(&record.calls), 1);
	KUNIT_EXPECT_EQ(test, key_to_poll(record.last_key),
			EPOLLHUP | POLLFREE);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
}

static void waitqueue_test_scheduler_integration(struct kunit *test)
{
	struct waitqueue_test_worker worker = {
		.ready = ATOMIC_INIT(0),
		.condition = ATOMIC_INIT(0),
		.done = ATOMIC_INIT(0),
	};
	struct task_struct *task;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	worker.wq_head = &wq_head;
	task = kthread_run(waitqueue_test_worker_thread, &worker,
			   "waitqueue-kunit");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start waitqueue worker");
		return;
	}

	if (!waitqueue_test_wait_for(&worker.ready, 1) ||
	    !waitqueue_test_wait_for_active(&wq_head)) {
		KUNIT_FAIL(test, "waitqueue worker did not become ready");
		goto cleanup;
	}

	atomic_set(&worker.condition, 1);
	wake_up(&wq_head);
	if (!waitqueue_test_wait_for(&worker.done, 1))
		KUNIT_FAIL(test, "waitqueue worker did not wake");

cleanup:
	atomic_set(&worker.condition, 1);
	wake_up_all(&wq_head);
	kthread_stop(task);
}

static struct kunit_case waitqueue_test_cases[] = {
	KUNIT_CASE(waitqueue_test_wake_all_order),
	KUNIT_CASE(waitqueue_test_exclusive_budget),
	KUNIT_CASE(waitqueue_test_negative_callback),
	KUNIT_CASE(waitqueue_test_wake_flags),
	KUNIT_CASE(waitqueue_test_pollfree),
	KUNIT_CASE(waitqueue_test_scheduler_integration),
	{}
};

static struct kunit_suite waitqueue_test_suite = {
	.name = "waitqueue",
	.test_cases = waitqueue_test_cases,
};

kunit_test_suites(&waitqueue_test_suite);

MODULE_DESCRIPTION("wait queue wake KUnit test suite");
MODULE_LICENSE("GPL");
