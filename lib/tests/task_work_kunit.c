// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched/signal.h>
#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/task-work-rust.h>

#define TASK_WORK_TEST_WORKERS 4
#define TASK_WORK_TEST_STRESS_ITEMS 64
#define TASK_WORK_TEST_STRESS_ROUNDS 100
#define TASK_WORK_TEST_TIMEOUT_MS 30000

struct task_work_test_context {
	atomic_t hits;
	atomic_t position;
	atomic_t order[3];
	atomic_t errors;
};

struct task_work_test_item {
	struct callback_head head;
	struct task_work_test_context *context;
	struct task_work_test_item *reentrant;
	int id;
};

static void task_work_test_record(struct task_work_test_item *item)
{
	int position;

	atomic_inc(&item->context->hits);
	position = atomic_fetch_inc(&item->context->position);
	if (position < ARRAY_SIZE(item->context->order))
		atomic_set(&item->context->order[position], item->id);
}

static void task_work_test_callback(struct callback_head *head)
{
	struct task_work_test_item *item = container_of(head,
						       struct task_work_test_item,
						       head);

	task_work_test_record(item);
}

static void task_work_test_reentrant_callback(struct callback_head *head)
{
	struct task_work_test_item *item = container_of(head,
						       struct task_work_test_item,
						       head);

	task_work_test_record(item);
	if (item->reentrant &&
	    task_work_add(current, &item->reentrant->head, TWA_NONE))
		atomic_inc(&item->context->errors);
}

static void task_work_test_init_item(struct task_work_test_context *context,
				     struct task_work_test_item *item, int id)
{
	item->context = context;
	item->id = id;
	init_task_work(&item->head, task_work_test_callback);
}

static void task_work_test_lifo(struct kunit *test)
{
	struct task_work_test_context context = {};
	struct task_work_test_item items[3];
	int i;

	for (i = 0; i < ARRAY_SIZE(items); i++)
		task_work_test_init_item(&context, &items[i], i + 1);

	for (i = 0; i < ARRAY_SIZE(items); i++)
		KUNIT_ASSERT_EQ(test,
				task_work_add(current, &items[i].head,
					      TWA_NONE), 0);
	KUNIT_EXPECT_TRUE(test, task_work_pending(current));

	task_work_run();

	KUNIT_EXPECT_FALSE(test, task_work_pending(current));
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 3);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[0]), 3);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[1]), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[2]), 1);
}

static bool task_work_test_match_id(struct callback_head *head, void *data)
{
	struct task_work_test_item *item = container_of(head,
						       struct task_work_test_item,
						       head);

	return item->id == *(int *)data;
}

static void task_work_test_cancel(struct kunit *test)
{
	struct task_work_test_context context = {};
	struct task_work_test_item items[3];
	struct callback_head *found;
	int id = 1;
	int i;

	for (i = 0; i < ARRAY_SIZE(items); i++)
		task_work_test_init_item(&context, &items[i], i + 1);

	for (i = 0; i < ARRAY_SIZE(items); i++)
		KUNIT_ASSERT_EQ(test,
				task_work_add(current, &items[i].head,
					      TWA_NONE), 0);

	KUNIT_EXPECT_TRUE(test, task_work_cancel(current, &items[1].head));
	KUNIT_EXPECT_FALSE(test, task_work_cancel(current, &items[1].head));

	found = task_work_cancel_func(current, task_work_test_callback);
	KUNIT_EXPECT_PTR_EQ(test, found, &items[2].head);

	KUNIT_ASSERT_EQ(test, task_work_add(current, &items[1].head, TWA_NONE),
			0);
	found = task_work_cancel_match(current, task_work_test_match_id, &id);
	KUNIT_EXPECT_PTR_EQ(test, found, &items[0].head);

	task_work_run();
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[0]), 2);
}

static void task_work_test_reentrant(struct kunit *test)
{
	struct task_work_test_context context = {};
	struct task_work_test_item first;
	struct task_work_test_item second;

	task_work_test_init_item(&context, &first, 1);
	task_work_test_init_item(&context, &second, 2);
	init_task_work(&first.head, task_work_test_reentrant_callback);
	first.reentrant = &second;

	KUNIT_ASSERT_EQ(test, task_work_add(current, &first.head, TWA_NONE), 0);
	task_work_run();

	KUNIT_EXPECT_FALSE(test, task_work_pending(current));
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
}

static void task_work_test_notifications(struct kunit *test)
{
	struct task_work_test_context context = {};
	struct task_work_test_item item;

	task_work_test_init_item(&context, &item, 1);

	KUNIT_ASSERT_EQ(test, task_work_add(current, &item.head, TWA_RESUME), 0);
	KUNIT_EXPECT_TRUE(test, test_thread_flag(TIF_NOTIFY_RESUME));
	clear_thread_flag(TIF_NOTIFY_RESUME);
	KUNIT_ASSERT_TRUE(test, task_work_cancel(current, &item.head));

	KUNIT_ASSERT_EQ(test, task_work_add(current, &item.head, TWA_SIGNAL), 0);
	KUNIT_EXPECT_TRUE(test, test_thread_flag(TIF_NOTIFY_SIGNAL));
	clear_notify_signal();
	KUNIT_ASSERT_TRUE(test, task_work_cancel(current, &item.head));

	KUNIT_ASSERT_EQ(test,
			task_work_add(current, &item.head, TWA_SIGNAL_NO_IPI), 0);
	KUNIT_EXPECT_TRUE(test, test_thread_flag(TIF_NOTIFY_SIGNAL));
	clear_notify_signal();
	KUNIT_ASSERT_TRUE(test, task_work_cancel(current, &item.head));

	KUNIT_EXPECT_FALSE(test, task_work_pending(current));
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 0);
}

#ifdef CONFIG_RUST_TASK_WORK
static void task_work_test_exiting(struct kunit *test)
{
	struct task_work_test_context context = {};
	struct task_work_test_item item;
	struct task_struct *task;

	task = kunit_kzalloc(test, sizeof(*task), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, task);
	raw_spin_lock_init(&task->pi_lock);
	task->flags = PF_EXITING;
	task_work_test_init_item(&context, &item, 1);

	rust_task_work_run_task(task);
	KUNIT_EXPECT_TRUE(test, task_work_pending(task));
	KUNIT_EXPECT_EQ(test, task_work_add(task, &item.head, TWA_NONE),
			-ESRCH);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 0);
}
#endif

struct task_work_test_target {
	atomic_t ready;
	atomic_t go;
	atomic_t done;
};

static int task_work_test_target_fn(void *data)
{
	struct task_work_test_target *target = data;

	atomic_inc(&target->ready);
	while (!atomic_read(&target->go)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	task_work_run();
	atomic_inc(&target->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

struct task_work_test_worker {
	struct task_struct *target;
	struct task_work_test_item *items;
	unsigned int start;
	unsigned int count;
	struct task_work_test_context *context;
	atomic_t *errors;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *start_workers;
};

static int task_work_test_worker_fn(void *data)
{
	struct task_work_test_worker *worker = data;
	unsigned int i;
	int round;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start_workers)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < worker->count; i++) {
		struct task_work_test_item *item = &worker->items[worker->start + i];
		bool queued = false;

		init_task_work(&item->head, task_work_test_callback);
		item->context = worker->context;
		for (round = 0; round < TASK_WORK_TEST_STRESS_ROUNDS; round++) {
			if (task_work_add(worker->target, &item->head, TWA_NONE)) {
				atomic_inc(worker->errors);
				continue;
			}
			queued = true;
			if (!task_work_cancel(worker->target, &item->head)) {
				atomic_inc(worker->errors);
				break;
			}
			queued = false;
		}
		if (!queued &&
		    task_work_add(worker->target, &item->head, TWA_NONE))
			atomic_inc(worker->errors);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void task_work_test_concurrent(struct kunit *test)
{
	struct task_struct *target_task = NULL;
	struct task_struct *tasks[TASK_WORK_TEST_WORKERS] = {};
	struct task_work_test_target target = {};
	struct task_work_test_worker workers[TASK_WORK_TEST_WORKERS];
	struct task_work_test_context context = {};
	struct task_work_test_item *items;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	unsigned int items_per_worker = TASK_WORK_TEST_STRESS_ITEMS /
					TASK_WORK_TEST_WORKERS;
	unsigned int total_items = items_per_worker *
				   TASK_WORK_TEST_WORKERS;
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	items = kunit_kzalloc(test, total_items * sizeof(*items), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, items);
	target_task = kthread_run(task_work_test_target_fn, &target,
				  "task-work-target");
	if (IS_ERR_OR_NULL(target_task)) {
		target_task = NULL;
		KUNIT_FAIL(test, "failed to start task-work target");
		return;
	}

	deadline = jiffies + msecs_to_jiffies(TASK_WORK_TEST_TIMEOUT_MS);
	while (atomic_read(&target.ready) != 1 &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&target.ready) != 1) {
		KUNIT_FAIL(test, "task-work target did not become ready");
		goto cleanup;
	}

	for (i = 0; i < TASK_WORK_TEST_WORKERS; i++) {
		workers[i].target = target_task;
		workers[i].items = items;
		workers[i].start = i * items_per_worker;
		workers[i].count = items_per_worker;
		workers[i].context = &context;
		workers[i].errors = &context.errors;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].start_workers = &start;
		tasks[i] = kthread_run(task_work_test_worker_fn, &workers[i],
				       "task-work-adder-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}
	if (started != TASK_WORK_TEST_WORKERS) {
		KUNIT_FAIL(test, "failed to start task-work adders");
		goto cleanup;
	}

	while (atomic_read(&ready) != TASK_WORK_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != TASK_WORK_TEST_WORKERS) {
		KUNIT_FAIL(test, "task-work adders did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) != TASK_WORK_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&done) != TASK_WORK_TEST_WORKERS) {
		KUNIT_FAIL(test, "task-work adders did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_TRUE(test, task_work_pending(target_task));
	atomic_set(&target.go, 1);
	while (atomic_read(&target.done) != 1 &&
	       time_before(jiffies, deadline))
		cond_resched();
	KUNIT_EXPECT_EQ(test, atomic_read(&target.done), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), total_items);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_FALSE(test, task_work_pending(target_task));

cleanup:
	atomic_set(&start, 1);
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);
	atomic_set(&target.go, 1);
	if (target_task)
		kthread_stop(target_task);
}

static struct kunit_case task_work_test_cases[] = {
	KUNIT_CASE(task_work_test_lifo),
	KUNIT_CASE(task_work_test_cancel),
	KUNIT_CASE(task_work_test_reentrant),
	KUNIT_CASE(task_work_test_notifications),
#ifdef CONFIG_RUST_TASK_WORK
	KUNIT_CASE(task_work_test_exiting),
#endif
	KUNIT_CASE(task_work_test_concurrent),
	{}
};

static struct kunit_suite task_work_test_suite = {
	.name = "task_work",
	.test_cases = task_work_test_cases,
};

kunit_test_suites(&task_work_test_suite);

MODULE_DESCRIPTION("task_work KUnit test suite");
MODULE_LICENSE("GPL");
