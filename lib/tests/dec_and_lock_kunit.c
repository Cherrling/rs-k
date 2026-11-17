// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#define DEC_AND_LOCK_TEST_WORKERS 4
#define DEC_AND_LOCK_TEST_ITERATIONS 10000
#define DEC_AND_LOCK_TEST_TIMEOUT_MS 30000

static bool dec_and_lock_test_spin_unlocked(spinlock_t *lock)
{
	bool acquired = spin_trylock(lock);

	if (acquired)
		spin_unlock(lock);
	return acquired;
}

static bool dec_and_lock_test_spin_held(spinlock_t *lock)
{
#ifdef CONFIG_PREEMPT_RT
	return rt_mutex_base_is_locked(&lock->lock);
#else
	return raw_spin_is_locked(&lock->rlock);
#endif
}

static void dec_and_lock_test_spinlock(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	spinlock_t lock; /* Lock under test. */

	spin_lock_init(&lock);
	KUNIT_EXPECT_FALSE(test, atomic_dec_and_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_TRUE(test, dec_and_lock_test_spin_unlocked(&lock));

	KUNIT_EXPECT_TRUE(test, atomic_dec_and_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, dec_and_lock_test_spin_held(&lock));
	spin_unlock(&lock);
}

static void dec_and_lock_test_spinlock_irqsave(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	spinlock_t lock; /* Lock under test. */
	unsigned long flags = 0;
	bool irq_disabled;

	spin_lock_init(&lock);
	KUNIT_EXPECT_FALSE(test,
			   atomic_dec_and_lock_irqsave(&count, &lock, flags));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_TRUE(test, dec_and_lock_test_spin_unlocked(&lock));

	KUNIT_EXPECT_TRUE(test,
			  atomic_dec_and_lock_irqsave(&count, &lock, flags));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, dec_and_lock_test_spin_held(&lock));
	irq_disabled = irqs_disabled();
	spin_unlock_irqrestore(&lock, flags);
	KUNIT_EXPECT_TRUE(test, irq_disabled);
}

static void dec_and_lock_test_raw_spinlock(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	raw_spinlock_t lock; /* Lock under test. */

	raw_spin_lock_init(&lock);
	KUNIT_EXPECT_FALSE(test, atomic_dec_and_raw_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_FALSE(test, raw_spin_is_locked(&lock));

	KUNIT_EXPECT_TRUE(test, atomic_dec_and_raw_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, raw_spin_is_locked(&lock));
	raw_spin_unlock(&lock);
}

static void dec_and_lock_test_raw_spinlock_irqsave(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	raw_spinlock_t lock; /* Lock under test. */
	unsigned long flags = 0;
	bool irq_disabled;

	raw_spin_lock_init(&lock);
	KUNIT_EXPECT_FALSE(test,
			   atomic_dec_and_raw_lock_irqsave(&count, &lock, flags));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_FALSE(test, raw_spin_is_locked(&lock));

	KUNIT_EXPECT_TRUE(test,
			  atomic_dec_and_raw_lock_irqsave(&count, &lock, flags));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, raw_spin_is_locked(&lock));
	irq_disabled = irqs_disabled();
	raw_spin_unlock_irqrestore(&lock, flags);
	KUNIT_EXPECT_TRUE(test, irq_disabled);
}

static void dec_and_lock_test_mutex(struct kunit *test)
{
	atomic_t count = ATOMIC_INIT(2);
	struct mutex lock; /* Lock under test. */

	mutex_init(&lock);
	KUNIT_EXPECT_FALSE(test, atomic_dec_and_mutex_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 1);
	KUNIT_EXPECT_FALSE(test, mutex_is_locked(&lock));

	KUNIT_EXPECT_TRUE(test, atomic_dec_and_mutex_lock(&count, &lock));
	KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
	KUNIT_EXPECT_TRUE(test, mutex_is_locked(&lock));
	KUNIT_EXPECT_PTR_EQ(test, (void *)mutex_get_owner(&lock), current);
	mutex_unlock(&lock);
}

struct dec_and_lock_worker {
	atomic_t *count;
	spinlock_t *lock; /* Lock under test. */
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *winners;
};

static int dec_and_lock_worker_fn(void *data)
{
	struct dec_and_lock_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < DEC_AND_LOCK_TEST_ITERATIONS; i++) {
		if (atomic_dec_and_lock(worker->count, worker->lock)) {
			atomic_inc(worker->winners);
			spin_unlock(worker->lock);
		}
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void dec_and_lock_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[DEC_AND_LOCK_TEST_WORKERS] = {};
	struct dec_and_lock_worker workers[DEC_AND_LOCK_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t winners = ATOMIC_INIT(0);
	atomic_t count;
	spinlock_t lock; /* Lock under test. */
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	atomic_set(&count, DEC_AND_LOCK_TEST_WORKERS *
			     DEC_AND_LOCK_TEST_ITERATIONS);
	spin_lock_init(&lock);
	for (i = 0; i < DEC_AND_LOCK_TEST_WORKERS; i++) {
		workers[i].count = &count;
		workers[i].lock = &lock;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].winners = &winners;
		tasks[i] = kthread_run(dec_and_lock_worker_fn, &workers[i],
				       "dec-and-lock-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start dec-and-lock worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(DEC_AND_LOCK_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < DEC_AND_LOCK_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != DEC_AND_LOCK_TEST_WORKERS) {
		KUNIT_FAIL(test, "dec-and-lock workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < DEC_AND_LOCK_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != DEC_AND_LOCK_TEST_WORKERS)
		KUNIT_FAIL(test, "dec-and-lock workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	if (started == DEC_AND_LOCK_TEST_WORKERS &&
	    atomic_read(&done) == DEC_AND_LOCK_TEST_WORKERS) {
		KUNIT_EXPECT_EQ(test, atomic_read(&count), 0);
		KUNIT_EXPECT_EQ(test, atomic_read(&winners), 1);
		KUNIT_EXPECT_TRUE(test, dec_and_lock_test_spin_unlocked(&lock));
	}
}

static struct kunit_case dec_and_lock_test_cases[] = {
	KUNIT_CASE(dec_and_lock_test_spinlock),
	KUNIT_CASE(dec_and_lock_test_spinlock_irqsave),
	KUNIT_CASE(dec_and_lock_test_raw_spinlock),
	KUNIT_CASE(dec_and_lock_test_raw_spinlock_irqsave),
	KUNIT_CASE(dec_and_lock_test_mutex),
	KUNIT_CASE(dec_and_lock_test_concurrent),
	{}
};

static struct kunit_suite dec_and_lock_test_suite = {
	.name = "dec_and_lock",
	.test_cases = dec_and_lock_test_cases,
};

kunit_test_suites(&dec_and_lock_test_suite);

MODULE_DESCRIPTION("Decrement-and-lock KUnit test suite");
MODULE_LICENSE("GPL");
