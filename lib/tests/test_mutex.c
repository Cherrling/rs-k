// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/mutex-rust.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/ww_mutex.h>

#define MUTEX_TEST_TIMEOUT_MS 30000
#define MUTEX_TEST_CONTENDERS 4
#define MUTEX_TEST_ITERATIONS 200

struct mutex_test_worker {
	struct mutex *lock; /* Mutex under test. */
	atomic_t *ready;
	atomic_t *done;
	atomic_t *wake_count;
	int *wake_order;
	int index;
	int signal;
	int ret;
	unsigned long owner_after_lock;
	bool killable;
	bool uninterruptible;
};

struct mutex_test_contender {
	struct mutex *lock; /* Mutex under test. */
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *active;
	atomic_t *max_active;
	atomic_t *operations;
	atomic_t *errors;
};

static DEFINE_WW_CLASS(mutex_test_ww_class);

static bool mutex_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(MUTEX_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int mutex_test_waiter_count(struct mutex *lock)
{
	struct mutex_waiter *first;
	struct list_head *cursor;
	unsigned long flags;
	int count = 0;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	first = lock->first_waiter;
	if (first) {
		count = 1;
		for (cursor = first->list.next; cursor != &first->list;
		     cursor = cursor->next)
			count++;
	}
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
	return count;
}

static bool mutex_test_wait_for_waiters(struct mutex *lock, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(MUTEX_TEST_TIMEOUT_MS);

	while (time_before(jiffies, deadline)) {
		int count = mutex_test_waiter_count(lock);

		if (count >= target)
			return true;
		cond_resched();
	}

	return false;
}

static void mutex_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int mutex_test_worker_thread(void *data)
{
	struct mutex_test_worker *worker = data;

	if (worker->signal)
		allow_kernel_signal(worker->signal);

	atomic_inc(worker->ready);
	if (worker->uninterruptible) {
		mutex_lock(worker->lock);
		worker->ret = 0;
	} else if (worker->killable) {
		worker->ret = mutex_lock_killable(worker->lock);
	} else {
		worker->ret = mutex_lock_interruptible(worker->lock);
	}

	if (worker->signal) {
		flush_signals(current);
		disallow_signal(worker->signal);
	}

	if (!worker->ret) {
		int slot;

		worker->owner_after_lock = mutex_get_owner(worker->lock);
		if (worker->wake_order) {
			slot = atomic_inc_return(worker->wake_count) - 1;
			worker->wake_order[slot] = worker->index;
		}
		cond_resched();
		mutex_unlock(worker->lock);
	}

	atomic_inc(worker->done);
	mutex_test_idle_until_stopped();
	return 0;
}

static int mutex_test_contender_thread(void *data)
{
	struct mutex_test_contender *contender = data;
	int i;

	atomic_inc(contender->ready);
	while (!atomic_read(contender->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < MUTEX_TEST_ITERATIONS; i++) {
		int active;
		int max;

		if (mutex_lock_interruptible(contender->lock)) {
			atomic_inc(contender->errors);
			break;
		}

		active = atomic_inc_return(contender->active);
		max = atomic_read(contender->max_active);
		while (active > max &&
		       atomic_cmpxchg(contender->max_active, max, active) != max)
			max = atomic_read(contender->max_active);
		cond_resched();
		atomic_dec(contender->active);
		atomic_inc(contender->operations);
		mutex_unlock(contender->lock);
	}

	atomic_inc(contender->done);
	mutex_test_idle_until_stopped();
	return 0;
}

static void mutex_test_stop_workers(struct task_struct **tasks, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void mutex_test_fast_paths(struct kunit *test)
{
	struct mutex lock; /* Mutex under test. */
	unsigned long owner;

	mutex_init(&lock);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock), 0UL);

	KUNIT_EXPECT_TRUE(test, mutex_trylock(&lock));
	KUNIT_EXPECT_TRUE(test, mutex_is_locked(&lock));
	owner = mutex_get_owner(&lock);
	KUNIT_EXPECT_PTR_EQ(test, (void *)owner, current);
	KUNIT_EXPECT_FALSE(test, mutex_trylock(&lock));
	KUNIT_EXPECT_EQ(test, mutex_test_waiter_count(&lock), 0);
	mutex_unlock(&lock);

	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock), 0UL);

	mutex_lock(&lock);
	KUNIT_EXPECT_TRUE(test, mutex_is_locked(&lock));
	mutex_unlock(&lock);
	mutex_destroy(&lock);
}

static void mutex_test_signal_worker(struct kunit *test, int signal,
				     bool killable)
{
	struct mutex_test_worker worker = {};
	struct task_struct *task;
	struct mutex lock; /* Mutex under test. */
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);

	mutex_init(&lock);
	mutex_lock(&lock);
	worker.lock = &lock;
	worker.ready = &ready;
	worker.done = &done;
	worker.signal = signal;
	worker.killable = killable;
	task = kthread_run(mutex_test_worker_thread, &worker,
			   "mutex-signal");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start signal worker");
		goto cleanup;
	}

	if (!mutex_test_wait_for(&ready, 1) ||
	    !mutex_test_wait_for_waiters(&lock, 1)) {
		KUNIT_FAIL(test, "signal worker did not queue");
		goto cleanup;
	}
	if (send_sig(signal, task, 1)) {
		KUNIT_FAIL(test, "failed to signal mutex waiter");
		goto cleanup;
	}
	if (!mutex_test_wait_for(&done, 1)) {
		KUNIT_FAIL(test, "signal worker did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, worker.ret, -EINTR);
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock), (unsigned long)current);
	KUNIT_EXPECT_EQ(test, mutex_test_waiter_count(&lock), 0);

cleanup:
	mutex_unlock(&lock);
	mutex_test_stop_workers(&task, 1);
	mutex_destroy(&lock);
}

static void mutex_test_interruptible_and_killable(struct kunit *test)
{
	mutex_test_signal_worker(test, SIGUSR1, false);
	mutex_test_signal_worker(test, SIGKILL, true);
}

static void mutex_test_handoff(struct kunit *test)
{
	struct mutex_test_worker worker = {};
	struct task_struct *task = NULL;
	struct mutex lock; /* Mutex under test. */
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	bool unlocked = false;

	mutex_init(&lock);
	mutex_lock(&lock);
	worker.lock = &lock;
	worker.ready = &ready;
	worker.done = &done;
	worker.uninterruptible = true;
	task = kthread_run(mutex_test_worker_thread, &worker,
			   "mutex-handoff");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start handoff worker");
		goto cleanup;
	}

	if (!mutex_test_wait_for(&ready, 1) ||
	    !mutex_test_wait_for_waiters(&lock, 1)) {
		KUNIT_FAIL(test, "handoff worker did not queue");
		goto cleanup;
	}

	atomic_long_or(MUTEX_FLAG_HANDOFF, &lock.owner);
	mutex_unlock(&lock);
	unlocked = true;
	if (!mutex_test_wait_for(&done, 1)) {
		KUNIT_FAIL(test, "handoff worker did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, worker.ret, 0);
	KUNIT_EXPECT_PTR_EQ(test, (void *)worker.owner_after_lock, task);
	KUNIT_EXPECT_EQ(test, mutex_test_waiter_count(&lock), 0);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock), 0UL);

cleanup:
	if (!unlocked)
		mutex_unlock(&lock);
	mutex_test_stop_workers(&task, 1);
	mutex_destroy(&lock);
}

static void mutex_test_fifo_order(struct kunit *test)
{
	struct mutex_test_worker workers[3] = {};
	struct task_struct *tasks[3] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int wake_order[3] = {};
	struct mutex lock; /* Mutex under test. */
	int started = 0;
	bool unlocked = false;
	int i;

	mutex_init(&lock);
	mutex_lock(&lock);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		workers[i].lock = &lock;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].wake_count = &wake_count;
		workers[i].wake_order = wake_order;
		workers[i].index = i;
		tasks[i] = kthread_run(mutex_test_worker_thread,
				       &workers[i], "mutex-fifo-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start FIFO worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!mutex_test_wait_for(&ready, i + 1) ||
		    !mutex_test_wait_for_waiters(&lock, i + 1)) {
			KUNIT_FAIL(test, "FIFO worker did not queue");
			goto cleanup;
		}
	}

	mutex_unlock(&lock);
	unlocked = true;
	if (!mutex_test_wait_for(&done, ARRAY_SIZE(workers))) {
		KUNIT_FAIL(test, "FIFO workers did not finish");
		goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		KUNIT_EXPECT_EQ(test, workers[i].ret, 0);
		KUNIT_EXPECT_EQ(test, wake_order[i], i);
		KUNIT_EXPECT_PTR_EQ(test,
				    (void *)workers[i].owner_after_lock,
				    tasks[i]);
	}
	KUNIT_EXPECT_EQ(test, mutex_test_waiter_count(&lock), 0);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));

cleanup:
	if (!unlocked)
		mutex_unlock(&lock);
	mutex_test_stop_workers(tasks, started);
	mutex_destroy(&lock);
}

static void mutex_test_ww_integration(struct kunit *test)
{
	struct ww_acquire_ctx ctx;
	struct ww_mutex lock;

	ww_mutex_init(&lock, &mutex_test_ww_class);
	ww_acquire_init(&ctx, &mutex_test_ww_class);
	KUNIT_EXPECT_EQ(test, ww_mutex_trylock(&lock, &ctx), 1);
	KUNIT_EXPECT_TRUE(test, ww_mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock.base),
			(unsigned long)current);
	ww_mutex_unlock(&lock);
	ww_acquire_fini(&ctx);

	KUNIT_EXPECT_FALSE(test, ww_mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock.base), 0UL);
	ww_mutex_destroy(&lock);
}

static void mutex_test_atomic_dec_and_lock(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	struct mutex lock; /* Mutex under test. */

	mutex_init(&lock);
	KUNIT_EXPECT_FALSE(test, atomic_dec_and_mutex_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));

	KUNIT_EXPECT_TRUE(test, atomic_dec_and_mutex_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, mutex_is_locked(&lock));
	KUNIT_EXPECT_EQ(test, mutex_get_owner(&lock), (unsigned long)current);
	mutex_unlock(&lock);
	mutex_destroy(&lock);
}

static void mutex_test_concurrent_ownership(struct kunit *test)
{
	struct mutex_test_contender contenders[MUTEX_TEST_CONTENDERS] = {};
	struct task_struct *tasks[MUTEX_TEST_CONTENDERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t active = ATOMIC_INIT(0);
	atomic_t max_active = ATOMIC_INIT(0);
	atomic_t operations = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct mutex lock; /* Mutex under test. */
	int started = 0;
	int i;

	mutex_init(&lock);
	for (i = 0; i < ARRAY_SIZE(contenders); i++) {
		contenders[i].lock = &lock;
		contenders[i].ready = &ready;
		contenders[i].start = &start;
		contenders[i].done = &done;
		contenders[i].active = &active;
		contenders[i].max_active = &max_active;
		contenders[i].operations = &operations;
		contenders[i].errors = &errors;
		tasks[i] = kthread_run(mutex_test_contender_thread,
				       &contenders[i], "mutex-contend-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start mutex contender");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!mutex_test_wait_for(&ready, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "mutex contenders did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!mutex_test_wait_for(&done, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "mutex contenders did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&max_active), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&active), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&operations),
			MUTEX_TEST_CONTENDERS * MUTEX_TEST_ITERATIONS);

cleanup:
	mutex_test_stop_workers(tasks, started);
	mutex_destroy(&lock);
}

static struct kunit_case mutex_test_cases[] = {
	KUNIT_CASE(mutex_test_fast_paths),
	KUNIT_CASE(mutex_test_interruptible_and_killable),
	KUNIT_CASE(mutex_test_handoff),
	KUNIT_CASE(mutex_test_fifo_order),
	KUNIT_CASE(mutex_test_ww_integration),
	KUNIT_CASE(mutex_test_atomic_dec_and_lock),
	KUNIT_CASE(mutex_test_concurrent_ownership),
	{}
};

static struct kunit_suite mutex_test_suite = {
	.name = "mutex",
	.test_cases = mutex_test_cases,
};

kunit_test_suite(mutex_test_suite);

MODULE_DESCRIPTION("KUnit tests for mutexes");
MODULE_LICENSE("GPL");
