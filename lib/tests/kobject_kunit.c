// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kobject.h>
#include <linux/kref.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/workqueue.h>

#define KOBJECT_TEST_WORKERS 4
#define KOBJECT_TEST_ITERATIONS 10000
#define KOBJECT_TEST_TIMEOUT_MS 30000
#define KSET_TEST_WORKERS 4
#define KSET_TEST_FIND_ITERATIONS 2000

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

static void kobject_test_wait_for_cleanup(struct kobject *kobj)
{
#ifdef CONFIG_DEBUG_KOBJECT_RELEASE
	flush_delayed_work(&kobj->release);
#endif
}

static bool kobject_test_path_matches(struct kobject *kobj,
				      const char *expected)
{
	char *path;
	bool matches;

	path = kobject_get_path(kobj, GFP_KERNEL);
	if (!path)
		return false;

	matches = !strcmp(path, expected);
	kfree(path);
	return matches;
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
	kobject_test_wait_for_cleanup(&kset->kobj);
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

static void kobject_test_hierarchy_cleanup(struct kunit *test)
{
	struct kobject_test_object *object;
	struct kobject *replacement;
	struct kobject *parent;
	const char *expected;
	bool path_matches;

	kobject_del(NULL);

	parent = kobject_create_and_add("kobject-kunit-hierarchy",
					kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, parent,
				    "kobject-kunit-hierarchy-child"));
	KUNIT_EXPECT_TRUE(test, object->kobj.state_in_sysfs);
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.parent, parent);
	expected = "/kernel/kobject-kunit-hierarchy/kobject-kunit-hierarchy-child";
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	kobject_del(&object->kobj);
	KUNIT_EXPECT_FALSE(test, object->kobj.state_in_sysfs);
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.parent, NULL);
	KUNIT_EXPECT_EQ(test, atomic_read(&object->releases), 0);
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 1U);

	replacement = kobject_create_and_add("kobject-kunit-hierarchy-child",
					     parent);
	KUNIT_ASSERT_NOT_NULL(test, replacement);

	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	KUNIT_EXPECT_EQ(test, kref_read(&object->kobj.kref), 0U);

	kobject_put(replacement);
	kobject_put(parent);
}

static void kobject_test_rename(struct kunit *test)
{
	struct kobject_test_object *detached;
	struct kobject_test_object *object;
	struct kobject *blocker;
	struct kobject *parent;
	const char *expected;
	bool path_matches;

	expected = "/kernel/kobject-kunit-rename-parent/kobject-kunit-rename-new";

	KUNIT_EXPECT_EQ(test, -EINVAL, kobject_rename(NULL, "invalid"));

	detached = kunit_kzalloc(test, sizeof(*detached), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, detached);
	kobject_test_init_object(detached);
	KUNIT_EXPECT_EQ(test, -EINVAL,
			kobject_rename(&detached->kobj, "invalid"));
	kobject_put(&detached->kobj);
	KUNIT_ASSERT_TRUE(test,
			  kobject_test_wait_for_release(detached, 1));

	parent = kobject_create_and_add("kobject-kunit-rename-parent",
					kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, parent);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, parent,
				    "kobject-kunit-rename-old"));

	KUNIT_ASSERT_EQ(test, 0,
			kobject_rename(&object->kobj,
				       "kobject-kunit-rename-new"));
	KUNIT_EXPECT_STREQ(test, kobject_name(&object->kobj),
			   "kobject-kunit-rename-new");
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	blocker = kobject_create_and_add("kobject-kunit-rename-taken", parent);
	KUNIT_ASSERT_NOT_NULL(test, blocker);
	KUNIT_EXPECT_EQ(test, -EEXIST,
			kobject_rename(&object->kobj,
				       "kobject-kunit-rename-taken"));
	KUNIT_EXPECT_STREQ(test, kobject_name(&object->kobj),
			   "kobject-kunit-rename-new");
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	kobject_del(&object->kobj);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	kobject_put(blocker);
	kobject_put(parent);
}

static void kobject_test_move(struct kunit *test)
{
	struct kobject_test_object *object;
	struct kobject *blocker;
	struct kobject *destination;
	struct kobject *source;
	const char *expected;
	bool path_matches;

	expected = "/kernel/kobject-kunit-move-source/kobject-kunit-move-child";

	KUNIT_EXPECT_EQ(test, -EINVAL, kobject_move(NULL, kernel_kobj));

	source = kobject_create_and_add("kobject-kunit-move-source",
					kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, source);
	destination = kobject_create_and_add("kobject-kunit-move-destination",
					     kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, destination);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, source,
				    "kobject-kunit-move-child"));
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	blocker = kobject_create_and_add("kobject-kunit-move-child",
					 destination);
	KUNIT_ASSERT_NOT_NULL(test, blocker);
	KUNIT_EXPECT_EQ(test, -EEXIST,
			kobject_move(&object->kobj, destination));
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.parent, source);
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	kobject_del(blocker);
	kobject_put(blocker);
	KUNIT_ASSERT_EQ(test, 0,
			kobject_move(&object->kobj, destination));
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.parent, destination);
	expected = "/kernel/kobject-kunit-move-destination/kobject-kunit-move-child";
	path_matches = kobject_test_path_matches(&object->kobj, expected);
	KUNIT_EXPECT_TRUE(test, path_matches);

	kobject_del(&object->kobj);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	kobject_put(source);
	kobject_put(destination);
}

static void kobject_test_kset_automatic_cleanup(struct kunit *test)
{
	struct kobject_test_object *object;
	struct kobject *found;
	struct kset *kset;

	kset = kset_create_and_add("kset-kunit-automatic-cleanup", NULL,
				   kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, kset);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	object->kobj.kset = kset;
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, NULL,
				    "kset-kunit-automatic-child"));

	found = kset_find_obj(kset, "kset-kunit-automatic-child");
	KUNIT_EXPECT_PTR_EQ(test, found, &object->kobj);
	if (found)
		kobject_put(found);

	kobject_uevent(&object->kobj, KOBJ_ADD);
	kobject_put(&object->kobj);
	KUNIT_ASSERT_TRUE(test,
			  kobject_test_wait_for_release(object, 1));
	KUNIT_EXPECT_FALSE(test, object->kobj.state_in_sysfs);
	KUNIT_EXPECT_PTR_EQ(test, object->kobj.parent, NULL);
	KUNIT_EXPECT_NULL(test,
			  kset_find_obj(kset, "kset-kunit-automatic-child"));

	kset_unregister(kset);
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

static void kobject_test_kset_init(struct kunit *test)
{
	struct kset *kset;

	kset = kunit_kzalloc(test, sizeof(*kset), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, kset);
	kset_init(kset);

	KUNIT_EXPECT_TRUE(test, kset->kobj.state_initialized);
	KUNIT_EXPECT_EQ(test, kref_read(&kset->kobj.kref), 1U);
	KUNIT_EXPECT_TRUE(test, list_empty(&kset->list));
	KUNIT_EXPECT_NULL(test, kset_find_obj(kset, "missing"));

	kobject_put(&kset->kobj);
	KUNIT_EXPECT_EQ(test, kref_read(&kset->kobj.kref), 0U);
	kobject_test_wait_for_cleanup(&kset->kobj);
}

static void kobject_test_kset_invalid_register(struct kunit *test)
{
	struct kset *kset;

	KUNIT_EXPECT_EQ(test, -EINVAL, kset_register(NULL));
	kset_unregister(NULL);

	kset = kunit_kzalloc(test, sizeof(*kset), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, kset);
	KUNIT_EXPECT_EQ(test, -EINVAL, kset_register(kset));
	KUNIT_EXPECT_FALSE(test, kset->kobj.state_initialized);
}

static void kobject_test_kset_lifecycle(struct kunit *test)
{
	struct kobject_test_object *object;
	struct kobject *found;
	struct kset *kset;

	kset = kset_create_and_add("kset-kunit-lifecycle", NULL, kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, kset);
	KUNIT_EXPECT_TRUE(test, kset->kobj.state_initialized);
	KUNIT_EXPECT_PTR_EQ(test, kset->kobj.parent, kernel_kobj);
	KUNIT_EXPECT_PTR_EQ(test, kset->kobj.kset, NULL);
	KUNIT_EXPECT_NULL(test, kset_find_obj(kset, "missing"));

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	object->kobj.kset = kset;
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, NULL, "kset-kunit-child"));

	found = kset_find_obj(kset, "kset-kunit-child");
	KUNIT_EXPECT_PTR_EQ(test, found, &object->kobj);
	kobject_put(found);
	KUNIT_EXPECT_NULL(test, kset_find_obj(kset, "missing"));

	kobject_del(&object->kobj);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	kset_unregister(kset);
}

static void kobject_test_kset_create_failure(struct kunit *test)
{
	struct kset *first;
	struct kset *second;

	first = kset_create_and_add("kset-kunit-duplicate", NULL, kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, first);

	second = kset_create_and_add("kset-kunit-duplicate", NULL, kernel_kobj);
	KUNIT_EXPECT_NULL(test, second);

	kset_unregister(first);
}

struct kset_test_worker {
	struct kset *kset;
	struct kobject *expected;
	const char *name;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static int kset_test_worker_fn(void *data)
{
	struct kset_test_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < KSET_TEST_FIND_ITERATIONS; i++) {
		struct kobject *found;

		found = kset_find_obj(worker->kset, worker->name);
		if (found != worker->expected)
			atomic_inc(worker->errors);
		if (found)
			kobject_put(found);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void kobject_test_kset_concurrent_find(struct kunit *test)
{
	struct task_struct *tasks[KSET_TEST_WORKERS] = {};
	struct kset_test_worker workers[KSET_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct kobject_test_object *object;
	struct kset *kset;
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	kset = kset_create_and_add("kset-kunit-concurrent", NULL, kernel_kobj);
	KUNIT_ASSERT_NOT_NULL(test, kset);

	object = kunit_kzalloc(test, sizeof(*object), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, object);
	kobject_test_init_object(object);
	object->kobj.kset = kset;
	KUNIT_ASSERT_EQ(test, 0,
			kobject_add(&object->kobj, NULL, "kset-kunit-find"));

	for (i = 0; i < KSET_TEST_WORKERS; i++) {
		workers[i].kset = kset;
		workers[i].expected = &object->kobj;
		workers[i].name = "kset-kunit-find";
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(kset_test_worker_fn, &workers[i],
				       "kset-find-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start kset worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(KOBJECT_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < KSET_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != KSET_TEST_WORKERS) {
		KUNIT_FAIL(test, "kset workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) < KSET_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != KSET_TEST_WORKERS)
		KUNIT_FAIL(test, "kset workers did not finish");

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	kobject_del(&object->kobj);
	kobject_put(&object->kobj);
	KUNIT_EXPECT_TRUE(test, kobject_test_wait_for_release(object, 1));
	kset_unregister(kset);

	if (started == KSET_TEST_WORKERS &&
	    atomic_read(&done) == KSET_TEST_WORKERS)
		KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
}

static struct kunit_case kobject_test_cases[] = {
	KUNIT_CASE(kobject_test_lifecycle),
	KUNIT_CASE(kobject_test_internal_init),
	KUNIT_CASE(kobject_test_invalid_init),
	KUNIT_CASE(kobject_test_hierarchy_cleanup),
	KUNIT_CASE(kobject_test_rename),
	KUNIT_CASE(kobject_test_move),
	KUNIT_CASE(kobject_test_kset_automatic_cleanup),
	KUNIT_CASE(kobject_test_concurrent),
	KUNIT_CASE(kobject_test_kset_init),
	KUNIT_CASE(kobject_test_kset_invalid_register),
	KUNIT_CASE(kobject_test_kset_lifecycle),
	KUNIT_CASE(kobject_test_kset_create_failure),
	KUNIT_CASE(kobject_test_kset_concurrent_find),
	{}
};

static struct kunit_suite kobject_test_suite = {
	.name = "kobject",
	.test_cases = kobject_test_cases,
};

kunit_test_suites(&kobject_test_suite);

MODULE_DESCRIPTION("kobject KUnit test suite");
MODULE_LICENSE("GPL");
