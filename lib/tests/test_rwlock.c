// SPDX-License-Identifier: GPL-2.0-or-later

#include <kunit/test.h>

#include <linux/atomic.h>
#include <linux/cpu.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/rwlock.h>
#include <linux/rwlock-rust.h>
#include <linux/sched.h>

#define RWLOCK_TEST_TIMEOUT_MS		30000
#define RWLOCK_TEST_CONTENDERS		4
#define RWLOCK_TEST_ITERATIONS		200

enum rwlock_test_kind {
	RWLOCK_TEST_READ,
	RWLOCK_TEST_WRITE,
};

struct rwlock_test_worker {
	rwlock_t *lock;
	atomic_t *ready;
	atomic_t *start;
	atomic_t *entered;
	atomic_t *done;
	atomic_t *order_count;
	int *order;
	int index;
	enum rwlock_test_kind kind;
};

struct rwlock_test_contender {
	rwlock_t *lock;
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

static bool rwlock_test_wait_for(atomic_t *counter, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(RWLOCK_TEST_TIMEOUT_MS);

	while (atomic_read(counter) < target && time_before(jiffies, deadline))
		cond_resched();

	return atomic_read(counter) >= target;
}

static bool rwlock_test_wait_for_waiting(rwlock_t *lock)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(RWLOCK_TEST_TIMEOUT_MS);

	while (time_before(jiffies, deadline)) {
		if (atomic_read(&lock->raw_lock.cnts) & QRW_WAITING)
			return true;
		cpu_relax();
	}

	return false;
}

static bool rwlock_test_wait_for_entered(atomic_t *entered, int target)
{
	unsigned long deadline =
		jiffies + msecs_to_jiffies(RWLOCK_TEST_TIMEOUT_MS);

	while (atomic_read(entered) < target && time_before(jiffies, deadline))
		cpu_relax();

	return atomic_read(entered) >= target;
}

static void rwlock_test_idle_until_stopped(void)
{
	while (!kthread_should_stop()) {
		set_current_state(TASK_IDLE);
		if (!kthread_should_stop())
			schedule();
	}
	__set_current_state(TASK_RUNNING);
}

static int rwlock_test_worker_thread(void *data)
{
	struct rwlock_test_worker *worker = data;
	int slot;

	atomic_inc(worker->ready);
	while (!atomic_read(worker->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	atomic_inc(worker->entered);
	if (worker->kind == RWLOCK_TEST_WRITE)
		write_lock(worker->lock);
	else
		read_lock(worker->lock);

	slot = atomic_inc_return(worker->order_count) - 1;
	if (slot < 2)
		worker->order[slot] = worker->index;

	if (worker->kind == RWLOCK_TEST_WRITE)
		write_unlock(worker->lock);
	else
		read_unlock(worker->lock);

	atomic_inc(worker->done);
	rwlock_test_idle_until_stopped();
	return 0;
}

static void rwlock_test_stop_workers(struct task_struct **tasks, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop(tasks[i]);
	}
}

static void rwlock_test_fast_paths(struct kunit *test)
{
	rwlock_t lock;

	rwlock_init(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);

	read_lock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), QRW_BIAS);
	read_unlock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);

	write_lock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), QRW_LOCKED);
	KUNIT_EXPECT_EQ(test, lock.raw_lock.wlocked, QRW_LOCKED);
	write_unlock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);
}

static void rwlock_test_trylocks(struct kunit *test)
{
	rwlock_t lock;

	rwlock_init(&lock);
	KUNIT_EXPECT_EQ(test, read_trylock(&lock), 1);
	KUNIT_EXPECT_EQ(test, read_trylock(&lock), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts),
			2 * QRW_BIAS);
	KUNIT_EXPECT_EQ(test, write_trylock(&lock), 0);
	read_unlock(&lock);
	read_unlock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);

	KUNIT_EXPECT_EQ(test, write_trylock(&lock), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), QRW_LOCKED);
	KUNIT_EXPECT_EQ(test, read_trylock(&lock), 0);
	KUNIT_EXPECT_EQ(test, write_trylock(&lock), 0);
	write_unlock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);
}

static void rwlock_test_reader_slow_path(struct kunit *test)
{
	rwlock_t lock;

	rwlock_init(&lock);
	atomic_set(&lock.raw_lock.cnts, QRW_WAITING);

	read_lock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts),
			QRW_WAITING | QRW_BIAS);
	read_unlock(&lock);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), QRW_WAITING);

	atomic_set(&lock.raw_lock.cnts, 0);
}

static void rwlock_test_writer_waiting_blocks_readers(struct kunit *test)
{
	struct rwlock_test_worker workers[2] = {};
	struct task_struct *tasks[2] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t entered = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t order_count = ATOMIC_INIT(0);
	atomic_t writer_start = ATOMIC_INIT(0);
	atomic_t reader_start = ATOMIC_INIT(0);
	int order[2] = {};
	rwlock_t lock;
	bool main_locked = false;
	int i;

	if (num_online_cpus() < 2)
		kunit_skip(test, "contended spinlock test requires multiple CPUs");

	workers[0].lock = &lock;
	workers[0].ready = &ready;
	workers[0].start = &writer_start;
	workers[0].entered = &entered;
	workers[0].done = &done;
	workers[0].order_count = &order_count;
	workers[0].order = order;
	workers[0].index = 0;
	workers[0].kind = RWLOCK_TEST_WRITE;
	workers[1].lock = &lock;
	workers[1].ready = &ready;
	workers[1].start = &reader_start;
	workers[1].entered = &entered;
	workers[1].done = &done;
	workers[1].order_count = &order_count;
	workers[1].order = order;
	workers[1].index = 1;
	workers[1].kind = RWLOCK_TEST_READ;

	rwlock_init(&lock);
	for (i = 0; i < ARRAY_SIZE(workers); i++) {
		tasks[i] = kthread_run(rwlock_test_worker_thread, &workers[i],
				       "rwlock-%s", i ? "reader" : "writer");
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start rwlock worker");
			tasks[i] = NULL;
			goto cleanup;
		}
	}
	if (!rwlock_test_wait_for(&ready, ARRAY_SIZE(workers))) {
		KUNIT_FAIL(test, "rwlock workers did not become ready");
		goto cleanup;
	}

	read_lock(&lock);
	main_locked = true;
	atomic_set(&writer_start, 1);
	if (!rwlock_test_wait_for_waiting(&lock)) {
		KUNIT_FAIL(test, "writer did not publish the waiting flag");
		goto cleanup;
	}

	atomic_set(&reader_start, 1);
	if (!rwlock_test_wait_for_entered(&entered, 2)) {
		KUNIT_FAIL(test, "reader did not enter the lock path");
		goto cleanup;
	}

	read_unlock(&lock);
	main_locked = false;
	if (!rwlock_test_wait_for(&done, 2)) {
		KUNIT_FAIL(test, "contended rwlock workers did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, order[0], 0);
	KUNIT_EXPECT_EQ(test, order[1], 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);

cleanup:
	if (main_locked)
		read_unlock(&lock);
	rwlock_test_stop_workers(tasks, ARRAY_SIZE(tasks));
}

static int rwlock_test_contender_thread(void *data)
{
	struct rwlock_test_contender *contender = data;
	int i;

	atomic_inc(contender->ready);
	while (!atomic_read(contender->start) && !kthread_should_stop())
		cond_resched();
	if (kthread_should_stop())
		return 0;

	for (i = 0; i < RWLOCK_TEST_ITERATIONS; i++) {
		if ((i + contender->index) & 1) {
			int active;
			int max;

			write_lock(contender->lock);
			active = atomic_inc_return(contender->writers);
			max = atomic_read(contender->max_writers);
			while (active > max &&
			       atomic_cmpxchg(contender->max_writers, max,
					      active) != max)
				max = atomic_read(contender->max_writers);
			if (atomic_read(contender->readers) || active != 1)
				atomic_inc(contender->errors);
			cpu_relax();
			atomic_dec(contender->writers);
			write_unlock(contender->lock);
		} else {
			read_lock(contender->lock);
			atomic_inc(contender->readers);
			if (atomic_read(contender->writers))
				atomic_inc(contender->errors);
			cpu_relax();
			atomic_dec(contender->readers);
			read_unlock(contender->lock);
		}
		atomic_inc(contender->operations);
	}

	atomic_inc(contender->done);
	rwlock_test_idle_until_stopped();
	return 0;
}

static void rwlock_test_concurrent_access(struct kunit *test)
{
	struct rwlock_test_contender contenders[RWLOCK_TEST_CONTENDERS] = {};
	struct task_struct *tasks[RWLOCK_TEST_CONTENDERS] = {};
	atomic_t ready = ATOMIC_INIT(0);
	atomic_t start = ATOMIC_INIT(0);
	atomic_t done = ATOMIC_INIT(0);
	atomic_t readers = ATOMIC_INIT(0);
	atomic_t writers = ATOMIC_INIT(0);
	atomic_t max_writers = ATOMIC_INIT(0);
	atomic_t operations = ATOMIC_INIT(0);
	atomic_t errors = ATOMIC_INIT(0);
	rwlock_t lock;
	int started = 0;
	int i;

	if (num_online_cpus() < 2)
		kunit_skip(test, "contended spinlock test requires multiple CPUs");

	rwlock_init(&lock);
	for (i = 0; i < ARRAY_SIZE(contenders); i++) {
		contenders[i].lock = &lock;
		contenders[i].ready = &ready;
		contenders[i].start = &start;
		contenders[i].done = &done;
		contenders[i].readers = &readers;
		contenders[i].writers = &writers;
		contenders[i].max_writers = &max_writers;
		contenders[i].operations = &operations;
		contenders[i].errors = &errors;
		contenders[i].index = i;
		tasks[i] = kthread_run(rwlock_test_contender_thread,
				       &contenders[i], "rwlock-mixed-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start rwlock contender");
			tasks[i] = NULL;
			goto cleanup;
		}
		started++;
	}

	if (!rwlock_test_wait_for(&ready, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "rwlock contenders did not become ready");
		goto cleanup;
	}
	atomic_set(&start, 1);
	if (!rwlock_test_wait_for(&done, ARRAY_SIZE(contenders))) {
		KUNIT_FAIL(test, "rwlock contenders did not finish");
		goto cleanup;
	}

	KUNIT_EXPECT_EQ(test, atomic_read(&errors), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&max_writers), 1);
	KUNIT_EXPECT_EQ(test, atomic_read(&readers), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&writers), 0);
	KUNIT_EXPECT_EQ(test, atomic_read(&operations),
			RWLOCK_TEST_CONTENDERS * RWLOCK_TEST_ITERATIONS);
	KUNIT_EXPECT_EQ(test, atomic_read(&lock.raw_lock.cnts), 0);

cleanup:
	rwlock_test_stop_workers(tasks, started);
}

static struct kunit_case rwlock_test_cases[] = {
	KUNIT_CASE(rwlock_test_fast_paths),
	KUNIT_CASE(rwlock_test_trylocks),
	KUNIT_CASE(rwlock_test_reader_slow_path),
	KUNIT_CASE(rwlock_test_writer_waiting_blocks_readers),
	KUNIT_CASE(rwlock_test_concurrent_access),
	{}
};

static struct kunit_suite rwlock_test_suite = {
	.name = "rwlock",
	.test_cases = rwlock_test_cases,
};

kunit_test_suite(rwlock_test_suite);

MODULE_DESCRIPTION("KUnit tests for queued read-write spinlocks");
MODULE_LICENSE("GPL");
