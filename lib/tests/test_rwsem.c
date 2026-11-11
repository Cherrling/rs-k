// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/rwsem.h>
#include <linux/rwsem-rust.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

#define RWSEM_TEST_TIMEOUT_MS		30000
#define RWSEM_TEST_FIFO_WORKERS		3
#define RWSEM_TEST_READERS		3
#define RWSEM_TEST_CONTENDERS		4
#define RWSEM_TEST_ITERATIONS		200

enum rwsem_test_kind {
	RWSEM_TEST_READ,
	RWSEM_TEST_READ_INTERRUPTIBLE,
	RWSEM_TEST_WRITE,
	RWSEM_TEST_WRITE_KILLABLE,
};

struct rwsem_test_worker {
	struct rw_semaphore *sem;
	atomic_t *ready;
	atomic_t *done;
	atomic_t *wake_count;
	int *wake_order;
	int index;
	int signal;
	int ret;
	unsigned long owner_after_lock;
	enum rwsem_test_kind kind;
};

struct rwsem_test_contender {
	struct rw_semaphore *sem;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *done;
	atomic_t *readers;
	atomic_t *writers;
	atomic_t *max_writers;
	atomic_t *operations;
	atomic_t *errors;
	int index;
};

static unsigned long rwsem_test_owner_task(struct rw_semaphore *sem)
{
	return atomic_long_read(&sem->owner) & ~RWSEM_OWNER_FLAGS_MASK;
}

static bool rwsem_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(RWSEM_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static int rwsem_test_waiter_count(struct rw_semaphore *sem)
{
	struct rwsem_waiter *first;
	struct list_head *cursor;
	unsigned long flags;
	int count = 0;

	raw_spin_lock_irqsave(&sem->wait_lock, flags);
	first = sem->first_waiter;
	if (first) {
		count = 1;
		for (cursor = first->list.next; cursor != &first->list;
		     cursor = cursor->next)
			count++;
	}
	raw_spin_unlock_irqrestore(&sem->wait_lock, flags);
	return count;
}

static bool rwsem_test_wait_for_waiters(struct rw_semaphore *sem, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(RWSEM_TEST_TIMEOUT_MS);

	while (time_before(jiffies, deadline)) {
		if (rwsem_test_waiter_count(sem) >= target)
			return true;
		cond_resched();
	}

	return false;
}

static void rwsem_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static void rwsem_test_record_wake(struct rwsem_test_worker *worker)
{
	int slot;

	if (!worker->wake_order)
		return;
	slot = atomic_inc_return(worker->wake_count) - 1;
	if (slot < RWSEM_TEST_FIFO_WORKERS)
		worker->wake_order[slot] = worker->index;
}

static int rwsem_test_worker_thread(void *data)
{
	struct rwsem_test_worker *worker = data;
	bool write = false;

	if (worker->signal)
		allow_kernel_signal(worker->signal);

	atomic_inc(worker->ready);
	switch (worker->kind) {
	case RWSEM_TEST_READ:
		down_read(worker->sem);
		worker->ret = 0;
		break;
	case RWSEM_TEST_READ_INTERRUPTIBLE:
		worker->ret = down_read_interruptible(worker->sem);
		break;
	case RWSEM_TEST_WRITE:
		down_write(worker->sem);
		worker->ret = 0;
		write = true;
		break;
	case RWSEM_TEST_WRITE_KILLABLE:
		worker->ret = down_write_killable(worker->sem);
		write = true;
		break;
	}

	if (worker->signal) {
		flush_signals(current);
		disallow_signal(worker->signal);
	}

	if (!worker->ret) {
		worker->owner_after_lock =
			rwsem_test_owner_task(worker->sem);
		rwsem_test_record_wake(worker);
		cond_resched();
		if (write)
			up_write(worker->sem);
		else
			up_read(worker->sem);
	}

	atomic_inc(worker->done);
	rwsem_test_idle_until_stopped();
	return 0;
}

static void rwsem_test_stop_workers(struct task_struct **tasks, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void rwsem_test_fast_paths(struct kunit *test)
{
	struct rw_semaphore sem;

	init_rwsem(&sem);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.owner), 0L);

	down_read(&sem);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)RWSEM_READER_BIAS);
	KUNIT_EXPECT_EQ(test, rwsem_test_owner_task(&sem),
			(unsigned long)current);
	KUNIT_EXPECT_NE(test, atomic_long_read(&sem.owner) &
			RWSEM_READER_OWNED, 0UL);

	KUNIT_EXPECT_EQ(test, down_read_trylock(&sem), 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)(2 * RWSEM_READER_BIAS));
	up_read(&sem);
	up_read(&sem);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));

	down_write(&sem);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)RWSEM_WRITER_LOCKED);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.owner),
			(unsigned long)current);
	KUNIT_EXPECT_EQ(test, down_write_trylock(&sem), 0);
	up_write(&sem);

	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.owner), 0L);
}

static void rwsem_test_contention_trylocks(struct kunit *test)
{
	struct rw_semaphore sem;

	init_rwsem(&sem);
	down_write(&sem);
	KUNIT_EXPECT_EQ(test, down_read_trylock(&sem), 0);
	KUNIT_EXPECT_EQ(test, down_write_trylock(&sem), 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)RWSEM_WRITER_LOCKED);
	up_write(&sem);

	down_read(&sem);
	KUNIT_EXPECT_EQ(test, down_write_trylock(&sem), 0);
	KUNIT_EXPECT_EQ(test, down_read_trylock(&sem), 1);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)(2 * RWSEM_READER_BIAS));
	up_read(&sem);
	up_read(&sem);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));
}

static void rwsem_test_reader_slow_steal(struct kunit *test)
{
	struct rw_semaphore sem;

	init_rwsem(&sem);
	atomic_long_set(&sem.count, RWSEM_FLAG_WAITERS);

	down_read(&sem);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)(RWSEM_READER_BIAS | RWSEM_FLAG_WAITERS));
	KUNIT_EXPECT_EQ(test, rwsem_test_owner_task(&sem),
			(unsigned long)current);
	KUNIT_EXPECT_NE(test, atomic_long_read(&sem.owner) &
			RWSEM_READER_OWNED, 0UL);

	up_read(&sem);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)RWSEM_FLAG_WAITERS);
	KUNIT_EXPECT_EQ(test, rwsem_test_waiter_count(&sem), 0);

	atomic_long_set(&sem.count, 0);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));
}

static void rwsem_test_blocking_readers(struct kunit *test)
{
	struct rwsem_test_worker workers[RWSEM_TEST_READERS] = {};
	struct task_struct *tasks[RWSEM_TEST_READERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	struct rw_semaphore sem;
	int started = 0;
	bool unlocked = false;
	int i;

	init_rwsem(&sem);
	down_write(&sem);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		workers[i].sem = &sem;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].kind = RWSEM_TEST_READ;
		tasks[i] = kthread_run(rwsem_test_worker_thread, &workers[i],
				       "rwsem-read-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start reader worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!rwsem_test_wait_for(&ready, i + 1) ||
		    !rwsem_test_wait_for_waiters(&sem, i + 1)) {
			KUNIT_FAIL(test, "reader worker did not queue");
			goto cleanup;
		}
	}

	up_write(&sem);
	unlocked = true;
	if (!rwsem_test_wait_for(&done, ARRAY_SIZE(workers))) {
		KUNIT_FAIL(test, "reader workers did not finish");
		goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(workers); i++)
		KUNIT_EXPECT_EQ(test, workers[i].ret, 0);
	KUNIT_EXPECT_EQ(test, rwsem_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));

cleanup:
	if (!unlocked)
		up_write(&sem);
	rwsem_test_stop_workers(tasks, started);
}

static void rwsem_test_signal_worker(struct kunit *test, int signal,
				     enum rwsem_test_kind kind)
{
	struct rwsem_test_worker worker = {};
	struct task_struct *task = NULL;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	struct rw_semaphore sem;
	bool unlocked = false;

	init_rwsem(&sem);
	down_write(&sem);
	worker.sem = &sem;
	worker.ready = &ready;
	worker.done = &done;
	worker.signal = signal;
	worker.kind = kind;
	task = kthread_run(rwsem_test_worker_thread, &worker, "rwsem-signal");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start signal worker");
		task = NULL;
		goto cleanup;
	}

	if (!rwsem_test_wait_for(&ready, 1) ||
	    !rwsem_test_wait_for_waiters(&sem, 1)) {
		KUNIT_FAIL(test, "signal worker did not queue");
		goto cleanup;
	}
	if (send_sig(signal, task, 1)) {
		KUNIT_FAIL(test, "failed to signal rwsem waiter");
		goto cleanup;
	}
	if (!rwsem_test_wait_for(&done, 1)) {
		KUNIT_FAIL(test, "signal worker did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, worker.ret, -EINTR);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.owner),
			(unsigned long)current);
	KUNIT_EXPECT_EQ(test, rwsem_test_waiter_count(&sem), 0);

cleanup:
	if (!unlocked) {
		up_write(&sem);
		unlocked = true;
	}
	rwsem_test_stop_workers(&task, 1);
}

static void rwsem_test_interruptible_and_killable(struct kunit *test)
{
	rwsem_test_signal_worker(test, SIGUSR1,
				 RWSEM_TEST_READ_INTERRUPTIBLE);
	rwsem_test_signal_worker(test, SIGKILL, RWSEM_TEST_WRITE_KILLABLE);
}

static void rwsem_test_writer_fifo(struct kunit *test)
{
	struct rwsem_test_worker workers[RWSEM_TEST_FIFO_WORKERS] = {};
	struct task_struct *tasks[RWSEM_TEST_FIFO_WORKERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t wake_count = ATOMIC_INIT(0);
	int wake_order[RWSEM_TEST_FIFO_WORKERS] = {};
	struct rw_semaphore sem;
	int started = 0;
	bool unlocked = false;
	int i;

	init_rwsem(&sem);
	down_write(&sem);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		workers[i].sem = &sem;
		workers[i].ready = &ready;
		workers[i].done = &done;
		workers[i].wake_count = &wake_count;
		workers[i].wake_order = wake_order;
		workers[i].index = i;
		workers[i].kind = RWSEM_TEST_WRITE;
		tasks[i] = kthread_run(rwsem_test_worker_thread, &workers[i],
				       "rwsem-write-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start writer worker");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
		if (!rwsem_test_wait_for(&ready, i + 1) ||
		    !rwsem_test_wait_for_waiters(&sem, i + 1)) {
			KUNIT_FAIL(test, "writer worker did not queue");
			goto cleanup;
		}
	}

	up_write(&sem);
	unlocked = true;
	if (!rwsem_test_wait_for(&done, ARRAY_SIZE(workers))) {
		KUNIT_FAIL(test, "writer workers did not finish");
		goto cleanup;
	}

	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		KUNIT_EXPECT_EQ(test, workers[i].ret, 0);
		KUNIT_EXPECT_EQ(test, wake_order[i], i);
		KUNIT_EXPECT_PTR_EQ(test, (void *)workers[i].owner_after_lock,
				    tasks[i]);
	}
	KUNIT_EXPECT_EQ(test, rwsem_test_waiter_count(&sem), 0);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));

cleanup:
	if (!unlocked)
		up_write(&sem);
	rwsem_test_stop_workers(tasks, started);
}

static void rwsem_test_downgrade(struct kunit *test)
{
	struct rwsem_test_worker worker = {};
	struct task_struct *task = NULL;
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	struct rw_semaphore sem;
	bool downgraded = false;

	init_rwsem(&sem);
	down_write(&sem);
	worker.sem = &sem;
	worker.ready = &ready;
	worker.done = &done;
	worker.kind = RWSEM_TEST_READ;
	task = kthread_run(rwsem_test_worker_thread, &worker, "rwsem-down");
	if (IS_ERR_OR_NULL(task)) {
		KUNIT_FAIL(test, "failed to start downgrade reader");
		task = NULL;
		goto cleanup;
	}
	if (!rwsem_test_wait_for(&ready, 1) ||
	    !rwsem_test_wait_for_waiters(&sem, 1)) {
		KUNIT_FAIL(test, "downgrade reader did not queue");
		goto cleanup;
	}

	downgrade_write(&sem);
	downgraded = true;
	if (!rwsem_test_wait_for(&done, 1)) {
		KUNIT_FAIL(test, "downgrade reader did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, worker.ret, 0);
	KUNIT_EXPECT_EQ(test, atomic_long_read(&sem.count),
			(long)RWSEM_READER_BIAS);
	KUNIT_EXPECT_EQ(test, rwsem_test_owner_task(&sem),
			(unsigned long)current);
	KUNIT_EXPECT_EQ(test, rwsem_test_waiter_count(&sem), 0);

cleanup:
	if (downgraded)
		up_read(&sem);
	else
		up_write(&sem);
	rwsem_test_stop_workers(&task, 1);
}

static int rwsem_test_contender_thread(void *data)
{
	struct rwsem_test_contender *contender = data;
	int i;

	atomic_inc(contender->ready);
	while (!atomic_read(contender->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < RWSEM_TEST_ITERATIONS; i++) {
		if ((i + contender->index) & 1) {
			int active;
			int max;

			down_write(contender->sem);
			active = atomic_inc_return(contender->writers);
			max = atomic_read(contender->max_writers);
			while (active > max &&
			       atomic_cmpxchg(contender->max_writers, max,
					      active) != max)
				max = atomic_read(contender->max_writers);
			if (atomic_read(contender->readers) || active != 1)
				atomic_inc(contender->errors);
			cond_resched();
			atomic_dec(contender->writers);
			up_write(contender->sem);
		} else {
			down_read(contender->sem);
			atomic_inc(contender->readers);
			if (atomic_read(contender->writers))
				atomic_inc(contender->errors);
			cond_resched();
			atomic_dec(contender->readers);
			up_read(contender->sem);
		}
		atomic_inc(contender->operations);
	}

	atomic_inc(contender->done);
	rwsem_test_idle_until_stopped();
	return 0;
}

static void rwsem_test_concurrent_access(struct kunit *test)
{
	struct rwsem_test_contender contenders[RWSEM_TEST_CONTENDERS] = {};
	struct task_struct *tasks[RWSEM_TEST_CONTENDERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t readers = ATOMIC_INIT(0);
	atomic_t writers = ATOMIC_INIT(0);
	atomic_t max_writers = ATOMIC_INIT(0);
	atomic_t operations = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	struct rw_semaphore sem;
	int started = 0;
	int i;

	init_rwsem(&sem);
	for (i = 0; i < ARRAY_SIZE(contenders); i++) {
		contenders[i].sem = &sem;
		contenders[i].ready = &ready;
		contenders[i].start = &start;
		contenders[i].done = &done;
		contenders[i].readers = &readers;
		contenders[i].writers = &writers;
		contenders[i].max_writers = &max_writers;
		contenders[i].operations = &operations;
		contenders[i].errors = &errors;
		contenders[i].index = i;
		tasks[i] = kthread_run(rwsem_test_contender_thread,
				       &contenders[i], "rwsem-mixed-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start rwsem contender");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!rwsem_test_wait_for(&ready, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "rwsem contenders did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!rwsem_test_wait_for(&done, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "rwsem contenders did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&max_writers), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&readers), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&writers), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&operations),
			RWSEM_TEST_CONTENDERS * RWSEM_TEST_ITERATIONS);
	KUNIT_EXPECT_FALSE(test, rwsem_is_locked(&sem));

cleanup:
	rwsem_test_stop_workers(tasks, started);
}

static struct kunit_case rwsem_test_cases[] = {
	KUNIT_CASE(rwsem_test_fast_paths),
	KUNIT_CASE(rwsem_test_contention_trylocks),
	KUNIT_CASE(rwsem_test_reader_slow_steal),
	KUNIT_CASE(rwsem_test_blocking_readers),
	KUNIT_CASE(rwsem_test_interruptible_and_killable),
	KUNIT_CASE(rwsem_test_writer_fifo),
	KUNIT_CASE(rwsem_test_downgrade),
	KUNIT_CASE(rwsem_test_concurrent_access),
	{}
};

static struct kunit_suite rwsem_test_suite = {
	.name = "rwsem",
	.test_cases = rwsem_test_cases,
};

kunit_test_suite(rwsem_test_suite);

MODULE_DESCRIPTION("KUnit tests for read-write semaphores");
MODULE_LICENSE("GPL");
