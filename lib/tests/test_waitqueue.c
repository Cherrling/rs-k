// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/wait_bit.h>

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

static int waitqueue_test_position(struct wait_queue_head *wq_head,
				   struct waitqueue_test_record *record)
{
	struct wait_queue_entry *entry;
	struct waitqueue_test_record *pos;
	unsigned long flags;
	int position = -ENOENT;
	int index = 0;

	spin_lock_irqsave(&wq_head->lock, flags);
	list_for_each_entry(entry, &wq_head->head, entry) {
		pos = container_of(entry, struct waitqueue_test_record, entry);
		if (pos == record) {
			position = index;
			break;
		}
		index++;
	}
	spin_unlock_irqrestore(&wq_head->lock, flags);
	return position;
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

static void waitqueue_test_entry_order(struct kunit *test)
{
	struct waitqueue_test_record normal1;
	struct waitqueue_test_record normal2;
	struct waitqueue_test_record priority1;
	struct waitqueue_test_record priority2;
	struct waitqueue_test_record exclusive;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&normal1, 1, 0, NULL, NULL);
	waitqueue_test_init_record(&normal2, 1, 1, NULL, NULL);
	waitqueue_test_init_record(&priority1, 1, 2, NULL, NULL);
	waitqueue_test_init_record(&priority2, 1, 3, NULL, NULL);
	waitqueue_test_init_record(&exclusive, 1, 4, NULL, NULL);

	normal1.entry.flags = WQ_FLAG_EXCLUSIVE;
	add_wait_queue(&wq_head, &normal1.entry);
	add_wait_queue_priority(&wq_head, &priority1.entry);
	add_wait_queue_priority(&wq_head, &priority2.entry);
	normal2.entry.flags = WQ_FLAG_EXCLUSIVE;
	add_wait_queue(&wq_head, &normal2.entry);
	add_wait_queue_exclusive(&wq_head, &exclusive.entry);

	KUNIT_EXPECT_EQ(test, normal1.entry.flags, 0);
	KUNIT_EXPECT_EQ(test, normal2.entry.flags, 0);
	KUNIT_EXPECT_EQ(test, priority1.entry.flags, WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, priority2.entry.flags, WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, exclusive.entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &priority1), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &priority2), 1);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &normal2), 2);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &normal1), 3);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &exclusive), 4);

	remove_wait_queue(&wq_head, &priority1.entry);
	remove_wait_queue(&wq_head, &priority2.entry);
	remove_wait_queue(&wq_head, &normal2.entry);
	remove_wait_queue(&wq_head, &normal1.entry);
	remove_wait_queue(&wq_head, &exclusive.entry);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
}

static void waitqueue_test_priority_exclusive(struct kunit *test)
{
	struct waitqueue_test_record priority1;
	struct waitqueue_test_record priority2;
	struct waitqueue_test_record priority3;
	struct waitqueue_test_record normal;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&priority1, 1, 0, NULL, NULL);
	waitqueue_test_init_record(&priority2, 1, 1, NULL, NULL);
	waitqueue_test_init_record(&priority3, 1, 2, NULL, NULL);
	waitqueue_test_init_record(&normal, 1, 3, NULL, NULL);

	KUNIT_EXPECT_EQ(test,
			add_wait_queue_priority_exclusive(&wq_head,
							  &priority1.entry), 0);
	KUNIT_EXPECT_EQ(test,
			add_wait_queue_priority_exclusive(&wq_head,
							  &priority2.entry), -EBUSY);
	KUNIT_EXPECT_EQ(test, priority2.entry.flags,
			WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_TRUE(test, list_empty(&priority2.entry.entry));
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &priority1), 0);

	remove_wait_queue(&wq_head, &priority1.entry);
	add_wait_queue(&wq_head, &normal.entry);
	KUNIT_EXPECT_EQ(test,
			add_wait_queue_priority_exclusive(&wq_head,
							  &priority3.entry), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &priority3), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &normal), 1);

	remove_wait_queue(&wq_head, &priority3.entry);
	remove_wait_queue(&wq_head, &normal.entry);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
}

static void waitqueue_test_init_wait_entry(struct kunit *test)
{
	struct wait_queue_entry entry = {
		.flags = WQ_FLAG_PRIORITY,
		.private = NULL,
		.func = NULL,
		.entry = { NULL, NULL },
	};

	init_wait_entry(&entry, WQ_FLAG_EXCLUSIVE);

	KUNIT_EXPECT_EQ(test, entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_PTR_EQ(test, entry.private, current);
	KUNIT_EXPECT_TRUE(test, entry.func == autoremove_wake_function);
	KUNIT_EXPECT_PTR_EQ(test, entry.entry.next, &entry.entry);
	KUNIT_EXPECT_PTR_EQ(test, entry.entry.prev, &entry.entry);
	KUNIT_EXPECT_TRUE(test, list_empty(&entry.entry));
}

static void waitqueue_test_autoremove_wake(struct kunit *test)
{
	struct wait_queue_entry entry;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	unsigned long flags;
	int ret;

	init_wait_entry(&entry, WQ_FLAG_EXCLUSIVE);
	add_wait_queue_exclusive(&wq_head, &entry);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 1);

	set_current_state(TASK_RUNNING);
	spin_lock_irqsave(&wq_head.lock, flags);
	ret = autoremove_wake_function(&entry, TASK_NORMAL, 0, NULL);
	spin_unlock_irqrestore(&wq_head.lock, flags);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_FALSE(test, list_empty(&entry.entry));
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 1);
	remove_wait_queue(&wq_head, &entry);

	init_wait_entry(&entry, WQ_FLAG_EXCLUSIVE);
	add_wait_queue_exclusive(&wq_head, &entry);
	set_current_state(TASK_INTERRUPTIBLE);
	spin_lock_irqsave(&wq_head.lock, flags);
	ret = autoremove_wake_function(&entry, TASK_NORMAL, 0, NULL);
	spin_unlock_irqrestore(&wq_head.lock, flags);

	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, list_empty(&entry.entry));
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void waitqueue_test_wait_woken(struct kunit *test)
{
	struct wait_queue_entry entry;
	long timeout;

	memset(&entry, 0, sizeof(entry));
	entry.flags = WQ_FLAG_WOKEN | WQ_FLAG_EXCLUSIVE;
	set_current_state(TASK_RUNNING);

	timeout = wait_woken(&entry, TASK_INTERRUPTIBLE, 37);

	KUNIT_EXPECT_EQ(test, timeout, 37);
	KUNIT_EXPECT_EQ(test, entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	entry.flags = WQ_FLAG_PRIORITY;
	set_current_state(TASK_RUNNING);

	timeout = wait_woken(&entry, TASK_INTERRUPTIBLE, 0);

	KUNIT_EXPECT_EQ(test, timeout, 0);
	KUNIT_EXPECT_EQ(test, entry.flags, WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void waitqueue_test_woken_wake(struct kunit *test)
{
	struct wait_queue_entry entry;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	unsigned long flags;
	long timeout;
	int ret;

	init_wait_entry(&entry, WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY);
	entry.func = woken_wake_function;

	set_current_state(TASK_INTERRUPTIBLE);
	spin_lock_irqsave(&wq_head.lock, flags);
	__add_wait_queue_entry_tail(&wq_head, &entry);
	ret = woken_wake_function(&entry, TASK_NORMAL, 0, NULL);
	spin_unlock_irqrestore(&wq_head.lock, flags);

	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, entry.flags,
			WQ_FLAG_WOKEN | WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	timeout = wait_woken(&entry, TASK_INTERRUPTIBLE, 41);

	KUNIT_EXPECT_EQ(test, timeout, 41);
	KUNIT_EXPECT_EQ(test, entry.flags,
			WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	remove_wait_queue(&wq_head, &entry);
}

static void waitqueue_test_woken_wake_bit(struct kunit *test)
{
	struct wait_bit_queue_entry wbq_entry;
	struct wait_bit_key key = {
		.flags = NULL,
		.bit_nr = 3,
	};
	struct wait_bit_key mismatch = {
		.flags = NULL,
		.bit_nr = 4,
	};
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	unsigned long word = 0;
	unsigned long flags;
	long timeout;
	int ret;

	key.flags = &word;
	mismatch.flags = &word;
	memset(&wbq_entry, 0, sizeof(wbq_entry));
	wbq_entry.key = key;
	wbq_entry.wq_entry.flags = WQ_FLAG_EXCLUSIVE;
	wbq_entry.wq_entry.private = current;
	wbq_entry.wq_entry.func = woken_wake_bit_function;
	INIT_LIST_HEAD(&wbq_entry.wq_entry.entry);

	ret = woken_wake_bit_function(&wbq_entry.wq_entry, TASK_NORMAL, 0,
				      &mismatch);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, wbq_entry.wq_entry.flags, WQ_FLAG_EXCLUSIVE);

	set_current_state(TASK_INTERRUPTIBLE);
	spin_lock_irqsave(&wq_head.lock, flags);
	__add_wait_queue_entry_tail(&wq_head, &wbq_entry.wq_entry);
	ret = woken_wake_bit_function(&wbq_entry.wq_entry, TASK_NORMAL, 0,
				      &key);
	spin_unlock_irqrestore(&wq_head.lock, flags);

	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_EQ(test, wbq_entry.wq_entry.flags,
			WQ_FLAG_WOKEN | WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	timeout = wait_woken(&wbq_entry.wq_entry, TASK_INTERRUPTIBLE, 23);

	KUNIT_EXPECT_EQ(test, timeout, 23);
	KUNIT_EXPECT_EQ(test, wbq_entry.wq_entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	remove_wait_queue(&wq_head, &wbq_entry.wq_entry);
}

static void waitqueue_test_prepare_to_wait(struct kunit *test)
{
	struct waitqueue_test_record record;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&record, 1, 0, NULL, NULL);
	record.entry.flags = WQ_FLAG_EXCLUSIVE | WQ_FLAG_PRIORITY;

	prepare_to_wait(&wq_head, &record.entry, TASK_INTERRUPTIBLE);
	KUNIT_EXPECT_EQ(test, record.entry.flags, WQ_FLAG_PRIORITY);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 1);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &record), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_INTERRUPTIBLE);

	prepare_to_wait(&wq_head, &record.entry, TASK_UNINTERRUPTIBLE);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 1);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &record), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_UNINTERRUPTIBLE);

	finish_wait(&wq_head, &record.entry);
	KUNIT_EXPECT_TRUE(test, list_empty(&record.entry.entry));
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void waitqueue_test_prepare_to_wait_exclusive(struct kunit *test)
{
	struct waitqueue_test_record first;
	struct waitqueue_test_record second;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);

	waitqueue_test_init_record(&first, 1, 0, NULL, NULL);
	waitqueue_test_init_record(&second, 1, 1, NULL, NULL);

	KUNIT_EXPECT_TRUE(test,
			  prepare_to_wait_exclusive(&wq_head, &first.entry,
						    TASK_INTERRUPTIBLE));
	KUNIT_EXPECT_EQ(test, first.entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &first), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_INTERRUPTIBLE);

	KUNIT_EXPECT_FALSE(test,
			   prepare_to_wait_exclusive(&wq_head, &second.entry,
						     TASK_UNINTERRUPTIBLE));
	KUNIT_EXPECT_FALSE(test,
			   prepare_to_wait_exclusive(&wq_head, &first.entry,
						     TASK_UNINTERRUPTIBLE));
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 2);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &first), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &second), 1);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_UNINTERRUPTIBLE);

	finish_wait(&wq_head, &first.entry);
	finish_wait(&wq_head, &second.entry);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void waitqueue_test_prepare_to_wait_event(struct kunit *test)
{
	struct waitqueue_test_record normal;
	struct waitqueue_test_record exclusive;
	DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq_head);
	long ret;

	waitqueue_test_init_record(&normal, 1, 0, NULL, NULL);
	waitqueue_test_init_record(&exclusive, 1, 1, NULL, NULL);
	exclusive.entry.flags = WQ_FLAG_EXCLUSIVE;

	KUNIT_EXPECT_EQ(test,
			prepare_to_wait_event(&wq_head, &normal.entry,
					      TASK_UNINTERRUPTIBLE), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &normal), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test,
			prepare_to_wait_event(&wq_head, &exclusive.entry,
					      TASK_INTERRUPTIBLE), 0);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &exclusive),
			1);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state),
			TASK_INTERRUPTIBLE);

	allow_kernel_signal(SIGUSR1);
	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, current, 1), 0);
	KUNIT_EXPECT_TRUE(test, signal_pending(current));
	ret = prepare_to_wait_event(&wq_head, &exclusive.entry,
				    TASK_INTERRUPTIBLE);
	flush_signals(current);
	disallow_signal(SIGUSR1);

	KUNIT_EXPECT_EQ(test, ret, -ERESTARTSYS);
	KUNIT_EXPECT_TRUE(test, list_empty(&exclusive.entry.entry));
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 1);
	KUNIT_EXPECT_EQ(test, waitqueue_test_position(&wq_head, &normal), 0);

	finish_wait(&wq_head, &exclusive.entry);
	finish_wait(&wq_head, &normal.entry);
	KUNIT_EXPECT_EQ(test, waitqueue_test_count(&wq_head), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
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
	KUNIT_CASE(waitqueue_test_entry_order),
	KUNIT_CASE(waitqueue_test_priority_exclusive),
	KUNIT_CASE(waitqueue_test_init_wait_entry),
	KUNIT_CASE(waitqueue_test_autoremove_wake),
	KUNIT_CASE(waitqueue_test_wait_woken),
	KUNIT_CASE(waitqueue_test_woken_wake),
	KUNIT_CASE(waitqueue_test_woken_wake_bit),
	KUNIT_CASE(waitqueue_test_prepare_to_wait),
	KUNIT_CASE(waitqueue_test_prepare_to_wait_exclusive),
	KUNIT_CASE(waitqueue_test_prepare_to_wait_event),
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
