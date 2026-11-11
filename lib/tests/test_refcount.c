// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/jump_label.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/refcount.h>
#include <linux/spinlock.h>

#define REFCOUNT_TEST_WORKERS 4
#define REFCOUNT_TEST_ITERATIONS 5000
#define REFCOUNT_TEST_TIMEOUT_MS 30000

static void refcount_test_basic(struct kunit *test)
{
	refcount_t ref = REFCOUNT_INIT(1);
	int old = -1;

	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	KUNIT_EXPECT_TRUE(test, __refcount_add_not_zero(2, &ref, &old));
	KUNIT_EXPECT_EQ(test, old, 1);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 3);

	KUNIT_EXPECT_TRUE(test, refcount_inc_not_zero(&ref));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 4);

	old = -1;
	__refcount_add(2, &ref, &old);
	KUNIT_EXPECT_EQ(test, old, 4);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 6);
	old = -1;
	__refcount_dec(&ref, &old);
	KUNIT_EXPECT_EQ(test, old, 6);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 5);

	KUNIT_EXPECT_FALSE(test, refcount_dec_and_test(&ref));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 4);
	KUNIT_EXPECT_FALSE(test, refcount_sub_and_test(3, &ref));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	KUNIT_EXPECT_TRUE(test, refcount_dec_and_test(&ref));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);

	old = 7;
	KUNIT_EXPECT_FALSE(test, refcount_inc_not_zero(&ref));
	KUNIT_EXPECT_EQ(test, old, 7);
	KUNIT_EXPECT_FALSE(test, __refcount_add_not_zero(1, &ref, &old));
	KUNIT_EXPECT_EQ(test, old, 0);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);
}

static void refcount_test_limited_acquire(struct kunit *test)
{
	refcount_t ref = REFCOUNT_INIT(5);
	int old = -1;
	bool added;

	added = __refcount_add_not_zero_limited_acquire(2, &ref, &old, 6);
	KUNIT_EXPECT_FALSE(test, added);
	KUNIT_EXPECT_EQ(test, old, 5);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 5);

	added = __refcount_add_not_zero_limited_acquire(1, &ref, &old, 6);
	KUNIT_EXPECT_TRUE(test, added);
	KUNIT_EXPECT_EQ(test, old, 5);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 6);

	added = __refcount_add_not_zero_limited_acquire(1, &ref, &old, 6);
	KUNIT_EXPECT_FALSE(test, added);
	KUNIT_EXPECT_EQ(test, old, 6);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 6);

	KUNIT_EXPECT_TRUE(test,
			  __refcount_inc_not_zero_limited_acquire(&ref, &old,
								  INT_MAX));
	KUNIT_EXPECT_EQ(test, old, 6);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 7);
}

static void refcount_test_exact_zero(struct kunit *test)
{
	refcount_t ref = REFCOUNT_INIT(3);
	int old = -1;

	KUNIT_EXPECT_FALSE(test, __refcount_dec_and_test(&ref, &old));
	KUNIT_EXPECT_EQ(test, old, 3);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 2);

	KUNIT_EXPECT_FALSE(test, __refcount_sub_and_test(1, &ref, &old));
	KUNIT_EXPECT_EQ(test, old, 2);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);

	KUNIT_EXPECT_TRUE(test, __refcount_dec_and_test(&ref, &old));
	KUNIT_EXPECT_EQ(test, old, 1);
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);
}

static void refcount_test_special_decrements(struct kunit *test)
{
	refcount_t one = REFCOUNT_INIT(1);
	refcount_t two = REFCOUNT_INIT(2);
	refcount_t saturated = REFCOUNT_INIT(REFCOUNT_SATURATED);

	KUNIT_EXPECT_TRUE(test, refcount_dec_if_one(&one));
	KUNIT_EXPECT_EQ(test, refcount_read(&one), 0);
	KUNIT_EXPECT_FALSE(test, refcount_dec_if_one(&two));
	KUNIT_EXPECT_EQ(test, refcount_read(&two), 2);

	KUNIT_EXPECT_TRUE(test, refcount_dec_not_one(&two));
	KUNIT_EXPECT_EQ(test, refcount_read(&two), 1);
	KUNIT_EXPECT_FALSE(test, refcount_dec_not_one(&two));
	KUNIT_EXPECT_EQ(test, refcount_read(&two), 1);

	KUNIT_EXPECT_TRUE(test, refcount_dec_not_one(&saturated));
	KUNIT_EXPECT_EQ(test, refcount_read(&saturated), REFCOUNT_SATURATED);
	KUNIT_EXPECT_FALSE(test, refcount_dec_if_one(&saturated));
	KUNIT_EXPECT_EQ(test, refcount_read(&saturated), REFCOUNT_SATURATED);
}

static void refcount_test_saturation(struct kunit *test)
{
	refcount_t direct = REFCOUNT_INIT(7);
	refcount_t add_overflow = REFCOUNT_INIT(REFCOUNT_MAX);
	refcount_t add_not_zero_overflow = REFCOUNT_INIT(REFCOUNT_MAX);
	refcount_t add_uaf = REFCOUNT_INIT(0);
	refcount_t sub_uaf = REFCOUNT_INIT(0);
	refcount_t dec_leak = REFCOUNT_INIT(1);
	refcount_t dec_not_one_uaf = REFCOUNT_INIT(0);

	kunit_warning_suppress(test) {
		refcount_warn_saturate(&direct, REFCOUNT_ADD_OVF);
		KUNIT_EXPECT_EQ(test, refcount_read(&direct),
				REFCOUNT_SATURATED);

		refcount_add(1, &add_overflow);
		KUNIT_EXPECT_EQ(test, refcount_read(&add_overflow),
				REFCOUNT_SATURATED);

		KUNIT_EXPECT_TRUE(test,
				  refcount_add_not_zero(1, &add_not_zero_overflow));
		KUNIT_EXPECT_EQ(test, refcount_read(&add_not_zero_overflow),
				REFCOUNT_SATURATED);

		refcount_inc(&add_uaf);
		KUNIT_EXPECT_EQ(test, refcount_read(&add_uaf),
				REFCOUNT_SATURATED);

		KUNIT_EXPECT_FALSE(test, refcount_sub_and_test(1, &sub_uaf));
		KUNIT_EXPECT_EQ(test, refcount_read(&sub_uaf),
				REFCOUNT_SATURATED);

		refcount_dec(&dec_leak);
		KUNIT_EXPECT_EQ(test, refcount_read(&dec_leak),
				REFCOUNT_SATURATED);

		KUNIT_EXPECT_TRUE(test, refcount_dec_not_one(&dec_not_one_uaf));
		KUNIT_EXPECT_EQ(test, refcount_read(&dec_not_one_uaf), 0);

		/*
		 * The direct ADD_OVF warning and the later ADD_OVF transition use
		 * one WARN_ONCE site.  KUnit counts both calls.  The remaining
		 * operations use four saturation sites plus the dec_not_one
		 * warning site.
		 */
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 7);
	}
}

static bool refcount_try_acquire_spinlock(spinlock_t *lock)
{
	if (!spin_trylock(lock))
		return false;

	spin_unlock(lock);
	return true;
}

static void refcount_test_lock_wrappers(struct kunit *test)
{
	/* Verify the mutex conditional-acquire contract. */
	DEFINE_MUTEX(mutex);
	/* Verify the plain and IRQ-safe spinlock contracts. */
	spinlock_t spinlock;
	refcount_t ref;
	unsigned long flags = 0;
	bool irq_disabled;
	bool locked;

	spin_lock_init(&spinlock);

	ref = (refcount_t)REFCOUNT_INIT(2);
	KUNIT_EXPECT_FALSE(test, refcount_dec_and_mutex_lock(&ref, &mutex));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&mutex));

	KUNIT_EXPECT_TRUE(test, refcount_dec_and_mutex_lock(&ref, &mutex));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);
	locked = mutex_is_locked(&mutex);
	mutex_unlock(&mutex);
	KUNIT_EXPECT_TRUE(test, locked);

	ref = (refcount_t)REFCOUNT_INIT(2);
	KUNIT_EXPECT_TRUE(test, refcount_try_acquire_spinlock(&spinlock));
	KUNIT_EXPECT_FALSE(test, refcount_dec_and_lock(&ref, &spinlock));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	KUNIT_EXPECT_TRUE(test, refcount_try_acquire_spinlock(&spinlock));

	KUNIT_EXPECT_TRUE(test, refcount_dec_and_lock(&ref, &spinlock));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);
	locked = refcount_try_acquire_spinlock(&spinlock);
	if (IS_ENABLED(CONFIG_SMP))
		KUNIT_EXPECT_FALSE(test, locked);
	spin_unlock(&spinlock);

	ref = (refcount_t)REFCOUNT_INIT(2);
	KUNIT_EXPECT_TRUE(test, refcount_try_acquire_spinlock(&spinlock));
	KUNIT_EXPECT_FALSE(test,
			   refcount_dec_and_lock_irqsave(&ref, &spinlock,
							 &flags));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	KUNIT_EXPECT_TRUE(test, refcount_try_acquire_spinlock(&spinlock));

	KUNIT_EXPECT_TRUE(test,
			  refcount_dec_and_lock_irqsave(&ref, &spinlock,
							&flags));
	KUNIT_EXPECT_EQ(test, refcount_read(&ref), 0);
	locked = refcount_try_acquire_spinlock(&spinlock);
	if (IS_ENABLED(CONFIG_SMP))
		KUNIT_EXPECT_FALSE(test, locked);
	irq_disabled = irqs_disabled();
	spin_unlock_irqrestore(&spinlock, flags);
	KUNIT_EXPECT_TRUE(test, irq_disabled);
}

struct refcount_worker {
	refcount_t *ref;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *start;
	atomic64_t *operations;
};

static int refcount_worker(void *data)
{
	struct refcount_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < REFCOUNT_TEST_ITERATIONS; i++) {
		refcount_inc(worker->ref);
		refcount_dec(worker->ref);
		atomic64_inc(worker->operations);
	}
	atomic_inc(worker->done);

	return 0;
}

static void refcount_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[REFCOUNT_TEST_WORKERS] = {};
	struct refcount_worker workers[REFCOUNT_TEST_WORKERS];
	refcount_t ref = REFCOUNT_INIT(1);
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic64_t operations = ATOMIC64_INIT(0);
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	for (i = 0; i < REFCOUNT_TEST_WORKERS; i++) {
		workers[i].ref = &ref;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].start = &start;
		workers[i].operations = &operations;
		tasks[i] = kthread_run(refcount_worker, &workers[i],
				       "refcount-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start refcount worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(REFCOUNT_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < REFCOUNT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != REFCOUNT_TEST_WORKERS) {
		KUNIT_FAIL(test, "refcount workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < REFCOUNT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != REFCOUNT_TEST_WORKERS)
		KUNIT_FAIL(test, "refcount workers did not finish");
cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	if (started == REFCOUNT_TEST_WORKERS &&
	    atomic_read(&ready) == REFCOUNT_TEST_WORKERS &&
	    atomic_read(&done) == REFCOUNT_TEST_WORKERS) {
		KUNIT_EXPECT_EQ(test, atomic64_read(&operations),
				(s64)REFCOUNT_TEST_WORKERS *
					REFCOUNT_TEST_ITERATIONS);
		KUNIT_EXPECT_EQ(test, refcount_read(&ref), 1);
	}
}

static struct kunit_case refcount_test_cases[] = {
	KUNIT_CASE(refcount_test_basic),
	KUNIT_CASE(refcount_test_limited_acquire),
	KUNIT_CASE(refcount_test_exact_zero),
	KUNIT_CASE(refcount_test_special_decrements),
	KUNIT_CASE(refcount_test_saturation),
	KUNIT_CASE(refcount_test_lock_wrappers),
	KUNIT_CASE(refcount_test_concurrent),
	{}
};

static struct kunit_suite refcount_test_suite = {
	.name = "refcount",
	.test_cases = refcount_test_cases,
};

kunit_test_suites(&refcount_test_suite);

MODULE_DESCRIPTION("refcount_t KUnit test suite");
MODULE_LICENSE("GPL");
