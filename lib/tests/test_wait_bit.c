// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/wait_bit.h>

#define WAIT_BIT_TEST_TIMEOUT_MS 30000

static atomic_t wait_bit_action_calls;

struct wait_bit_test_worker {
	struct wait_bit_queue_entry wbq_entry;
	unsigned long word;
	atomic_t action_ready;
	atomic_t action_returned;
	atomic_t done;
	bool lock;
	int ret;
};

static void wait_bit_test_init_entry(struct wait_bit_queue_entry *wbq_entry,
				     unsigned long *word, int bit, int flags)
{
	memset(wbq_entry, 0, sizeof(*wbq_entry));
	wbq_entry->key.flags = word;
	wbq_entry->key.bit_nr = bit;
	wbq_entry->wq_entry.flags = flags;
	wbq_entry->wq_entry.private = current;
	wbq_entry->wq_entry.func = wake_bit_function;
	INIT_LIST_HEAD(&wbq_entry->wq_entry.entry);
}

static bool wait_bit_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(WAIT_BIT_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int wait_bit_test_clear_action(struct wait_bit_key *key, int mode)
{
	atomic_inc(&wait_bit_action_calls);
	clear_bit(key->bit_nr, key->flags);
	return 0;
}

static int wait_bit_test_error_action(struct wait_bit_key *key, int mode)
{
	atomic_inc(&wait_bit_action_calls);
	return -EAGAIN;
}

static int wait_bit_test_unexpected_action(struct wait_bit_key *key, int mode)
{
	atomic_inc(&wait_bit_action_calls);
	return 0;
}

static int wait_bit_test_scheduler_action(struct wait_bit_key *key, int mode)
{
	struct wait_bit_test_worker *worker =
		(void *)(unsigned long)key->timeout;

	atomic_inc(&worker->action_ready);
	schedule();
	atomic_inc(&worker->action_returned);
	if (signal_pending_state(mode, current))
		return -EINTR;

	return 0;
}

static int wait_bit_test_worker_thread(void *data)
{
	struct wait_bit_test_worker *worker = data;
	struct wait_queue_head *wq_head =
		bit_waitqueue(&worker->word, 0);

	wait_bit_test_init_entry(&worker->wbq_entry, &worker->word, 0,
				 worker->lock ? WQ_FLAG_EXCLUSIVE : 0);
	worker->wbq_entry.key.timeout = (unsigned long)worker;

	if (worker->lock)
		worker->ret = __wait_on_bit_lock(wq_head, &worker->wbq_entry,
						 wait_bit_test_scheduler_action,
						 TASK_INTERRUPTIBLE);
	else
		worker->ret = __wait_on_bit(wq_head, &worker->wbq_entry,
					    wait_bit_test_scheduler_action,
					    TASK_INTERRUPTIBLE);

	if (worker->lock && !worker->ret)
		clear_bit(0, &worker->word);

	atomic_inc(&worker->done);
	return 0;
}

static void wait_bit_test_queue_selection(struct kunit *test)
{
	unsigned long word = 0;
	unsigned long var = 0;

	KUNIT_EXPECT_PTR_EQ(test, bit_waitqueue(&word, 3),
			    bit_waitqueue(&word, 3));
	KUNIT_EXPECT_PTR_EQ(test, __var_waitqueue(&var),
			    __var_waitqueue(&var));
}

static void wait_bit_test_common_wait(struct kunit *test)
{
	struct wait_bit_queue_entry wbq_entry;
	struct wait_queue_head *wq_head;
	unsigned long word = 0;
	int ret;

	atomic_set(&wait_bit_action_calls, 0);
	wq_head = bit_waitqueue(&word, 2);
	wait_bit_test_init_entry(&wbq_entry, &word, 2, 0);
	ret = __wait_on_bit(wq_head, &wbq_entry,
			    wait_bit_test_unexpected_action,
			    TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 0);
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));

	set_bit(2, &word);
	wait_bit_test_init_entry(&wbq_entry, &word, 2, 0);
	ret = __wait_on_bit(wq_head, &wbq_entry, wait_bit_test_clear_action,
			    TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 1);
	KUNIT_EXPECT_FALSE(test, test_bit(2, &word));
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));

	set_bit(2, &word);
	wait_bit_test_init_entry(&wbq_entry, &word, 2, 0);
	ret = __wait_on_bit(wq_head, &wbq_entry, wait_bit_test_error_action,
			    TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 2);
	KUNIT_EXPECT_TRUE(test, test_bit(2, &word));
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));
	clear_bit(2, &word);
}

static void wait_bit_test_locked_wait(struct kunit *test)
{
	struct wait_bit_queue_entry wbq_entry;
	struct wait_queue_head *wq_head;
	unsigned long word = 0;
	int ret;

	atomic_set(&wait_bit_action_calls, 0);
	wq_head = bit_waitqueue(&word, 4);
	wait_bit_test_init_entry(&wbq_entry, &word, 4, WQ_FLAG_EXCLUSIVE);
	ret = __wait_on_bit_lock(wq_head, &wbq_entry,
				 wait_bit_test_clear_action,
				 TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 0);
	KUNIT_EXPECT_TRUE(test, test_bit(4, &word));
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));

	clear_bit(4, &word);
	set_bit(4, &word);
	wait_bit_test_init_entry(&wbq_entry, &word, 4, WQ_FLAG_EXCLUSIVE);
	ret = __wait_on_bit_lock(wq_head, &wbq_entry,
				 wait_bit_test_clear_action,
				 TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 1);
	KUNIT_EXPECT_TRUE(test, test_bit(4, &word));
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));

	wait_bit_test_init_entry(&wbq_entry, &word, 4, WQ_FLAG_EXCLUSIVE);
	ret = __wait_on_bit_lock(wq_head, &wbq_entry,
				 wait_bit_test_error_action,
				 TASK_UNINTERRUPTIBLE);

	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_EQ(test, atomic_read(&wait_bit_action_calls), 2);
	KUNIT_EXPECT_TRUE(test, test_bit(4, &word));
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));
	clear_bit(4, &word);
}

static void wait_bit_test_timeout_and_signal(struct kunit *test)
{
	unsigned long word = 1;
	int ret;

	ret = wait_on_bit_timeout(&word, 0, TASK_UNINTERRUPTIBLE, 0);
	KUNIT_EXPECT_EQ(test, ret, -EAGAIN);
	KUNIT_EXPECT_TRUE(test, test_bit(0, &word));

	allow_kernel_signal(SIGUSR1);
	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, current, 1), 0);
	ret = wait_on_bit(&word, 0, TASK_INTERRUPTIBLE);
	flush_signals(current);
	disallow_signal(SIGUSR1);

	KUNIT_EXPECT_EQ(test, ret, -EINTR);
	KUNIT_EXPECT_TRUE(test, test_bit(0, &word));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
	clear_bit(0, &word);
}

static void wait_bit_test_wake_callback(struct kunit *test)
{
	struct wait_bit_queue_entry wbq_entry;
	struct wait_bit_queue_entry mismatch_entry;
	struct wait_queue_head *wq_head;
	struct wait_bit_key key;
	unsigned long word = 0;
	unsigned long other = 0;

	wait_bit_test_init_entry(&wbq_entry, &word, 5, 0);
	key = (struct wait_bit_key) {
		.flags = &other,
		.bit_nr = 5,
	};
	KUNIT_EXPECT_EQ(test,
			wake_bit_function(&wbq_entry.wq_entry, TASK_NORMAL, 0,
					  &key), 0);

	key.flags = &word;
	key.bit_nr = 6;
	KUNIT_EXPECT_EQ(test,
			wake_bit_function(&wbq_entry.wq_entry, TASK_NORMAL, 0,
					  &key), 0);

	key.bit_nr = 5;
	set_bit(5, &word);
	KUNIT_EXPECT_EQ(test,
			wake_bit_function(&wbq_entry.wq_entry, TASK_NORMAL, 0,
					  &key), 0);
	clear_bit(5, &word);

	wq_head = bit_waitqueue(&word, 5);
	wait_bit_test_init_entry(&mismatch_entry, &other, 5, 0);
	add_wait_queue(wq_head, &mismatch_entry.wq_entry);
	wake_up_bit(&word, 5);
	KUNIT_EXPECT_FALSE(test, list_empty(&mismatch_entry.wq_entry.entry));
	remove_wait_queue(wq_head, &mismatch_entry.wq_entry);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(wq_head, &wbq_entry.wq_entry);
	wake_up_bit(&word, 5);

	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void wait_bit_test_variable_wait(struct kunit *test)
{
	struct wait_bit_queue_entry wbq_entry;
	struct wait_bit_queue_entry mismatch_entry;
	struct wait_bit_key key;
	struct wait_queue_head *wq_head;
	unsigned long var = 0;
	unsigned long other = 0;

	init_wait_var_entry(&wbq_entry, &var, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_PTR_EQ(test, wbq_entry.key.flags, &var);
	KUNIT_EXPECT_EQ(test, wbq_entry.key.bit_nr, -1);
	KUNIT_EXPECT_EQ(test, wbq_entry.wq_entry.flags, WQ_FLAG_EXCLUSIVE);
	KUNIT_EXPECT_PTR_EQ(test, wbq_entry.wq_entry.private, current);
	KUNIT_EXPECT_TRUE(test, wbq_entry.wq_entry.func);
	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));

	key = (struct wait_bit_key) {
		.flags = &var,
		.bit_nr = -1,
	};
	KUNIT_EXPECT_PTR_EQ(test, __var_wake_key(&wbq_entry.wq_entry, &key),
			    &key);
	key.flags = &other;
	KUNIT_EXPECT_PTR_EQ(test, __var_wake_key(&wbq_entry.wq_entry, &key),
			    NULL);

	wq_head = __var_waitqueue(&var);
	init_wait_var_entry(&mismatch_entry, &other, 0);
	add_wait_queue(wq_head, &mismatch_entry.wq_entry);
	wake_up_var(&var);
	KUNIT_EXPECT_FALSE(test, list_empty(&mismatch_entry.wq_entry.entry));
	remove_wait_queue(wq_head, &mismatch_entry.wq_entry);

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(wq_head, &wbq_entry.wq_entry);
	wake_up_var(&var);

	KUNIT_EXPECT_TRUE(test, list_empty(&wbq_entry.wq_entry.entry));
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static bool wait_bit_test_run_scheduler(struct kunit *test, bool lock)
{
	struct wait_bit_test_worker worker = {
		.word = 1,
		.action_ready = ATOMIC_INIT(0),
		.action_returned = ATOMIC_INIT(0),
		.done = ATOMIC_INIT(0),
		.lock = lock,
	};
	struct task_struct *task;
	bool success = false;

	task = kthread_run(wait_bit_test_worker_thread, &worker,
			   lock ? "wait-bit-lock" : "wait-bit");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start bit wait worker");
		return false;
	}

	if (!wait_bit_test_wait_for(&worker.action_ready, 1)) {
		KUNIT_FAIL(test, "bit wait worker did not enter its action");
		goto cleanup;
	}

	clear_and_wake_up_bit(0, &worker.word);
	if (!wait_bit_test_wait_for(&worker.done, 1)) {
		KUNIT_FAIL(test,
			   "%s bit wait worker did not finish: action=%d returned=%d state=%u queued=%d bit=%d",
			   lock ? "locked" : "normal",
			   atomic_read(&worker.action_ready),
			   atomic_read(&worker.action_returned),
			   READ_ONCE(task->__state),
			   !list_empty(&worker.wbq_entry.wq_entry.entry),
			   test_bit(0, &worker.word));
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, worker.ret, 0);
	KUNIT_EXPECT_FALSE(test, test_bit(0, &worker.word));
	KUNIT_EXPECT_TRUE(test, list_empty(&worker.wbq_entry.wq_entry.entry));
	success = true;

cleanup:
	clear_and_wake_up_bit(0, &worker.word);
	kthread_stop(task);
	return success;
}

static void wait_bit_test_scheduler_integration(struct kunit *test)
{
	if (!wait_bit_test_run_scheduler(test, false))
		return;
	wait_bit_test_run_scheduler(test, true);
}

static struct kunit_case wait_bit_test_cases[] = {
	KUNIT_CASE(wait_bit_test_queue_selection),
	KUNIT_CASE(wait_bit_test_common_wait),
	KUNIT_CASE(wait_bit_test_locked_wait),
	KUNIT_CASE(wait_bit_test_timeout_and_signal),
	KUNIT_CASE(wait_bit_test_wake_callback),
	KUNIT_CASE(wait_bit_test_variable_wait),
	KUNIT_CASE(wait_bit_test_scheduler_integration),
	{}
};

static struct kunit_suite wait_bit_test_suite = {
	.name = "wait_bit",
	.test_cases = wait_bit_test_cases,
};

kunit_test_suite(wait_bit_test_suite);

MODULE_DESCRIPTION("KUnit tests for bit and variable waits");
MODULE_LICENSE("GPL");
