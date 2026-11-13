// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/pid.h>
#include <linux/pid-allocation-rust.h>
#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/task.h>

#define PID_ALLOCATION_REPEAT_COUNT 32
#define PID_ALLOCATION_WORKERS 16

struct pid_allocation_context {
	struct pid *pid;
	struct completion started;
	struct completion *go;
};

static int pid_allocation_thread(void *data)
{
	struct pid_allocation_context *ctx = data;

	complete(&ctx->started);
	wait_for_completion(ctx->go);
	ctx->pid = alloc_pid(&init_pid_ns, NULL, 0);

	return 0;
}

static void pid_allocation_release_all(struct pid **pids, int count)
{
	int i;

	for (i = 0; i < count; i++) {
		if (!IS_ERR_OR_NULL(pids[i]))
			free_pid(pids[i]);
	}
	rcu_barrier();
}

static void pid_allocation_test_default(struct kunit *test)
{
	struct pid *pid;

	pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid));
	KUNIT_EXPECT_GE(test, pid_nr(pid), 1);

	free_pid(pid);
	rcu_barrier();
}

static void pid_allocation_test_invalid_set_tid(struct kunit *test)
{
	struct pid *pid;
	pid_t tid;

	pid = alloc_pid(&init_pid_ns, NULL, init_pid_ns.level + 2);
	KUNIT_ASSERT_TRUE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(pid));

	tid = 0;
	pid = alloc_pid(&init_pid_ns, &tid, 1);
	KUNIT_ASSERT_TRUE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(pid));

	tid = READ_ONCE(init_pid_ns.pid_max);
	pid = alloc_pid(&init_pid_ns, &tid, 1);
	KUNIT_ASSERT_TRUE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(pid));
}

static void pid_allocation_test_set_tid_collision(struct kunit *test)
{
	struct pid *first;
	struct pid *second;
	pid_t requested;
	int pid_max;

	pid_max = READ_ONCE(init_pid_ns.pid_max);
	first = ERR_PTR(-EEXIST);

	for (requested = pid_max - 1;
	     requested >= 1 && requested > pid_max - 128;
	     requested--) {
		first = alloc_pid(&init_pid_ns, &requested, 1);
		if (!IS_ERR(first))
			break;
		KUNIT_ASSERT_EQ(test, -EEXIST, PTR_ERR(first));
	}

	KUNIT_ASSERT_FALSE(test, IS_ERR(first));
	KUNIT_EXPECT_EQ(test, requested, pid_nr(first));

	second = alloc_pid(&init_pid_ns, &requested, 1);
	KUNIT_ASSERT_TRUE(test, IS_ERR(second));
	KUNIT_EXPECT_EQ(test, -EEXIST, PTR_ERR(second));

	free_pid(first);
	rcu_barrier();
}

#ifdef CONFIG_PID_NS
static void pid_allocation_test_namespace_rollback(struct kunit *test)
{
	struct pid_namespace *nested;
	struct pid *parent_pid;
	struct pid *pid;
	pid_t requested[2];
	pid_t child_tid = RESERVED_PIDS + 1;
	int occupied;

	nested = copy_pid_ns(CLONE_NEWPID, &init_user_ns, &init_pid_ns);
	KUNIT_ASSERT_FALSE(test, IS_ERR(nested));

	parent_pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(parent_pid));
	occupied = pid_nr(parent_pid);

	requested[0] = child_tid;
	pid = alloc_pid(nested, requested, 1);
	KUNIT_ASSERT_TRUE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, -EINVAL, PTR_ERR(pid));

	nested->child_reaper = current;
	requested[0] = child_tid;
	requested[1] = occupied;
	pid = alloc_pid(nested, requested, 2);
	KUNIT_ASSERT_TRUE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, -EEXIST, PTR_ERR(pid));

	requested[0] = child_tid;
	pid = alloc_pid(nested, requested, 1);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid));
	KUNIT_EXPECT_EQ(test, child_tid, pid_nr_ns(pid, nested));

	nested->child_reaper = NULL;
	free_pid(pid);
	free_pid(parent_pid);
	rcu_barrier();
	put_pid_ns(nested);
}
#endif

static void pid_allocation_test_repeated(struct kunit *test)
{
	struct pid *pids[PID_ALLOCATION_REPEAT_COUNT] = {};
	int allocated = 0;
	int i;
	int j;

	for (i = 0; i < PID_ALLOCATION_REPEAT_COUNT; i++) {
		pids[i] = alloc_pid(&init_pid_ns, NULL, 0);
		if (IS_ERR(pids[i])) {
			KUNIT_FAIL(test, "allocation %d failed: %ld",
				   i, PTR_ERR(pids[i]));
			break;
		}
		allocated++;
	}

	for (i = 0; i < allocated; i++) {
		for (j = i + 1; j < allocated; j++)
			KUNIT_EXPECT_NE(test, pid_nr(pids[i]), pid_nr(pids[j]));
	}

	pid_allocation_release_all(pids, allocated);
}

static void pid_allocation_test_concurrent(struct kunit *test)
{
	struct pid_allocation_context *ctx;
	struct task_struct **tasks;
	struct completion go;
	bool failed = false;
	int i;
	int j;

	ctx = kunit_kzalloc(test, sizeof(*ctx) * PID_ALLOCATION_WORKERS,
			    GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ctx);
	tasks = kunit_kzalloc(test, sizeof(*tasks) * PID_ALLOCATION_WORKERS,
			      GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tasks);

	init_completion(&go);
	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		init_completion(&ctx[i].started);
		ctx[i].go = &go;
		tasks[i] = kthread_create(pid_allocation_thread, &ctx[i],
					  "pid-alloc-%d", i);
		if (IS_ERR_OR_NULL(tasks[i])) {
			KUNIT_FAIL(test, "thread %d creation failed", i);
			goto cleanup;
		}
		get_task_struct(tasks[i]);
	}

	for (i = 0; i < PID_ALLOCATION_WORKERS; i++)
		wake_up_process(tasks[i]);

	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		if (!wait_for_completion_timeout(&ctx[i].started, HZ)) {
			KUNIT_FAIL(test, "thread %d did not start", i);
			goto cleanup;
		}
	}

	complete_all(&go);
	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[i]));
		tasks[i] = NULL;
	}

	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		if (IS_ERR_OR_NULL(ctx[i].pid)) {
			KUNIT_FAIL(test, "worker %d allocation failed", i);
			failed = true;
		}
	}

	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		if (IS_ERR_OR_NULL(ctx[i].pid))
			continue;
		for (j = i + 1; j < PID_ALLOCATION_WORKERS; j++) {
			if (IS_ERR_OR_NULL(ctx[j].pid))
				continue;
			KUNIT_EXPECT_NE(test, pid_nr(ctx[i].pid), pid_nr(ctx[j].pid));
		}
	}

	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		if (!IS_ERR_OR_NULL(ctx[i].pid)) {
			free_pid(ctx[i].pid);
			ctx[i].pid = NULL;
		}
	}
	rcu_barrier();

	if (!failed)
		return;

cleanup:
	complete_all(&go);
	for (i = 0; i < PID_ALLOCATION_WORKERS; i++) {
		if (!IS_ERR_OR_NULL(tasks[i]))
			kthread_stop_put(tasks[i]);
		if (!IS_ERR_OR_NULL(ctx[i].pid)) {
			free_pid(ctx[i].pid);
			ctx[i].pid = NULL;
		}
	}
	rcu_barrier();
}

#ifdef CONFIG_RUST_PID_ALLOCATION
static void pid_allocation_test_pid_min(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1, rust_pid_alloc_test_pid_min(0));
	KUNIT_EXPECT_EQ(test, 1, rust_pid_alloc_test_pid_min(RESERVED_PIDS));
	KUNIT_EXPECT_EQ(test, RESERVED_PIDS,
			rust_pid_alloc_test_pid_min(RESERVED_PIDS + 1));
	KUNIT_EXPECT_EQ(test, RESERVED_PIDS,
			rust_pid_alloc_test_pid_min(UINT_MAX));
}

static void pid_allocation_test_error_mapping(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, -EEXIST,
			rust_pid_alloc_test_map_idr_error(-ENOSPC, 0));
	KUNIT_EXPECT_EQ(test, -EAGAIN,
			rust_pid_alloc_test_map_idr_error(-ENOSPC, 1));
	KUNIT_EXPECT_EQ(test, -ENOMEM,
			rust_pid_alloc_test_map_idr_error(-ENOMEM, 0));
}
#endif

static struct kunit_case pid_allocation_test_cases[] = {
#ifdef CONFIG_RUST_PID_ALLOCATION
	KUNIT_CASE(pid_allocation_test_pid_min),
	KUNIT_CASE(pid_allocation_test_error_mapping),
#endif
	KUNIT_CASE(pid_allocation_test_default),
	KUNIT_CASE(pid_allocation_test_invalid_set_tid),
	KUNIT_CASE(pid_allocation_test_set_tid_collision),
#ifdef CONFIG_PID_NS
	KUNIT_CASE(pid_allocation_test_namespace_rollback),
#endif
	KUNIT_CASE(pid_allocation_test_repeated),
	KUNIT_CASE(pid_allocation_test_concurrent),
	{}
};

static struct kunit_suite pid_allocation_test_suite = {
	.name = "pid_allocation",
	.test_cases = pid_allocation_test_cases,
};

kunit_test_suite(pid_allocation_test_suite);

MODULE_DESCRIPTION("KUnit tests for PID allocation");
MODULE_LICENSE("GPL");
