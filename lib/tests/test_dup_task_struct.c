// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/dup-task-struct-rust.h>
#include <linux/kthread.h>
#include <linux/numa.h>
#include <linux/rcupdate.h>
#include <linux/refcount.h>
#include <linux/sched.h>
#include <linux/sched/task.h>
#include <linux/sched/task_stack.h>

#define DUP_TASK_STRUCT_TEST_TIMEOUT (5 * HZ)

struct dup_task_struct_context {
	struct completion started;
	int invalid_reason;
};

enum dup_task_struct_invalid_reason {
	DUP_TASK_STRUCT_VALID,
	DUP_TASK_STRUCT_NO_STACK,
	DUP_TASK_STRUCT_BAD_STACK_MAGIC,
	DUP_TASK_STRUCT_ZERO_USAGE,
	DUP_TASK_STRUCT_BAD_RCU_USERS,
	DUP_TASK_STRUCT_ZERO_STACK_REFCOUNT,
	DUP_TASK_STRUCT_STALE_SPLICE_PIPE,
	DUP_TASK_STRUCT_STALE_TASK_FRAG,
};

static int dup_task_struct_child_state_valid(void)
{
	struct task_struct *task = current;

	if (!task->stack)
		return DUP_TASK_STRUCT_NO_STACK;
	if (*end_of_stack(task) != STACK_END_MAGIC)
		return DUP_TASK_STRUCT_BAD_STACK_MAGIC;
	if (!refcount_read(&task->usage))
		return DUP_TASK_STRUCT_ZERO_USAGE;
	if (refcount_read(&task->rcu_users) != 2)
		return DUP_TASK_STRUCT_BAD_RCU_USERS;
#ifdef CONFIG_THREAD_INFO_IN_TASK
	if (!refcount_read(&task->stack_refcount))
		return DUP_TASK_STRUCT_ZERO_STACK_REFCOUNT;
#endif
	if (task->splice_pipe)
		return DUP_TASK_STRUCT_STALE_SPLICE_PIPE;
	if (task->task_frag.page)
		return DUP_TASK_STRUCT_STALE_TASK_FRAG;
	return DUP_TASK_STRUCT_VALID;
}

static int dup_task_struct_child(void *data)
{
	struct dup_task_struct_context *context = data;

	context->invalid_reason = dup_task_struct_child_state_valid();
	complete(&context->started);

	while (!kthread_should_stop())
		schedule();

	return 0;
}

#ifdef CONFIG_RUST_DUP_TASK_STRUCT
static void dup_task_struct_test_cleanup(struct kunit *test)
{
	static const u32 stages[] = {
		RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_TASK,
		RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_STACK,
		RUST_DUP_TASK_STRUCT_TEST_FAIL_AFTER_IPI_MASK,
	};
	u32 failure_count = rust_dup_task_struct_test_failure_count();
	int i;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {
		rust_dup_task_struct_test_fail_next(stages[i]);
		KUNIT_EXPECT_TRUE(test,
				  !rust_dup_task_struct(current, NUMA_NO_NODE));
		KUNIT_EXPECT_EQ(test, failure_count + 1,
				rust_dup_task_struct_test_failure_count());
		failure_count++;
	}

	rust_dup_task_struct_test_fail_next(0);
}
#endif

static void dup_task_struct_test_kthread(struct kunit *test)
{
	struct dup_task_struct_context context;
	struct task_struct *task;
#ifdef CONFIG_RUST_DUP_TASK_STRUCT
	u32 complete_count;
	u32 failure_count;
	u32 stack_alloc_count;
	u32 task_alloc_count;
#endif

	memset(&context, 0, sizeof(context));
	init_completion(&context.started);

#ifdef CONFIG_RUST_DUP_TASK_STRUCT
	task_alloc_count = rust_dup_task_struct_test_task_alloc_count();
	stack_alloc_count = rust_dup_task_struct_test_stack_alloc_count();
	complete_count = rust_dup_task_struct_test_complete_count();
	failure_count = rust_dup_task_struct_test_failure_count();
#endif

	task = kthread_run(dup_task_struct_child, &context,
			   "dup-task-struct-test");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, task);
	KUNIT_ASSERT_NE(test, 0,
			wait_for_completion_timeout(&context.started,
						    DUP_TASK_STRUCT_TEST_TIMEOUT));
	KUNIT_EXPECT_EQ(test, DUP_TASK_STRUCT_VALID, context.invalid_reason);

	get_task_struct(task);
	KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(task));
	rcu_barrier();

#ifdef CONFIG_RUST_DUP_TASK_STRUCT
	KUNIT_EXPECT_GT(test, rust_dup_task_struct_test_task_alloc_count(),
			task_alloc_count);
	KUNIT_EXPECT_GT(test, rust_dup_task_struct_test_stack_alloc_count(),
			stack_alloc_count);
	KUNIT_EXPECT_GT(test, rust_dup_task_struct_test_complete_count(),
			complete_count);
	KUNIT_EXPECT_EQ(test, rust_dup_task_struct_test_failure_count(),
			failure_count);
#endif
}

static struct kunit_case dup_task_struct_test_cases[] = {
#ifdef CONFIG_RUST_DUP_TASK_STRUCT
	KUNIT_CASE(dup_task_struct_test_cleanup),
#endif
	KUNIT_CASE(dup_task_struct_test_kthread),
	{}
};

static struct kunit_suite dup_task_struct_test_suite = {
	.name = "dup_task_struct",
	.test_cases = dup_task_struct_test_cases,
};

kunit_test_suite(dup_task_struct_test_suite);

MODULE_DESCRIPTION("KUnit tests for task structure duplication");
MODULE_LICENSE("GPL");
