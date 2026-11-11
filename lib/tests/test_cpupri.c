// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/cpupri.h>
#include <linux/cpumask.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define CPUPRI_TEST_TIMEOUT_MS		30000
#define CPUPRI_TEST_WORKERS		4
#define CPUPRI_TEST_UPDATES		10000
#define CPUPRI_TEST_SEARCHES		20000

static int cpupri_test_rejected_cpu = -1;
static bool cpupri_test_reject_all_cpus;
static atomic_t cpupri_test_fitness_calls = ATOMIC_INIT(0);

struct cpupri_test_worker {
	struct cpupri *cp;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	unsigned int cpu;
	int index;
};

static bool cpupri_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(CPUPRI_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static struct task_struct *cpupri_test_alloc_task(int priority)
{
	struct task_struct *task = kzalloc_obj(*task);

	if (!task)
		return NULL;

	task->prio = priority;
	cpumask_clear(&task->cpus_mask);
	return task;
}

static void cpupri_test_set_affinity(struct task_struct *task, unsigned int cpu)
{
	cpumask_clear(&task->cpus_mask);
	cpumask_set_cpu(cpu, &task->cpus_mask);
}

static bool cpupri_test_fitness(struct task_struct *task, int cpu)
{
	atomic_inc(&cpupri_test_fitness_calls);

	if (cpupri_test_reject_all_cpus)
		return false;

	return cpu != cpupri_test_rejected_cpu;
}

static int cpupri_test_expected_vector(int priority)
{
	if (priority == CPUPRI_INVALID)
		return CPUPRI_INVALID;
	if (priority < MAX_RT_PRIO - 1)
		return MAX_RT_PRIO - 1 - priority;
	if (priority == MAX_RT_PRIO - 1)
		return CPUPRI_NORMAL;
	return CPUPRI_HIGHER;
}

static void cpupri_test_priority_mapping(struct kunit *test)
{
	static const struct {
		int priority;
		int vector;
	} cases[] = {
		{ CPUPRI_INVALID, CPUPRI_INVALID },
		{ 0, 99 },
		{ 1, 98 },
		{ 98, 1 },
		{ MAX_RT_PRIO - 1, CPUPRI_NORMAL },
		{ MAX_RT_PRIO, CPUPRI_HIGHER },
	};
	struct cpupri cp;
	unsigned int cpu = cpumask_first(cpu_active_mask);
	int i;

	KUNIT_ASSERT_LT(test, cpu, nr_cpu_ids);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		int vector = cases[i].vector;

		cpupri_set(&cp, cpu, cases[i].priority);
		KUNIT_EXPECT_EQ(test, cp.cpu_to_pri[cpu], vector);
		KUNIT_EXPECT_EQ(test,
				atomic_read(&cp.pri_to_cpu[vector].count),
				vector == CPUPRI_INVALID ? 0 : 1);
		if (i) {
			int old_vector = cases[i - 1].vector;

			if (old_vector != CPUPRI_INVALID)
				KUNIT_EXPECT_EQ(test,
						atomic_read(&cp.pri_to_cpu[old_vector].count),
						0);
		}
	}

	cpupri_cleanup(&cp);
}

static void cpupri_test_exhaustive_search_order(struct kunit *test)
{
	struct task_struct *task;
	struct cpupri cp;
	cpumask_var_t lowest_mask;
	unsigned int cpu = cpumask_first(cpu_active_mask);
	int cpu_priority;
	int task_priority;

	KUNIT_ASSERT_LT(test, cpu, nr_cpu_ids);
	KUNIT_ASSERT_TRUE(test, zalloc_cpumask_var(&lowest_mask, GFP_KERNEL));
	task = cpupri_test_alloc_task(0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	cpupri_test_set_affinity(task, cpu);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);

	for (cpu_priority = CPUPRI_INVALID;
	     cpu_priority < CPUPRI_NR_PRIORITIES; cpu_priority++) {
		int cpu_vector = cpupri_test_expected_vector(cpu_priority);

		cpupri_set(&cp, cpu, cpu_priority);
		for (task_priority = 0;
		     task_priority < CPUPRI_NR_PRIORITIES; task_priority++) {
			int task_vector = cpupri_test_expected_vector(task_priority);
			bool expected = cpu_vector != CPUPRI_INVALID &&
					cpu_vector < task_vector;
			int ret;

			task->prio = task_priority;
			cpumask_clear(lowest_mask);
			ret = cpupri_find(&cp, task, lowest_mask);
			if (ret != expected)
				KUNIT_EXPECT_EQ(test, ret, expected);
			if (expected)
				KUNIT_EXPECT_TRUE(test,
						  cpumask_test_cpu(cpu, lowest_mask));
		}
	}

	cpupri_cleanup(&cp);
	kfree(task);
	free_cpumask_var(lowest_mask);
}

static void cpupri_test_find_boundaries(struct kunit *test)
{
	struct task_struct *task;
	struct cpupri cp;
	cpumask_var_t lowest_mask;
	unsigned int cpu = cpumask_first(cpu_active_mask);
	int ret;

	KUNIT_ASSERT_LT(test, cpu, nr_cpu_ids);
	KUNIT_ASSERT_TRUE(test, zalloc_cpumask_var(&lowest_mask, GFP_KERNEL));
	task = cpupri_test_alloc_task(0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	cpupri_test_set_affinity(task, cpu);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);

	cpupri_set(&cp, cpu, MAX_RT_PRIO - 1);
	task->prio = MAX_RT_PRIO - 2;
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu, lowest_mask));

	cpupri_set(&cp, cpu, 0);
	task->prio = 0;
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 0);

	task->prio = MAX_RT_PRIO;
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu, lowest_mask));
	KUNIT_EXPECT_EQ(test, cpupri_find(&cp, task, NULL), 1);

	cpupri_set(&cp, cpu, CPUPRI_HIGHER);
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, cpupri_find(&cp, task, NULL), 0);

	cpupri_cleanup(&cp);
	kfree(task);
	free_cpumask_var(lowest_mask);
}

static void cpupri_test_lowest_priority_and_affinity(struct kunit *test)
{
	struct task_struct *task;
	struct cpupri cp;
	cpumask_var_t lowest_mask;
	unsigned int cpu0 = cpumask_first(cpu_active_mask);
	unsigned int cpu1 = cpumask_next(cpu0, cpu_active_mask);
	int ret;

	if (cpu1 >= nr_cpu_ids)
		kunit_skip(test, "CPU-priority search needs two active CPUs");

	KUNIT_ASSERT_TRUE(test, zalloc_cpumask_var(&lowest_mask, GFP_KERNEL));
	task = cpupri_test_alloc_task(10);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	cpumask_copy(&task->cpus_mask, cpu_active_mask);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);

	cpupri_set(&cp, cpu0, 80);
	cpupri_set(&cp, cpu1, 20);
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu0, lowest_mask));
	KUNIT_EXPECT_FALSE(test, cpumask_test_cpu(cpu1, lowest_mask));

	cpupri_test_set_affinity(task, cpu1);
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_FALSE(test, cpumask_test_cpu(cpu0, lowest_mask));
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu1, lowest_mask));

	cpumask_copy(&task->cpus_mask, cpu_active_mask);
	cpupri_set(&cp, cpu0, 20);
	ret = cpupri_find(&cp, task, lowest_mask);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu0, lowest_mask));
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu1, lowest_mask));

	cpupri_cleanup(&cp);
	kfree(task);
	free_cpumask_var(lowest_mask);
}

static void cpupri_test_fitness_filter_and_fallback(struct kunit *test)
{
	struct task_struct *task;
	struct cpupri cp;
	cpumask_var_t lowest_mask;
	unsigned int cpu0 = cpumask_first(cpu_active_mask);
	unsigned int cpu1 = cpumask_next(cpu0, cpu_active_mask);
	int ret;

	if (cpu1 >= nr_cpu_ids)
		kunit_skip(test, "fitness fallback test needs two active CPUs");

	KUNIT_ASSERT_TRUE(test, zalloc_cpumask_var(&lowest_mask, GFP_KERNEL));
	task = cpupri_test_alloc_task(49);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	cpumask_copy(&task->cpus_mask, cpu_active_mask);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);
	cpupri_set(&cp, cpu0, 50);
	cpupri_set(&cp, cpu1, 50);

	cpupri_test_rejected_cpu = cpu0;
	cpupri_test_reject_all_cpus = false;
	atomic_set(&cpupri_test_fitness_calls, 0);
	ret = cpupri_find_fitness(&cp, task, lowest_mask,
				  cpupri_test_fitness);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_FALSE(test, cpumask_test_cpu(cpu0, lowest_mask));
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu1, lowest_mask));
	KUNIT_EXPECT_EQ(test, atomic_read(&cpupri_test_fitness_calls), 2);

	cpupri_test_reject_all_cpus = true;
	atomic_set(&cpupri_test_fitness_calls, 0);
	ret = cpupri_find_fitness(&cp, task, lowest_mask,
				  cpupri_test_fitness);
	KUNIT_EXPECT_EQ(test, ret, 1);
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu0, lowest_mask));
	KUNIT_EXPECT_TRUE(test, cpumask_test_cpu(cpu1, lowest_mask));
	KUNIT_EXPECT_EQ(test, atomic_read(&cpupri_test_fitness_calls), 2);

	cpupri_test_reject_all_cpus = false;
	cpupri_test_rejected_cpu = -1;
	cpupri_cleanup(&cp);
	kfree(task);
	free_cpumask_var(lowest_mask);
}

static int cpupri_test_worker_thread(void *data)
{
	static const int priorities[] = { 99, 80, 50, 20 };
	struct cpupri_test_worker *worker = data;
	int i;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < CPUPRI_TEST_UPDATES; i++) {
		int selector = (i + worker->index) % ARRAY_SIZE(priorities);

		cpupri_set(worker->cp, worker->cpu, priorities[selector]);
	}

	atomic_inc(worker->done);
	return 0;
}

static void cpupri_test_concurrent_transitions(struct kunit *test)
{
	struct cpupri_test_worker workers[CPUPRI_TEST_WORKERS] = {};
	struct task_struct *tasks[CPUPRI_TEST_WORKERS] = {};
	struct task_struct *task;
	struct cpupri cp;
	cpumask_var_t lowest_mask;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	unsigned int cpus[CPUPRI_TEST_WORKERS];
	unsigned int cpu;
	int workers_started = 0;
	int errors = 0;
	int nr_workers = 0;
	int i;

	for (cpu = cpumask_first(cpu_active_mask); cpu < nr_cpu_ids;
	     cpu = cpumask_next(cpu, cpu_active_mask)) {
		if (nr_workers == ARRAY_SIZE(cpus))
			break;
		cpus[nr_workers++] = cpu;
	}
	if (nr_workers < 2)
		kunit_skip(test, "concurrent transition test needs two active CPUs");

	KUNIT_ASSERT_TRUE(test, zalloc_cpumask_var(&lowest_mask, GFP_KERNEL));
	task = cpupri_test_alloc_task(0);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	cpumask_copy(&task->cpus_mask, cpu_active_mask);
	KUNIT_ASSERT_EQ(test, cpupri_init(&cp), 0);

	for (i = 0; i < nr_workers; i++) {
		cpupri_set(&cp, cpus[i], 50);
		workers[i].cp = &cp;
		workers[i].ready = &ready;
		workers[i].start = &start;
		workers[i].done = &done;
		workers[i].cpu = cpus[i];
		workers[i].index = i;
		tasks[i] = kthread_run(cpupri_test_worker_thread, &workers[i],
				       "cpupri-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start cpupri worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		workers_started++;
	}

	if (!cpupri_test_wait_for(&ready, nr_workers)) {
		KUNIT_FAIL(test, "cpupri workers did not become ready");
		goto cleanup;
	}

	atomic_set(&start, 1);
	for (i = 0; i < CPUPRI_TEST_SEARCHES; i++) {
		if (!cpupri_find(&cp, task, lowest_mask) ||
		    cpumask_empty(lowest_mask))
			errors++;
		if (!(i & 255))
			cond_resched();
	}
	if (!cpupri_test_wait_for(&done, nr_workers)) {
		KUNIT_FAIL(test, "cpupri workers did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, errors, 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&done), nr_workers);

cleanup:
	atomic_set(&start, 1);
	for (i = 0; i < workers_started; i++)
		kthread_stop(tasks[i]);
	cpupri_cleanup(&cp);
	kfree(task);
	free_cpumask_var(lowest_mask);
}

static struct kunit_case cpupri_test_cases[] = {
	KUNIT_CASE(cpupri_test_priority_mapping),
	KUNIT_CASE(cpupri_test_exhaustive_search_order),
	KUNIT_CASE(cpupri_test_find_boundaries),
	KUNIT_CASE(cpupri_test_lowest_priority_and_affinity),
	KUNIT_CASE(cpupri_test_fitness_filter_and_fallback),
	KUNIT_CASE(cpupri_test_concurrent_transitions),
	{}
};

static struct kunit_suite cpupri_test_suite = {
	.name = "cpupri",
	.test_cases = cpupri_test_cases,
};

kunit_test_suite(cpupri_test_suite);

MODULE_DESCRIPTION("KUnit tests for CPU-priority scheduling state");
MODULE_LICENSE("GPL");
