// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/semaphore-rust.h>

#define SEMAPHORE_TEST_TIMEOUT_MS 30000
#define SEMAPHORE_TEST_CONTENDERS 4
#define SEMAPHORE_TEST_ITERATIONS 200

struct semaphore_test_worker {
	struct semaphore *sem;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *wake_count;
	int *wake_order;
	int index;
	int ret;
	bool uninterruptible;
};

struct semaphore_test_contender {
	struct semaphore *sem;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *active;
	atomic_t *max_active;
	atomic_t *operations;
	atomic_t *errors;
};

static bool semaphore_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(SEMAPHORE_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static void semaphore_test_init_locked(struct kunit *test,
				       struct semaphore *sem)
{
	sema_init(sem, 1);
	KUNIT_ASSERT_EQ(test, down_trylock(sem), 0);
}

static unsigned int semaphore_test_count(struct semaphore *sem)
{
	unsigned long flags;
	unsigned int count;

	raw_spin_lock_irqsave(&sem->lock, flags);
	count = READ_ONCE(sem->count);
	raw_spin_unlock_irqrestore(&sem->lock, flags);
	return count;
}

static int semaphore_test_waiter_count(struct semaphore *sem)
{
	struct semaphore_waiter *first;
	struct list_head *cursor;
	unsigned long flags;
	int count = 0;

	raw_spin_lock_irqsave(&sem->lock, flags);
	first = sem->first_waiter;
	if (first) {
		count = 1;
		for (cursor = first->list.next; cursor != &first->list;
		     cursor = cursor->next)
			count++;
	}
	raw_spin_unlock_irqrestore(&sem->lock, flags);
	return count;
}

static bool semaphore_test_wait_for_waiters(struct semaphore *sem, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(SEMAPHORE_TEST_TIMEOUT_MS);

	while (semaphore_test_waiter_count(sem) < target &&
	       time_before(jiffies, deadline))
		cond_resched();

	return semaphore_test_waiter_count(sem) >= target;
}

static void semaphore_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int semaphore_test_worker_thread(void *data)
{
	struct semaphore_test_worker *worker = data;

	atomic_inc(worker->ready);
	if (worker->uninterruptible) {
		down(worker->sem);
		worker->ret = 0;
	} else {
		worker->ret = down_interruptible(worker->sem);
	}
	if (!worker->ret) {
		int slot;

		if (worker->wake_order) {
			slot = atomic_inc_return(worker->wake_count) - 1;
			worker->wake_order[slot] = worker->index;
		}
		up(worker->sem);
	}
	atomic_inc(worker->done);
	semaphore_test_idle_until_stopped();
	return 0;
}

static int semaphore_test_timeout_worker_thread(void *data)
{
	struct semaphore_test_worker *worker = data;

	atomic_inc(worker->ready);
	worker->ret = down_timeout(worker->sem, msecs_to_jiffies(1000));
	if (!worker->ret)
		up(worker->sem);
	atomic_inc(worker->done);
	semaphore_test_idle_until_stopped();
	return 0;
}

static int semaphore_test_contender_thread(void *data)
{
	struct semaphore_test_contender *contender = data;
	int i;

	atomic_inc(contender->ready);
	while (!atomic_read(contender->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < SEMAPHORE_TEST_ITERATIONS; i++) {
		int active;
		int max;

		if (down_interruptible(contender->sem)) {
			atomic_inc(contender->errors);
			break;
		}

		active = atomic_inc_return(contender->active);
		max = atomic_read(contender->max_active);
		while (active > max &&
		       atomic_cmpxchg(contender->max_active, max, active) != max)
			max = atomic_read(contender->max_active);
		cond_resched();
		atomic_dec(contender->active);
		atomic_inc(contender->operations);
		up(contender->sem);
	}

	atomic_inc(contender->done);
	semaphore_test_idle_until_stopped();
	return 0;
}

static void semaphore_test_stop_workers(struct task_struct **tasks,
					struct semaphore *sem, int count)
{
	int i;

	for (i = 0; i < count; i++)
		up(sem);
	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void semaphore_test_fast_paths(struct kunit *test)
{
	struct semaphore sem;
	unsigned long holder;

	sema_init(&sem, 2);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 0);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 0);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 1);
	KUNIT_EXPECT_EQ(test, semaphore_test_count(&sem), 0U);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);

	up(&sem);
	down(&sem);
	holder = sem_last_holder(&sem);
	KUNIT_EXPECT_EQ(test, semaphore_test_count(&sem), 0U);
	if (IS_ENABLED(CONFIG_DETECT_HUNG_TASK_BLOCKER))
		KUNIT_EXPECT_PTR_EQ(test, (void *)holder, current);
	up(&sem);
	if (IS_ENABLED(CONFIG_DETECT_HUNG_TASK_BLOCKER))
		KUNIT_EXPECT_EQ(test, sem_last_holder(&sem), 0UL);

	KUNIT_EXPECT_EQ(test, down_interruptible(&sem), 0);
	up(&sem);
	KUNIT_EXPECT_EQ(test, down_killable(&sem), 0);
	up(&sem);
	KUNIT_EXPECT_EQ(test, down_timeout(&sem, 0), 0);
	up(&sem);
	KUNIT_EXPECT_EQ(test, semaphore_test_count(&sem), 1U);
}

static void semaphore_test_timeout_and_signals(struct kunit *test)
{
	struct semaphore sem;
	int ret;

	semaphore_test_init_locked(test, &sem);
	ret = down_timeout(&sem, 0);
	KUNIT_EXPECT_EQ(test, ret, -ETIME);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);

	ret = down_timeout(&sem, 1);
	KUNIT_EXPECT_EQ(test, ret, -ETIME);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);

	allow_kernel_signal(SIGUSR1);
	KUNIT_EXPECT_EQ(test, send_sig(SIGUSR1, current, 1), 0);
	ret = down_interruptible(&sem);
	flush_signals(current);
	disallow_signal(SIGUSR1);

	KUNIT_EXPECT_EQ(test, ret, -EINTR);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);

	allow_kernel_signal(SIGKILL);
	KUNIT_EXPECT_EQ(test, send_sig(SIGKILL, current, 1), 0);
	ret = down_killable(&sem);
	flush_signals(current);
	disallow_signal(SIGKILL);

	KUNIT_EXPECT_EQ(test, ret, -EINTR);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_EQ(test, READ_ONCE(current->__state), TASK_RUNNING);
}

static void semaphore_test_blocking_handoff(struct kunit *test)
{
	struct semaphore_test_worker worker = {};
	struct task_struct *task;
	struct semaphore sem;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);

	semaphore_test_init_locked(test, &sem);
	worker.sem = &sem;
	worker.ready = &ready;
	worker.done = &done;
	worker.uninterruptible = true;
	task = kthread_run(semaphore_test_worker_thread, &worker,
			   "semaphore-handoff");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start semaphore worker");
		return;
	}

	if (!semaphore_test_wait_for(&ready, 1) ||
	    !semaphore_test_wait_for_waiters(&sem, 1)) {
		KUNIT_FAIL(test, "semaphore worker did not queue");
		goto cleanup;
	}
	KUNIT_EXPECT_EQ(test, semaphore_test_count(&sem), 0U);

	up(&sem);
	KUNIT_EXPECT_TRUE(test, semaphore_test_wait_for(&done, 1));
	KUNIT_EXPECT_EQ(test, worker.ret, 0);
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_EQ(test, semaphore_test_count(&sem), 1U);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 0);

cleanup:
	up(&sem);
	kthread_stop(task);
}

static void semaphore_test_timeout_handoff(struct kunit *test)
{
	struct semaphore_test_worker worker = {};
	struct task_struct *task;
	struct semaphore sem;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);

	semaphore_test_init_locked(test, &sem);
	worker.sem = &sem;
	worker.ready = &ready;
	worker.done = &done;
	task = kthread_run(semaphore_test_timeout_worker_thread, &worker,
			   "semaphore-timeout");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start timed semaphore worker");
		return;
	}

	if (!semaphore_test_wait_for(&ready, 1) ||
	    !semaphore_test_wait_for_waiters(&sem, 1)) {
		KUNIT_FAIL(test, "timed semaphore worker did not queue");
		goto cleanup;
	}
	up(&sem);
	KUNIT_EXPECT_TRUE(test, semaphore_test_wait_for(&done, 1));
	KUNIT_EXPECT_EQ(test, worker.ret, 0);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 0);

cleanup:
	up(&sem);
	kthread_stop(task);
}

static void semaphore_test_fifo_handoff(struct kunit *test)
{
	struct semaphore_test_worker workers[3] = {};
	struct task_struct *tasks[3] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int wake_order[3] = {};
	struct semaphore sem;
	int started = 0;
	int i;

	semaphore_test_init_locked(test, &sem);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		workers[i].sem = &sem;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].wake_count = &wake_count;
		workers[i].wake_order = wake_order;
		workers[i].index = i;
		tasks[i] = kthread_run(semaphore_test_worker_thread,
				       &workers[i], "semaphore-fifo-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start FIFO worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!semaphore_test_wait_for(&ready, i + 1) ||
		    !semaphore_test_wait_for_waiters(&sem, i + 1)) {
			KUNIT_FAIL(test, "FIFO worker did not queue");
			goto cleanup;
		}
	}

	up(&sem);
	if (!semaphore_test_wait_for(&done, ARRAY_SIZE(workers))) {
		KUNIT_FAIL(test, "FIFO workers did not finish");
		goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		KUNIT_EXPECT_EQ(test, workers[i].ret, 0);
		KUNIT_EXPECT_EQ(test, wake_order[i], i);
	}
	KUNIT_EXPECT_EQ(test, semaphore_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_EQ(test, down_trylock(&sem), 0);

cleanup:
	semaphore_test_stop_workers(tasks, &sem, started);
}

static void semaphore_test_concurrent_ownership(struct kunit *test)
{
	struct semaphore_test_contender contenders[SEMAPHORE_TEST_CONTENDERS] = {};
	struct task_struct *tasks[SEMAPHORE_TEST_CONTENDERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t active = ATOMIC_INIT(0);
	atomic_t max_active = ATOMIC_INIT(0);
	atomic_t operations = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct semaphore sem;
	int started = 0;
	int i;

	sema_init(&sem, 1);
	for (i = 0; i < ARRAY_SIZE(contenders); i++) {
		contenders[i].sem = &sem;
		contenders[i].ready = &ready;
		contenders[i].start = &start;
		contenders[i].done = &done;
		contenders[i].active = &active;
		contenders[i].max_active = &max_active;
		contenders[i].operations = &operations;
		contenders[i].errors = &errors;
		tasks[i] = kthread_run(semaphore_test_contender_thread,
				       &contenders[i], "semaphore-contend-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start semaphore contender");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!semaphore_test_wait_for(&ready, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "semaphore contenders did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!semaphore_test_wait_for(&done, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "semaphore contenders did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&max_active), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&active), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&operations),
			SEMAPHORE_TEST_CONTENDERS * SEMAPHORE_TEST_ITERATIONS);

cleanup:
	semaphore_test_stop_workers(tasks, &sem, started);
}

static struct kunit_case semaphore_test_cases[] = {
	KUNIT_CASE(semaphore_test_fast_paths),
	KUNIT_CASE(semaphore_test_timeout_and_signals),
	KUNIT_CASE(semaphore_test_blocking_handoff),
	KUNIT_CASE(semaphore_test_timeout_handoff),
	KUNIT_CASE(semaphore_test_fifo_handoff),
	KUNIT_CASE(semaphore_test_concurrent_ownership),
	{}
};

static struct kunit_suite semaphore_test_suite = {
	.name = "semaphore",
	.test_cases = semaphore_test_cases,
};

kunit_test_suite(semaphore_test_suite);

MODULE_DESCRIPTION("KUnit tests for counting semaphores");
MODULE_LICENSE("GPL");
