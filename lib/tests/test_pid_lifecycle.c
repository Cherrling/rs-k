// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/kthread.h>
#include <linux/pid.h>
#include <linux/pid-rust.h>
#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/string.h>

#define PID_LIFECYCLE_TEST_ROUNDS 24

static int pid_lifecycle_test_thread(void *data)
{
	struct completion *started = data;

	complete(started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

static void pid_lifecycle_test_null(struct kunit *test)
{
	put_pid(NULL);
}

static void pid_lifecycle_test_final_put(struct kunit *test)
{
	struct pid *pid;

	pid = kmem_cache_zalloc(init_pid_ns.pid_cachep, GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pid);

	get_pid_ns(&init_pid_ns);
	refcount_set(&pid->count, 2);
	pid->level = 0;
	pid->numbers[0].ns = &init_pid_ns;

	put_pid(pid);
	KUNIT_EXPECT_EQ(test, 1, refcount_read(&pid->count));
	put_pid(pid);
}

static void pid_lifecycle_test_nonfinal_put(struct kunit *test)
{
	struct pid *pid;

	pid = get_task_pid(current, PIDTYPE_PID);
	KUNIT_ASSERT_NOT_NULL(test, pid);
	KUNIT_EXPECT_GT(test, refcount_read(&pid->count), 1);
	put_pid(pid);
}

static void pid_lifecycle_test_reap_kthreads(struct kunit *test)
{
	struct task_struct *task;
	struct completion started;
	int round;

	for (round = 0; round < PID_LIFECYCLE_TEST_ROUNDS; round++) {
		init_completion(&started);
		task = kthread_run(pid_lifecycle_test_thread, &started,
				   "pid-lifecycle-%d", round);
		KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
		KUNIT_ASSERT_NE(test, 0,
				wait_for_completion_timeout(&started, HZ));
		get_task_struct(task);
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	}

	rcu_barrier();
}

#ifdef CONFIG_RUST_PID_LIFECYCLE
static void pid_lifecycle_test_count_action(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_NONE,
			rust_pid_test_count_action(0));
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_WAKE,
			rust_pid_test_count_action(1));
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_WAKE,
			rust_pid_test_count_action(2));
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_NONE,
			rust_pid_test_count_action(3));
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_WARN,
			rust_pid_test_count_action(PIDNS_ADDING));
	KUNIT_EXPECT_EQ(test, RUST_PID_COUNT_ACTION_NONE,
			rust_pid_test_count_action(UINT_MAX));
}

static void pid_lifecycle_test_release_order(struct kunit *test)
{
	struct pid *pids[PIDTYPE_MAX] = {};
	unsigned int order[PIDTYPE_MAX] = {};
	unsigned int storage[PIDTYPE_MAX];

	pids[PIDTYPE_PID] = (struct pid *)&storage[0];
	pids[PIDTYPE_TGID] = (struct pid *)&storage[1];
	pids[PIDTYPE_SID] = (struct pid *)&storage[3];

	KUNIT_EXPECT_EQ(test, 3,
			rust_pid_test_visit_order(pids, order, ARRAY_SIZE(order)));
	KUNIT_EXPECT_EQ(test, PIDTYPE_SID, order[0]);
	KUNIT_EXPECT_EQ(test, PIDTYPE_TGID, order[1]);
	KUNIT_EXPECT_EQ(test, PIDTYPE_PID, order[2]);

	pids[PIDTYPE_PGID] = (struct pid *)&storage[2];
	KUNIT_EXPECT_EQ(test, PIDTYPE_MAX,
			rust_pid_test_visit_order(pids, order, ARRAY_SIZE(order)));
	KUNIT_EXPECT_EQ(test, PIDTYPE_SID, order[0]);
	KUNIT_EXPECT_EQ(test, PIDTYPE_PGID, order[1]);
	KUNIT_EXPECT_EQ(test, PIDTYPE_TGID, order[2]);
	KUNIT_EXPECT_EQ(test, PIDTYPE_PID, order[3]);

	memset(pids, 0, sizeof(pids));
	KUNIT_EXPECT_EQ(test, 0,
			rust_pid_test_visit_order(pids, order, ARRAY_SIZE(order)));
}
#endif

static struct kunit_case pid_lifecycle_test_cases[] = {
#ifdef CONFIG_RUST_PID_LIFECYCLE
	KUNIT_CASE(pid_lifecycle_test_count_action),
	KUNIT_CASE(pid_lifecycle_test_release_order),
#endif
	KUNIT_CASE(pid_lifecycle_test_null),
	KUNIT_CASE(pid_lifecycle_test_nonfinal_put),
	KUNIT_CASE(pid_lifecycle_test_final_put),
	KUNIT_CASE(pid_lifecycle_test_reap_kthreads),
	{}
};

static struct kunit_suite pid_lifecycle_test_suite = {
	.name = "pid_lifecycle",
	.test_cases = pid_lifecycle_test_cases,
};

kunit_test_suite(pid_lifecycle_test_suite);

MODULE_DESCRIPTION("KUnit tests for PID lifecycle release");
MODULE_LICENSE("GPL");
