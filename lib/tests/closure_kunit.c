// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/closure.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/llist.h>
#include <linux/sched.h>
#include <linux/workqueue.h>

#define CLOSURE_TEST_WORKERS 4
#define CLOSURE_TEST_ITERATIONS 10000
#define CLOSURE_TEST_TIMEOUT_MS 30000

struct closure_test_object {
	struct closure cl;
	atomic_t callbacks;
	atomic_t *order_count;
	int *sequence;
	int index;
};

struct closure_test_worker {
	struct closure *cl;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *errors;
};

static struct closure_test_object *
closure_test_object_from_work(struct work_struct *work)
{
	return container_of(work, struct closure_test_object, cl.work);
}

static void closure_test_callback(struct work_struct *work)
{
	struct closure_test_object *object = closure_test_object_from_work(work);

	atomic_inc(&object->callbacks);
}

static void closure_test_ordered_destructor(struct work_struct *work)
{
	struct closure_test_object *object = closure_test_object_from_work(work);
	int slot;

	slot = atomic_inc_return(object->order_count) - 1;
	if (slot >= 0 && slot < CLOSURE_TEST_WORKERS)
		object->sequence[slot] = object->index;
}

static bool closure_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline = jiffies + msecs_to_jiffies(CLOSURE_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static void closure_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int closure_test_releaser(void *data)
{
	struct closure_test_worker *worker = data;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	closure_put(worker->cl);
	closure_test_idle_until_stopped();
	return 0;
}

static int closure_test_concurrent_worker(void *data)
{
	struct closure_test_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < CLOSURE_TEST_ITERATIONS; i++) {
		closure_get(worker->cl);
		closure_put(worker->cl);
		if (closure_nr_remaining(worker->cl) == 0)
			atomic_inc(worker->errors);
	}

	atomic_inc(worker->done);
	closure_test_idle_until_stopped();
	return 0;
}

static void closure_test_lifecycle(struct kunit *test)
{
	struct closure_test_object object = {};

	closure_init(&object.cl, NULL);
#ifdef CONFIG_DEBUG_CLOSURES
	KUNIT_EXPECT_EQ(test, object.cl.magic, CLOSURE_MAGIC_ALIVE);
#endif
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);

	closure_get(&object.cl);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 2U);
	closure_put(&object.cl);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.callbacks), 0);

	continue_at(&object.cl, closure_test_callback, NULL);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.callbacks), 1);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);

	closure_return(&object.cl);
	KUNIT_EXPECT_EQ(test, atomic_read(&object.cl.remaining), 0);
#ifdef CONFIG_DEBUG_CLOSURES
	KUNIT_EXPECT_EQ(test, object.cl.magic, CLOSURE_MAGIC_DEAD);
#endif
}

static void closure_test_waitlist(struct kunit *test)
{
	struct closure_test_object object = {};
	struct closure_waitlist waitlist;

	init_llist_head(&waitlist.list);
	closure_init_stack(&object.cl);

	KUNIT_EXPECT_TRUE(test, closure_wait(&waitlist, &object.cl));
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 2U);
	KUNIT_EXPECT_TRUE(test,
			  atomic_read(&object.cl.remaining) & CLOSURE_WAITING);
#ifdef CONFIG_DEBUG_CLOSURES
	KUNIT_EXPECT_NE(test, object.cl.waiting_on, 0UL);
#endif
	KUNIT_EXPECT_FALSE(test, closure_wait(&waitlist, &object.cl));
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 2U);

	closure_wake_up(&waitlist);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);
	KUNIT_EXPECT_FALSE(test,
			   atomic_read(&object.cl.remaining) & CLOSURE_WAITING);
#ifdef CONFIG_DEBUG_CLOSURES
	KUNIT_EXPECT_EQ(test, object.cl.waiting_on, 0UL);
#endif
	KUNIT_EXPECT_TRUE(test, llist_empty(&waitlist.list));

	closure_return(&object.cl);
}

static void closure_test_waitlist_fifo(struct kunit *test)
{
	struct closure_test_object objects[CLOSURE_TEST_WORKERS] = {};
	struct closure_waitlist waitlist;
	atomic_t order_count = ATOMIC_INIT(0);
	int sequence[CLOSURE_TEST_WORKERS] = {};
	int i;

	init_llist_head(&waitlist.list);
	for (i = 0; i < CLOSURE_TEST_WORKERS; i++) {
		objects[i].order_count = &order_count;
		objects[i].sequence = sequence;
		objects[i].index = i;
		closure_init_stack(&objects[i].cl);
		KUNIT_ASSERT_TRUE(test,
				  closure_wait(&waitlist, &objects[i].cl));
		closure_return_with_destructor(&objects[i].cl,
					       closure_test_ordered_destructor);
	}

	closure_wake_up(&waitlist);
	KUNIT_ASSERT_EQ(test, atomic_read(&order_count), CLOSURE_TEST_WORKERS);
	KUNIT_ASSERT_TRUE(test, llist_empty(&waitlist.list));

	for (i = 0; i < CLOSURE_TEST_WORKERS; i++)
		KUNIT_EXPECT_EQ(test, sequence[i], i);
}

static void closure_test_sync(struct kunit *test)
{
	struct closure_test_object object = {};
	struct closure_test_worker worker = {};
	struct task_struct *task;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);

	closure_init_stack(&object.cl);
	closure_get(&object.cl);
	worker.cl = &object.cl;
	worker.ready = &ready;
	worker.start = &start;

	task = kthread_run(closure_test_releaser, &worker, "closure-sync");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	if (IS_ERR(task))
		task = NULL;
	KUNIT_ASSERT_TRUE(test, closure_test_wait_for(&ready, 1));

	atomic_set(&start, 1);
	closure_sync(&object.cl);
	kthread_stop(task);

	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);
	KUNIT_EXPECT_FALSE(test, object.cl.closure_get_happened);
	closure_return(&object.cl);
}

static void closure_test_sync_timeout_complete(struct kunit *test)
{
	struct closure_test_object object = {};
	struct closure_test_worker worker = {};
	struct task_struct *task;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	long ret;

	closure_init_stack(&object.cl);
	closure_get(&object.cl);
	worker.cl = &object.cl;
	worker.ready = &ready;
	worker.start = &start;

	task = kthread_run(closure_test_releaser, &worker,
			   "closure-sync-timeout");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	if (IS_ERR(task))
		task = NULL;
	KUNIT_ASSERT_TRUE(test, closure_test_wait_for(&ready, 1));

	atomic_set(&start, 1);
	ret = closure_sync_timeout(&object.cl, HZ);
	kthread_stop(task);

	KUNIT_EXPECT_EQ(test, ret, 0L);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);
	closure_return(&object.cl);
}

static void closure_test_sync_timeout_expire(struct kunit *test)
{
	struct closure_test_object object = {};
	long ret;

	closure_init_stack(&object.cl);
	closure_get(&object.cl);

	ret = closure_sync_timeout(&object.cl, 1);
	KUNIT_EXPECT_EQ(test, ret, -ETIME);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 2U);

	closure_put(&object.cl);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);
	closure_return(&object.cl);
}

static void closure_test_return_sync(struct kunit *test)
{
	struct closure_test_object object = {};
	struct closure_test_worker worker = {};
	struct task_struct *task;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);

	closure_init_stack(&object.cl);
	closure_get(&object.cl);
	worker.cl = &object.cl;
	worker.ready = &ready;
	worker.start = &start;

	task = kthread_run(closure_test_releaser, &worker,
			   "closure-return-sync");
	KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(task));
	if (IS_ERR(task))
		task = NULL;
	KUNIT_ASSERT_TRUE(test, closure_test_wait_for(&ready, 1));

	atomic_set(&start, 1);
	closure_return_sync(&object.cl);
	kthread_stop(task);

	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 0U);
	KUNIT_EXPECT_TRUE(test,
			  atomic_read(&object.cl.remaining) & CLOSURE_DESTRUCTOR);
	KUNIT_EXPECT_FALSE(test, closure_get_not_zero(&object.cl));
}

static void closure_test_workqueue(struct kunit *test)
{
	struct closure_test_object object = {};

	closure_init_stack(&object.cl);
	continue_at(&object.cl, closure_test_callback, system_dfl_wq);

	KUNIT_ASSERT_TRUE(test,
			  closure_test_wait_for(&object.callbacks, 1));
	KUNIT_EXPECT_EQ(test, atomic_read(&object.cl.remaining),
			CLOSURE_REMAINING_INITIALIZER);
	closure_return(&object.cl);
}

static void closure_test_parent_destructor(struct kunit *test)
{
	struct closure_test_object parent = {};
	struct closure_test_object child = {};

	closure_init_stack(&parent.cl);
	closure_init(&child.cl, &parent.cl);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&parent.cl), 2U);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&child.cl), 1U);

	closure_return_with_destructor(&child.cl, closure_test_callback);
	KUNIT_EXPECT_EQ(test, atomic_read(&child.callbacks), 1);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&child.cl), 0U);
	KUNIT_EXPECT_TRUE(test,
			  atomic_read(&child.cl.remaining) & CLOSURE_DESTRUCTOR);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&parent.cl), 1U);

	closure_return(&parent.cl);
}

static void closure_test_concurrent(struct kunit *test)
{
	struct closure_test_object object = {};
	struct closure_test_worker workers[CLOSURE_TEST_WORKERS] = {};
	struct task_struct *tasks[CLOSURE_TEST_WORKERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	int started = 0;
	int i;

	closure_init_stack(&object.cl);
	for (i = 0; i < CLOSURE_TEST_WORKERS; i++) {
		workers[i].cl = &object.cl;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].errors = &errors;
		tasks[i] = kthread_run(closure_test_concurrent_worker,
				       &workers[i], "closure-ref-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			KUNIT_FAIL(test, "failed to start closure worker");
			goto cleanup;
		}
		started++;
	}

	if (!closure_test_wait_for(&ready, CLOSURE_TEST_WORKERS)) {
		KUNIT_FAIL(test, "closure workers did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!closure_test_wait_for(&done, CLOSURE_TEST_WORKERS)) {
		KUNIT_FAIL(test, "closure workers did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
	KUNIT_EXPECT_EQ(test, closure_nr_remaining(&object.cl), 1U);

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);
	if (started == CLOSURE_TEST_WORKERS &&
	    atomic_read(&done) == CLOSURE_TEST_WORKERS)
		closure_return(&object.cl);
}

static struct kunit_case closure_test_cases[] = {
	KUNIT_CASE(closure_test_lifecycle),
	KUNIT_CASE(closure_test_waitlist),
	KUNIT_CASE(closure_test_waitlist_fifo),
	KUNIT_CASE(closure_test_sync),
	KUNIT_CASE(closure_test_sync_timeout_complete),
	KUNIT_CASE(closure_test_sync_timeout_expire),
	KUNIT_CASE(closure_test_return_sync),
	KUNIT_CASE(closure_test_workqueue),
	KUNIT_CASE(closure_test_parent_destructor),
	KUNIT_CASE(closure_test_concurrent),
	{}
};

static struct kunit_suite closure_test_suite = {
	.name = "closure",
	.test_cases = closure_test_cases,
};

kunit_test_suites(&closure_test_suite);

MODULE_DESCRIPTION("closure KUnit test suite");
MODULE_LICENSE("GPL");
