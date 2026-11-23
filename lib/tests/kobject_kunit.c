// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/string.h>

#define KOBJECT_TEST_WORKERS 4
#define KOBJECT_TEST_ITERATIONS 10000
#define KOBJECT_TEST_TIMEOUT_MS 30000

struct kobject_test_object {
	struct kobject kobj;
	atomic_t releases;
};

static void kobject_test_release(struct kobject *kobj)
{
	struct kobject_test_object *object = container_of(kobj,
							  struct kobject_test_object,
							  kobj);

	atomic_inc(&object->releases);
}

static const struct kobj_type kobject_test_type = {
	.release = kobject_test_release,
};

static void kobject_test_init_object(struct kobject_test_object *object)
{
	memset(object, 0, sizeof(*object));
	atomic_set(&object->releases, 0);
	kobject_init(&object->kobj, &kobject_test_type);
}

static bool kobject_test_wait_for_release(struct kobject_test_object *object,
					  int expected)
{
	unsigned long deadline;

	deadline = jiffies + msecs_to_jiffies(KOBJECT_TEST_TIMEOUT_MS);
	while (atomic_read(&object->releases) < expected &&
	       time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(&object->releases) == expected;
}

static void kobject_test_lifecycle(struct kunit *test)
{
	struct kobject_test_object *object;

	KUNIT_EXPECT_NULL(test, kobject_get(NULL));
	KUNIT_EXPECT_NULL(test, kobject_get_unless_zero(NULL));
	kobject_put(NULL);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	KUNIT_EXPECT_TRUE(test, object->kobj.state_initialized);
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.ktype, &kobject_test_type);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);

	KUNIT_EXPECT_PTR_EQ(test, kobject_get(&object->kobj), &object->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 2U);
	KUNIT_EXPECT_PTR_EQ(test, kobject_get_unless_zero(&object->kobj),
			    &object->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 3U);

	kobject_put(&object->kobj);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object->releases), 0);

	kobject_put(&object->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 0U);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));

	KUNIT_EXPECT_NULL(test, kobject_get_unless_zero(&object->kobj));
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 0U);
	KUNIT_EXPECT_EQ(test, atomic_read(&object->releases), 1);
}

static void kobject_test_internal_init(struct kunit *test)
{
	struct kset *kset;

	kset = kunit_kzalloc(test, sizeof(*kset), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, kset);
	kset_init(kset);

	KUNIT_EXPECT_TRUE(test, kset->kobj.state_initialized);
	KUNIT_EXPECT_EQ(test, kref_read(&kset->kobj.kref), 1U);
	KUNIT_EXPECT_PTR_EQ(test, kset->kobj.ktype, NULL);
	kobject_put(&kset->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&kset->kobj.kref), 0U);
}

static void kobject_test_invalid_init(struct kunit *test)
{
	struct kobject_test_object *object;

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);

	kobject_init(NULL, &kobject_test_type);
	KUNIT_EXPECT_FALSE(test, object->kobj.state_initialized);

	kobject_init(&object->kobj, NULL);
	KUNIT_EXPECT_FALSE(test, object->kobj.state_initialized);

	kobject_test_init_object(object);
	KUNIT_EXPECT_TRUE(test, object->kobj.state_initialized);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);

	kobject_init(&object->kobj, &kobject_test_type);
	KUNIT_EXPECT_TRUE(test, object->kobj.state_initialized);
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.ktype, &kobject_test_type);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);

	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 0U);
}

struct kobject_test_worker {
	struct kobject *kobj;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static int kobject_test_worker_fn(void *data)
{
	struct kobject_test_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < KOBJECT_TEST_ITERATIONS; i++) {
		kobject_get(worker->kobj);
		kobject_put(worker->kobj);

		if (!kobject_get_unless_zero(worker->kobj))
			atomic_inc(worker->errors);
		else
			kobject_put(worker->kobj);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void kobject_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[KOBJECT_TEST_WORKERS] = {};
	struct kobject_test_worker workers[KOBJECT_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct kobject_test_object *object;
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	for (i = 0; i < KOBJECT_TEST_WORKERS; i++) {
		workers[i].kobj = &object->kobj;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(kobject_test_worker_fn, &workers[i],
				       "kobject-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start kobject worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(KOBJECT_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < KOBJECT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != KOBJECT_TEST_WORKERS) {
		KUNIT_FAIL(test, "kobject workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < KOBJECT_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != KOBJECT_TEST_WORKERS)
		KUNIT_FAIL(test, "kobject workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	KUNIT_EXPECT_EQ(test, atomic_read(&object->releases), 0);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 0U);

	if (started == KOBJECT_TEST_WORKERS &&
	    atomic_read(&done) == KOBJECT_TEST_WORKERS)
		KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
}

static struct kunit_case kobject_test_cases[] = {
	KUNIT_CASE(kobject_test_lifecycle),
	KUNIT_CASE(kobject_test_internal_init),
	KUNIT_CASE(kobject_test_invalid_init),
	KUNIT_CASE(kobject_test_concurrent),
	{}
};

static struct kunit_suite kobject_test_suite = {
	.name = "kobject",
	.test_cases = kobject_test_cases,
};

kunit_test_suites(&kobject_test_suite);

MODULE_DESCRIPTION("kobject KUnit test suite");
MODULE_LICENSE("GPL");
