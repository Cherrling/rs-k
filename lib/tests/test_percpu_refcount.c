// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/percpu-refcount.h>
#include <linux/rcupdate.h>

#define PERCPU_REF_TEST_TIMEOUT_MS 30000
#define PERCPU_REF_TEST_WORKERS 4
#define PERCPU_REF_TEST_SWITCHES 20

struct percpu_ref_test {
	struct percpu_ref ref;
	struct completion released;
	struct completion confirmed;
};

static void test_release(struct percpu_ref *ref)
{
	struct percpu_ref_test *test =
		container_of(ref, struct percpu_ref_test, ref);

	complete(&test->released);
}

static void test_confirm(struct percpu_ref *ref)
{
	struct percpu_ref_test *test =
		container_of(ref, struct percpu_ref_test, ref);

	complete(&test->confirmed);
}

static void percpu_ref_test_init(struct kunit *test,
				 struct percpu_ref_test *ctx,
				 unsigned int flags)
{
	init_completion(&ctx->released);
	init_completion(&ctx->confirmed);
	KUNIT_ASSERT_EQ(test, 0,
			percpu_ref_init(&ctx->ref, test_release, flags,
					GFP_KERNEL));
}

static bool percpu_ref_test_wait(struct completion *completion)
{
	return wait_for_completion_timeout(completion,
		       msecs_to_jiffies(PERCPU_REF_TEST_TIMEOUT_MS));
}

static void percpu_ref_test_basic(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, 0);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_get(&ctx.ref);
	percpu_ref_put(&ctx.ref);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_kill_and_confirm(&ctx.ref, test_confirm);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.confirmed));
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static void percpu_ref_test_mode_switch(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_ALLOW_REINIT);
	percpu_ref_get_many(&ctx.ref, 3);

	percpu_ref_switch_to_atomic_sync(&ctx.ref);
	KUNIT_EXPECT_TRUE(test,
			  ctx.ref.percpu_count_ptr & __PERCPU_REF_ATOMIC);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_switch_to_percpu(&ctx.ref);
	KUNIT_EXPECT_FALSE(test,
			   ctx.ref.percpu_count_ptr & __PERCPU_REF_ATOMIC);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_kill(&ctx.ref);
	percpu_ref_put_many(&ctx.ref, 3);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static void percpu_ref_test_init_atomic(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_INIT_ATOMIC);
	KUNIT_EXPECT_TRUE(test,
			  ctx.ref.percpu_count_ptr & __PERCPU_REF_ATOMIC);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));
	percpu_ref_switch_to_atomic(&ctx.ref, test_confirm);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.confirmed));
	KUNIT_EXPECT_TRUE(test, percpu_ref_tryget_live(&ctx.ref));
	percpu_ref_put(&ctx.ref);

	percpu_ref_kill(&ctx.ref);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static void percpu_ref_test_reinit(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_INIT_DEAD);
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_reinit(&ctx.ref);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_kill(&ctx.ref);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static void percpu_ref_test_resurrect(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_ALLOW_REINIT);
	percpu_ref_get(&ctx.ref);
	percpu_ref_kill_and_confirm(&ctx.ref, test_confirm);

	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.confirmed));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_resurrect(&ctx.ref);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_dying(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

	percpu_ref_kill(&ctx.ref);
	percpu_ref_put(&ctx.ref);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static void percpu_ref_test_tryget_live(struct kunit *test)
{
	struct percpu_ref_test ctx;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_ALLOW_REINIT);
	KUNIT_EXPECT_TRUE(test, percpu_ref_tryget_live(&ctx.ref));
	percpu_ref_put(&ctx.ref);

	percpu_ref_kill_and_confirm(&ctx.ref, test_confirm);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.confirmed));
	KUNIT_EXPECT_FALSE(test, percpu_ref_tryget_live(&ctx.ref));
	KUNIT_EXPECT_FALSE(test, percpu_ref_tryget_live(&ctx.ref));
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

struct percpu_ref_worker {
	struct percpu_ref *ref;
	atomic_t *ready;
	atomic64_t *operations;
};

static int percpu_ref_worker(void *data)
{
	struct percpu_ref_worker *worker = data;

	atomic_inc(worker->ready);
	while (!kthread_should_stop()) {
		percpu_ref_get_many(worker->ref, 3);
		percpu_ref_put_many(worker->ref, 3);
		atomic64_inc(worker->operations);
		cond_resched();
	}
	return 0;
}

static void percpu_ref_test_concurrent_switch(struct kunit *test)
{
	struct percpu_ref_test ctx;
	struct task_struct *tasks[PERCPU_REF_TEST_WORKERS] = {};
	struct percpu_ref_worker workers[PERCPU_REF_TEST_WORKERS];
	atomic_t ready = ATOMIC_INIT(0);
	atomic64_t operations = ATOMIC64_INIT(0);
	unsigned long deadline;
	unsigned int started = 0;
	int i;

	percpu_ref_test_init(test, &ctx, PERCPU_REF_ALLOW_REINIT);

	for (i = 0; i < PERCPU_REF_TEST_WORKERS; i++) {
		workers[i].ref = &ctx.ref;
		workers[i].ready = &ready;
		workers[i].operations = &operations;
		tasks[i] = kthread_run(percpu_ref_worker, &workers[i],
				       "percpu-ref-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "failed to start percpu_ref worker");
			goto cleanup;
		}
		started++;
	}

	deadline = jiffies + msecs_to_jiffies(PERCPU_REF_TEST_TIMEOUT_MS);
	while (atomic_read(&ready) < PERCPU_REF_TEST_WORKERS &&
	       time_before(jiffies, deadline))
		cond_resched();
	if (atomic_read(&ready) != PERCPU_REF_TEST_WORKERS) {
		KUNIT_FAIL(test, "percpu_ref workers did not become ready");
		goto cleanup;
	}

	for (i = 0; i < PERCPU_REF_TEST_SWITCHES; i++) {
		percpu_ref_switch_to_atomic_sync(&ctx.ref);
		percpu_ref_switch_to_percpu(&ctx.ref);
	}

	KUNIT_EXPECT_GT(test, atomic64_read(&operations), 0);
	KUNIT_EXPECT_FALSE(test, percpu_ref_is_zero(&ctx.ref));

cleanup:
	for (i = 0; i < started; i++)
		kthread_stop(tasks[i]);

	percpu_ref_kill(&ctx.ref);
	KUNIT_EXPECT_TRUE(test, percpu_ref_test_wait(&ctx.released));
	KUNIT_EXPECT_TRUE(test, percpu_ref_is_zero(&ctx.ref));

	rcu_barrier();
	percpu_ref_exit(&ctx.ref);
}

static struct kunit_case percpu_ref_test_cases[] = {
	KUNIT_CASE(percpu_ref_test_basic),
	KUNIT_CASE(percpu_ref_test_mode_switch),
	KUNIT_CASE(percpu_ref_test_init_atomic),
	KUNIT_CASE(percpu_ref_test_reinit),
	KUNIT_CASE(percpu_ref_test_resurrect),
	KUNIT_CASE(percpu_ref_test_tryget_live),
	KUNIT_CASE(percpu_ref_test_concurrent_switch),
	{}
};

static struct kunit_suite percpu_ref_test_suite = {
	.name = "percpu_ref",
	.test_cases = percpu_ref_test_cases,
};

kunit_test_suites(&percpu_ref_test_suite);

MODULE_DESCRIPTION("percpu_ref() KUnit test suite");
MODULE_LICENSE("GPL");
