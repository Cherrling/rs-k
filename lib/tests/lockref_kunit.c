// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/lockref.h>
#include <linux/spinlock.h>

#define LOCKREF_TEST_WORKERS 4
#define LOCKREF_TEST_ITERATIONS 10000
#define LOCKREF_TEST_TIMEOUT_MS 30000

static void lockref_test_transitions(struct kunit *test)
{
	struct lockref ref;

	lockref_init(&ref);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);

	lockref_get(&ref);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 2);
	KUNIT_EXPECT_TRUE(test, lockref_get_not_zero(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 3);
	KUNIT_EXPECT_TRUE(test, lockref_get_not_dead(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 4);

	KUNIT_EXPECT_TRUE(test, lockref_put_or_lock(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 3);
	KUNIT_EXPECT_TRUE(test, lockref_put_or_lock(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 2);
	KUNIT_EXPECT_TRUE(test, lockref_put_or_lock(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
	KUNIT_EXPECT_FALSE(test, lockref_put_or_lock(&ref));
	spin_unlock(&ref.lock);

#if USE_CMPXCHG_LOCKREF
	KUNIT_EXPECT_EQ(test, lockref_put_return(&ref), 0);
	KUNIT_EXPECT_EQ(test, lockref_put_return(&ref), -1);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 0);
#else
	/*
	 * Without a packed lock and count, put_return() cannot safely change
	 * the count and returns -1.
	 */
	KUNIT_EXPECT_EQ(test, lockref_put_return(&ref), -1);
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
	spin_lock(&ref.lock);
	WRITE_ONCE(ref.count, 0);
	spin_unlock(&ref.lock);
#endif

	KUNIT_EXPECT_FALSE(test, lockref_get_not_zero(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 0);
	KUNIT_EXPECT_TRUE(test, lockref_get_not_dead(&ref));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
}

static void lockref_test_put_or_lock(struct kunit *test)
{
	struct lockref ref;

	lockref_init(&ref);
	KUNIT_EXPECT_FALSE(test, lockref_put_or_lock(&ref));
	KUNIT_EXPECT_TRUE(test, spin_is_locked(&ref.lock));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
	spin_unlock(&ref.lock);

	lockref_get(&ref);
	KUNIT_EXPECT_TRUE(test, lockref_put_or_lock(&ref));
	KUNIT_EXPECT_FALSE(test, spin_is_locked(&ref.lock));
	KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
}

static void lockref_test_dead(struct kunit *test)
{
	struct lockref ref;

	lockref_init(&ref);
	spin_lock(&ref.lock);
	lockref_mark_dead(&ref);
	spin_unlock(&ref.lock);

	KUNIT_EXPECT_TRUE(test, lockref_is_dead(&ref));
	KUNIT_EXPECT_FALSE(test, lockref_get_not_dead(&ref));
	KUNIT_EXPECT_FALSE(test, lockref_get_not_zero(&ref));
	KUNIT_EXPECT_EQ(test, lockref_put_return(&ref), -1);
	KUNIT_EXPECT_TRUE(test, lockref_is_dead(&ref));
}

struct lockref_worker {
	struct lockref *ref;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static int lockref_worker_fn(void *data)
{
	struct lockref_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < LOCKREF_TEST_ITERATIONS; i++) {
		bool released;

		lockref_get(worker->ref);
		released = lockref_put_or_lock(worker->ref);
		if (!released) {
			spin_unlock(&worker->ref->lock);
			atomic_inc(worker->errors);
		}

		if (!lockref_get_not_zero(worker->ref))
			atomic_inc(worker->errors);
		else if (!lockref_put_or_lock(worker->ref)) {
			spin_unlock(&worker->ref->lock);
			atomic_inc(worker->errors);
		}
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void lockref_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[LOCKREF_TEST_WORKERS] = {};
	struct lockref_worker workers[LOCKREF_TEST_WORKERS];
	struct lockref ref;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	lockref_init(&ref);
	for (i = 0; i < LOCKREF_TEST_WORKERS; i++) {
		workers[i].ref = &ref;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(lockref_worker_fn, &workers[i],
				       "lockref-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start lockref worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(LOCKREF_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < LOCKREF_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != LOCKREF_TEST_WORKERS) {
		KUNIT_FAIL(test, "lockref workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < LOCKREF_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != LOCKREF_TEST_WORKERS)
		KUNIT_FAIL(test, "lockref workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	if (started == LOCKREF_TEST_WORKERS &&
	    atomic_read(&done) == LOCKREF_TEST_WORKERS) {
		KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
		KUNIT_EXPECT_EQ(test, READ_ONCE(ref.count), 1);
		KUNIT_EXPECT_FALSE(test, spin_is_locked(&ref.lock));
	}
}

static struct kunit_case lockref_test_cases[] = {
	KUNIT_CASE(lockref_test_transitions),
	KUNIT_CASE(lockref_test_put_or_lock),
	KUNIT_CASE(lockref_test_dead),
	KUNIT_CASE(lockref_test_concurrent),
	{}
};

static struct kunit_suite lockref_test_suite = {
	.name = "lockref",
	.test_cases = lockref_test_cases,
};

kunit_test_suites(&lockref_test_suite);

MODULE_DESCRIPTION("lockref KUnit test suite");
MODULE_LICENSE("GPL");
