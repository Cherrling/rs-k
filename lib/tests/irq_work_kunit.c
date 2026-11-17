// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/irq_work.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/smp.h>

#define IRQ_WORK_TEST_WORKERS 4
#define IRQ_WORK_TEST_ITEMS 16
#define IRQ_WORK_TEST_TIMEOUT_MS 5000

struct irq_work_test_context {
	atomic_t hits;
	atomic_t errors;
	atomic_t order[3];
	atomic_t position;
	int expected_cpu;
	struct irq_work work;
};

struct irq_work_test_item {
	atomic_t hits;
	int id;
	struct irq_work work;
	struct irq_work_test_context *context;
};

static void irq_work_test_flush_one(void *data)
{
	irq_work_run();
}

static void irq_work_test_flush_all(void)
{
	on_each_cpu(irq_work_test_flush_one, NULL, true);
}

static bool irq_work_test_wait(atomic_t *value, int expected)
{
	unsigned long deadline = jiffies +
				 msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);

	while (atomic_read(value) < expected && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(value) >= expected;
}

static void irq_work_test_record(struct irq_work_test_item *item)
{
	struct irq_work_test_context *context = item->context;
	int position;

	atomic_inc(&context->hits);
	position = atomic_fetch_inc(&context->position);
	if (position < ARRAY_SIZE(context->order))
		atomic_set(&context->order[position], item->id);
}

static void irq_work_test_callback(struct irq_work *work)
{
	struct irq_work_test_item *item = container_of(work,
						       struct irq_work_test_item,
						       work);

	irq_work_test_record(item);
}

static void irq_work_test_state_callback(struct irq_work *work)
{
	struct irq_work_test_context *context =
		container_of(work, struct irq_work_test_context, work);

	if (irq_work_is_pending(work) || !irq_work_is_busy(work))
		atomic_inc(&context->errors);
	atomic_inc(&context->hits);
}

static void irq_work_test_reentrant_callback(struct irq_work *work)
{
	struct irq_work_test_item *item = container_of(work,
						       struct irq_work_test_item,
						       work);
	int hits;

	if (irq_work_is_pending(work) || !irq_work_is_busy(work))
		atomic_inc(&item->context->errors);

	hits = atomic_inc_return(&item->context->hits);
	if (hits == 1 && !irq_work_queue(work))
		atomic_inc(&item->context->errors);
}

static void irq_work_test_remote_callback(struct irq_work *work)
{
	struct irq_work_test_context *context =
		container_of(work, struct irq_work_test_context, work);

	if (smp_processor_id() != context->expected_cpu)
		atomic_inc(&context->errors);
	atomic_inc(&context->hits);
}

#ifdef CONFIG_PREEMPT_RT
static void irq_work_test_rt_remote_callback(struct irq_work *work)
{
	struct irq_work_test_context *context =
		container_of(work, struct irq_work_test_context, work);

	if (smp_processor_id() != context->expected_cpu || in_hardirq())
		atomic_inc(&context->errors);
	atomic_inc(&context->hits);
}
#endif

static void irq_work_test_duplicate(struct kunit *test)
{
	struct irq_work_test_context context = {};
	unsigned long flags;

	context.work = IRQ_WORK_INIT_HARD(irq_work_test_state_callback);

	local_irq_save(flags);
	KUNIT_EXPECT_TRUE(test, irq_work_queue(&context.work));
	KUNIT_EXPECT_TRUE(test, irq_work_is_pending(&context.work));
	KUNIT_EXPECT_TRUE(test, irq_work_is_busy(&context.work));
	KUNIT_EXPECT_FALSE(test, irq_work_queue(&context.work));
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 0);

	irq_work_run();
	local_irq_restore(flags);

	irq_work_sync(&context.work);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_FALSE(test, irq_work_is_pending(&context.work));
	KUNIT_EXPECT_FALSE(test, irq_work_is_busy(&context.work));
}

static void irq_work_test_lifo(struct kunit *test)
{
	struct irq_work_test_context context = {};
	struct irq_work_test_item items[3];
	unsigned long flags;
	int i;

	for (i = 0; i < ARRAY_SIZE(items); i++) {
		items[i].context = &context;
		items[i].id = i + 1;
		items[i].work = IRQ_WORK_INIT_HARD(irq_work_test_callback);
	}

	local_irq_save(flags);
	for (i = 0; i < ARRAY_SIZE(items); i++)
		KUNIT_EXPECT_TRUE(test, irq_work_queue(&items[i].work));

	irq_work_run();
	local_irq_restore(flags);

	for (i = 0; i < ARRAY_SIZE(items); i++)
		irq_work_sync(&items[i].work);

	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 3);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[0]), 3);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[1]), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.order[2]), 1);
}

static void irq_work_test_reentrant(struct kunit *test)
{
	struct irq_work_test_context context = {};
	struct irq_work_test_item item = {
		.context = &context,
		.id = 1,
	};
	bool completed;

	item.work = IRQ_WORK_INIT_HARD(irq_work_test_reentrant_callback);
	KUNIT_ASSERT_TRUE(test, irq_work_queue(&item.work));

	completed = irq_work_test_wait(&context.hits, 2);
	if (!completed)
		irq_work_test_flush_all();
	irq_work_sync(&item.work);

	KUNIT_EXPECT_TRUE(test, completed);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 2);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_FALSE(test, irq_work_is_pending(&item.work));
	KUNIT_EXPECT_FALSE(test, irq_work_is_busy(&item.work));
}

static void irq_work_test_lazy(struct kunit *test)
{
	struct irq_work_test_context context = {};
	struct irq_work_test_item item = {
		.context = &context,
		.id = 1,
	};
	unsigned long flags;
	bool completed;

	item.work = IRQ_WORK_INIT_LAZY(irq_work_test_callback);

	local_irq_save(flags);
	KUNIT_EXPECT_TRUE(test, irq_work_queue(&item.work));
	KUNIT_EXPECT_TRUE(test, irq_work_is_pending(&item.work));
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 0);
	irq_work_tick();
	local_irq_restore(flags);

	completed = irq_work_test_wait(&context.hits, 1);
	if (!completed)
		irq_work_test_flush_all();
	irq_work_sync(&item.work);

	KUNIT_EXPECT_TRUE(test, completed);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_FALSE(test, irq_work_is_pending(&item.work));
	KUNIT_EXPECT_FALSE(test, irq_work_is_busy(&item.work));
}

static void irq_work_test_remote(struct kunit *test)
{
	struct irq_work_test_context context = {};
	bool completed;
	int cpu;

	if (num_online_cpus() < 2)
		kunit_skip(test, "remote queue test needs two online CPUs");

	cpu = cpumask_any_but(cpu_online_mask, smp_processor_id());
	if (cpu >= nr_cpu_ids)
		kunit_skip(test, "no remote online CPU is available");

	context.expected_cpu = cpu;
	context.work = IRQ_WORK_INIT_HARD(irq_work_test_remote_callback);

	KUNIT_ASSERT_TRUE(test, irq_work_queue_on(&context.work, cpu));
	completed = irq_work_test_wait(&context.hits, 1);
	if (!completed)
		irq_work_test_flush_all();
	irq_work_sync(&context.work);

	KUNIT_EXPECT_TRUE(test, completed);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_FALSE(test, irq_work_is_pending(&context.work));
	KUNIT_EXPECT_FALSE(test, irq_work_is_busy(&context.work));
}

#ifdef CONFIG_PREEMPT_RT
static void irq_work_test_rt_remote_lazy(struct kunit *test)
{
	struct irq_work_test_context context = {};
	bool completed;
	int cpu;

	if (num_online_cpus() < 2)
		kunit_skip(test, "remote lazy test needs two online CPUs");

	cpu = cpumask_any_but(cpu_online_mask, smp_processor_id());
	if (cpu >= nr_cpu_ids)
		kunit_skip(test, "no remote online CPU is available");

	context.expected_cpu = cpu;
	context.work = IRQ_WORK_INIT(irq_work_test_rt_remote_callback);

	KUNIT_ASSERT_TRUE(test, irq_work_queue_on(&context.work, cpu));
	completed = irq_work_test_wait(&context.hits, 1);
	if (!completed)
		irq_work_test_flush_all();
	irq_work_sync(&context.work);

	KUNIT_EXPECT_TRUE(test, completed);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	KUNIT_EXPECT_FALSE(test, irq_work_is_pending(&context.work));
	KUNIT_EXPECT_FALSE(test, irq_work_is_busy(&context.work));
}
#endif

struct irq_work_test_sync_context {
	atomic_t entered;
	atomic_t release;
	atomic_t hits;
	atomic_t errors;
	int expected_cpu;
	struct irq_work work;
};

struct irq_work_test_sync_worker {
	struct irq_work_test_sync_context *context;
	atomic_t ready;
	atomic_t go;
	atomic_t done;
};

struct irq_work_test_sync_release {
	struct irq_work_test_sync_context *context;
	atomic_t ready;
	atomic_t fired;
};

static void irq_work_test_sync_callback(struct irq_work *work)
{
	struct irq_work_test_sync_context *context =
		container_of(work, struct irq_work_test_sync_context, work);

	if (smp_processor_id() != context->expected_cpu)
		atomic_inc(&context->errors);
	atomic_set(&context->entered, 1);
	while (!atomic_read(&context->release))
		cpu_relax();
	atomic_inc(&context->hits);
}

static int irq_work_test_sync_worker_fn(void *data)
{
	struct irq_work_test_sync_worker *worker = data;

	atomic_set(&worker->ready, 1);
	while (!atomic_read(&worker->go)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	irq_work_sync(&worker->context->work);
	atomic_set(&worker->done, 1);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static int irq_work_test_sync_release_fn(void *data)
{
	struct irq_work_test_sync_release *release = data;
	unsigned long deadline;

	atomic_set(&release->ready, 1);

	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while (!atomic_read(&release->context->entered) &&
	       !kthread_should_stop() && time_before(jiffies, deadline))
		cond_resched();

	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while (!atomic_read(&release->context->release) &&
	       !kthread_should_stop() && time_before(jiffies, deadline))
		cond_resched();

	if (!kthread_should_stop() &&
	    !atomic_read(&release->context->release)) {
		atomic_set(&release->fired, 1);
		atomic_set(&release->context->release, 1);
	}

	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void irq_work_test_sync(struct kunit *test)
{
	struct irq_work_test_sync_context context = {};
	struct irq_work_test_sync_worker worker = {
		.context = &context,
	};
	struct irq_work_test_sync_release release = {
		.context = &context,
	};
	struct task_struct *sync_task = NULL;
	struct task_struct *release_task = NULL;
	cpumask_var_t old_mask;
	unsigned long deadline;
	int local_cpu;
	int sync_cpu;
	int target_cpu;
	int cpu;

	if (num_online_cpus() < 3)
		kunit_skip(test, "contended sync test needs three online CPUs");
	if (!alloc_cpumask_var(&old_mask, GFP_KERNEL))
		kunit_skip(test, "failed to allocate CPU mask");

	cpumask_copy(old_mask, current->cpus_ptr);
	local_cpu = smp_processor_id();
	if (set_cpus_allowed_ptr(current, cpumask_of(local_cpu))) {
		free_cpumask_var(old_mask);
		KUNIT_FAIL(test, "failed to pin the sync test thread");
		return;
	}

	target_cpu = cpumask_any_but(cpu_online_mask, local_cpu);
	if (target_cpu >= nr_cpu_ids) {
		set_cpus_allowed_ptr(current, old_mask);
		free_cpumask_var(old_mask);
		kunit_skip(test, "no remote online CPU is available");
	}

	sync_cpu = nr_cpu_ids;
	for_each_cpu(cpu, cpu_online_mask) {
		if (cpu != local_cpu && cpu != target_cpu) {
			sync_cpu = cpu;
			break;
		}
	}
	if (sync_cpu >= nr_cpu_ids) {
		set_cpus_allowed_ptr(current, old_mask);
		free_cpumask_var(old_mask);
		kunit_skip(test, "no isolated online CPU is available");
	}

	context.expected_cpu = target_cpu;
	context.work = IRQ_WORK_INIT_HARD(irq_work_test_sync_callback);

	sync_task = kthread_create(irq_work_test_sync_worker_fn, &worker,
				   "irq-work-sync");
	if (IS_ERR(sync_task)) {
		sync_task = NULL;
		KUNIT_FAIL(test, "failed to create the sync worker");
		goto cleanup;
	}
	kthread_bind(sync_task, sync_cpu);
	wake_up_process(sync_task);

	release_task = kthread_create(irq_work_test_sync_release_fn, &release,
				      "irq-work-release");
	if (IS_ERR(release_task)) {
		release_task = NULL;
		KUNIT_FAIL(test, "failed to create the release worker");
		goto cleanup;
	}
	kthread_bind(release_task, sync_cpu);
	wake_up_process(release_task);

	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while ((!atomic_read(&worker.ready) ||
		!atomic_read(&release.ready)) &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (!atomic_read(&worker.ready) || !atomic_read(&release.ready)) {
		KUNIT_FAIL(test, "sync test workers did not become ready");
		goto cleanup;
	}

	if (!irq_work_queue_on(&context.work, target_cpu)) {
		KUNIT_FAIL(test, "failed to queue remote sync work");
		goto cleanup;
	}

	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while (!atomic_read(&context.entered) && time_before(jiffies, deadline))
		cond_resched();
	if (!atomic_read(&context.entered)) {
		KUNIT_FAIL(test, "remote sync work did not start");
		goto cleanup;
	}

	atomic_set(&worker.go, 1);
	msleep(20);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.entered), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&worker.done), 0);

	atomic_set(&context.release, 1);
	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while (!atomic_read(&worker.done) && time_before(jiffies, deadline))
		cond_resched();
	KUNIT_EXPECT_EQ(test, atomic_read(&worker.done), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&release.fired), 0);

cleanup:
	atomic_set(&context.release, 1);
	irq_work_test_flush_all();
	if (sync_task)
		kthread_stop(sync_task);
	if (release_task)
		kthread_stop(release_task);
	irq_work_sync(&context.work);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
	set_cpus_allowed_ptr(current, old_mask);
	free_cpumask_var(old_mask);
}

struct irq_work_test_worker {
	struct irq_work_test_item *items;
	unsigned int start;
	unsigned int count;
	struct irq_work_test_context *context;
	atomic_t *ready;
	atomic_t *start_workers;
	atomic_t *done;
};

static int irq_work_test_worker_fn(void *data)
{
	struct irq_work_test_worker *worker = data;
	unsigned int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start_workers)) {
		if (kthread_should_stop())
			return 0;
		cond_resched();
	}

	for (i = 0; i < worker->count; i++) {
		struct irq_work_test_item *item =
			&worker->items[worker->start + i];

		if (!irq_work_queue(&item->work))
			atomic_inc(&worker->context->errors);
	}

	atomic_inc(worker->done);
	while (!kthread_should_stop())
		cond_resched();
	return 0;
}

static void irq_work_test_concurrent(struct kunit *test)
{
	struct task_struct *tasks[IRQ_WORK_TEST_WORKERS] = {};
	struct irq_work_test_worker workers[IRQ_WORK_TEST_WORKERS];
	struct irq_work_test_context context = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	struct irq_work_test_item *items;
	unsigned int total = IRQ_WORK_TEST_WORKERS * IRQ_WORK_TEST_ITEMS;
	unsigned long deadline;
	unsigned int started = 0;
	bool completed = false;
	int i;

	items = kunit_kzalloc(test, total * sizeof(*items), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, items);
	for (i = 0; i < total; i++) {
		items[i].context = &context;
		items[i].id = i;
		items[i].work = IRQ_WORK_INIT_HARD(irq_work_test_callback);
	}

	for (i = 0; i < IRQ_WORK_TEST_WORKERS; i++) {
		workers[i].items = items;
		workers[i].start = i * IRQ_WORK_TEST_ITEMS;
		workers[i].count = IRQ_WORK_TEST_ITEMS;
		workers[i].context = &context;
		workers[i].ready = &ready;
		workers[i].start_workers = &start;
		workers[i].done = &done;
		tasks[i] = kthread_run(irq_work_test_worker_fn, &workers[i],
				       "irq-work-add-%u", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			tasks[i] = NULL;
			break;
		}
		started++;
	}

	if (started != IRQ_WORK_TEST_WORKERS) {
		KUNIT_FAIL(test, "failed to start all IRQ work producers");
		goto cleanup;
	}
	deadline = jiffies + msecs_to_jiffies(IRQ_WORK_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) != started && time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != started) {
		KUNIT_FAIL(test, "IRQ work producers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	while (atomic_read(&done) != started && time_before(jiffies, deadline))
		cond_resched();
	KUNIT_EXPECT_EQ(test, atomic_read(&done), started);

	completed = irq_work_test_wait(&context.hits, total);

cleanup:
	atomic_set(&start, 1);
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);
	if (!completed)
		irq_work_test_flush_all();
	for (i = 0; i < total; i++)
		irq_work_sync(&items[i].work);

	KUNIT_EXPECT_TRUE(test, completed);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.hits), total);
	KUNIT_EXPECT_EQ(test, atomic_read(&context.errors), 0);
}

static struct kunit_case irq_work_test_cases[] = {
	KUNIT_CASE(irq_work_test_duplicate),
	KUNIT_CASE(irq_work_test_lifo),
	KUNIT_CASE(irq_work_test_reentrant),
	KUNIT_CASE(irq_work_test_lazy),
	KUNIT_CASE(irq_work_test_remote),
#ifdef CONFIG_PREEMPT_RT
	KUNIT_CASE(irq_work_test_rt_remote_lazy),
#endif
	KUNIT_CASE(irq_work_test_sync),
	KUNIT_CASE(irq_work_test_concurrent),
	{}
};

static struct kunit_suite irq_work_test_suite = {
	.name = "irq_work",
	.test_cases = irq_work_test_cases,
};

kunit_test_suites(&irq_work_test_suite);

MODULE_DESCRIPTION("IRQ work KUnit test suite");
MODULE_LICENSE("GPL");
