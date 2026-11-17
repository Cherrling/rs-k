// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>

#define KREF_TEST_WORKERS 4
#define KREF_TEST_ITERATIONS 10000
#define KREF_TEST_TIMEOUT_MS 30000

struct kref_test_object {
	struct kref ref;
	struct mutex mutex; /* protects the mutex release variant */
	spinlock_t lock; /* protects the spinlock release variant */
	atomic_t releases;
	bool release_mutex;
	bool release_lock;
};

static void kref_test_release(struct kref *ref)
{
	struct kref_test_object *object = container_of(ref,
						       struct kref_test_object,
						       ref);

	if (object->release_mutex) {
		lockdep_assert_held(&object->mutex);
		mutex_unlock(&object->mutex);
	}
	if (object->release_lock) {
		lockdep_assert_held(&object->lock);
		spin_unlock(&object->lock);
	}
	atomic_inc(&object->releases);
}

static void kref_test_init_object(struct kref_test_object *object)
{
	mutex_init(&object->mutex);
	spin_lock_init(&object->lock);
	atomic_set(&object->releases, 0);
	object->release_mutex = false;
	object->release_lock = false;
	kref_init(&object->ref);
}

static bool kref_test_mutex_unlocked(struct mutex *mutex)
{
	bool locked = mutex_trylock(mutex);

	if (locked)
		mutex_unlock(mutex);
	return locked;
}

static bool kref_test_spin_unlocked(spinlock_t *lock)
{
	bool locked = spin_trylock(lock);

	if (locked)
		spin_unlock(lock);
	return locked;
}

static void kref_test_lifecycle(struct kunit *test)
{
	struct kref_test_object object;

	kref_test_init_object(&object);
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 1U);

	kref_get(&object.ref);
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 2U);
	KUNIT_EXPECT_TRUE(test, kref_get_unless_zero(&object.ref));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 3U);

	KUNIT_EXPECT_FALSE(test, kref_put(&object.ref, kref_test_release));
	KUNIT_EXPECT_FALSE(test, kref_put(&object.ref, kref_test_release));
	KUNIT_EXPECT_TRUE(test, kref_put(&object.ref, kref_test_release));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 1);

	KUNIT_EXPECT_FALSE(test, kref_get_unless_zero(&object.ref));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 1);
}

static void kref_test_put_mutex(struct kunit *test)
{
	struct kref_test_object object;

	kref_test_init_object(&object);
	object.release_mutex = true;
	kref_get(&object.ref);

	KUNIT_EXPECT_FALSE(test, kref_put_mutex(&object.ref, kref_test_release,
						&object.mutex));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 1U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 0);
	KUNIT_ASSERT_TRUE(test, kref_test_mutex_unlocked(&object.mutex));

	KUNIT_EXPECT_TRUE(test, kref_put_mutex(&object.ref, kref_test_release,
					       &object.mutex));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 1);
	KUNIT_EXPECT_TRUE(test, kref_test_mutex_unlocked(&object.mutex));
}

static void kref_test_put_lock(struct kunit *test)
{
	struct kref_test_object object;

	kref_test_init_object(&object);
	object.release_lock = true;
	kref_get(&object.ref);

	KUNIT_EXPECT_FALSE(test, kref_put_lock(&object.ref, kref_test_release,
					       &object.lock));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 1U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 0);
	KUNIT_ASSERT_TRUE(test, kref_test_spin_unlocked(&object.lock));

	KUNIT_EXPECT_TRUE(test, kref_put_lock(&object.ref, kref_test_release,
					      &object.lock));
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 1);
	KUNIT_EXPECT_TRUE(test, kref_test_spin_unlocked(&object.lock));
}

struct kref_test_worker {
	struct kref *ref;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static int kref_test_worker_fn(void *data)
{
	struct kref_test_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < KREF_TEST_ITERATIONS; i++) {
		kref_get(worker->ref);
		kref_put(worker->ref, kref_test_release);

		if (!kref_get_unless_zero(worker->ref))
			atomic_inc(worker->errors);
		else
			kref_put(worker->ref, kref_test_release);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void kref_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[KREF_TEST_WORKERS] = {};
	struct kref_test_worker workers[KREF_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct kref_test_object object;
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	kref_test_init_object(&object);
	for (i = 0; i < KREF_TEST_WORKERS; i++) {
		workers[i].ref = &object.ref;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(kref_test_worker_fn, &workers[i],
				       "kref-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start kref worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(KREF_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < KREF_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != KREF_TEST_WORKERS) {
		KUNIT_FAIL(test, "kref workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < KREF_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != KREF_TEST_WORKERS)
		KUNIT_FAIL(test, "kref workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 0);
	KUNIT_EXPECT_TRUE(test, kref_put(&object.ref, kref_test_release));
	KUNIT_EXPECT_EQ(test, atomic_read(&object.releases), 1);
	KUNIT_EXPECT_EQ(test, kref_read(&object.ref), 0U);

	if (started == KREF_TEST_WORKERS &&
	    atomic_read(&done) == KREF_TEST_WORKERS)
		KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
}

static struct kunit_case kref_test_cases[] = {
	KUNIT_CASE(kref_test_lifecycle),
	KUNIT_CASE(kref_test_put_mutex),
	KUNIT_CASE(kref_test_put_lock),
	KUNIT_CASE(kref_test_concurrent),
	{}
};

static struct kunit_suite kref_test_suite = {
	.name = "kref",
	.test_cases = kref_test_cases,
};

kunit_test_suites(&kref_test_suite);

MODULE_DESCRIPTION("kref KUnit test suite");
MODULE_LICENSE("GPL");
