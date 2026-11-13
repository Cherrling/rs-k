// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>

static struct pid *pid_number_test_pid(struct kunit *test, unsigned int level)
{
	struct pid *pid;

	pid = kunit_kzalloc(test, struct_size(pid, numbers, level + 1),
			    GFP_KERNEL);
	if (!pid)
		return NULL;

	pid->level = level;
	return pid;
}

static struct pid_namespace *
pid_number_test_ns(struct kunit *test, unsigned int level)
{
	struct pid_namespace *ns;

	ns = kunit_kzalloc(test, sizeof(*ns), GFP_KERNEL);
	if (!ns)
		return NULL;

	ns->level = level;
	return ns;
}

static void pid_number_test_resolve_in_namespace(struct kunit *test)
{
	struct pid_namespace *init_ns;
	struct pid_namespace *child_ns;
	struct pid *pid;

	init_ns = pid_number_test_ns(test, 0);
	KUNIT_ASSERT_NOT_NULL(test, init_ns);
	child_ns = pid_number_test_ns(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, child_ns);
	pid = pid_number_test_pid(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, pid);
	pid->numbers[0].nr = 42;
	pid->numbers[0].ns = init_ns;
	pid->numbers[1].nr = 7;
	pid->numbers[1].ns = child_ns;

	KUNIT_EXPECT_EQ(test, 42, pid_nr_ns(pid, init_ns));
	KUNIT_EXPECT_EQ(test, 7, pid_nr_ns(pid, child_ns));
}

static void pid_number_test_reject_invalid_namespace(struct kunit *test)
{
	struct pid_namespace *init_ns;
	struct pid_namespace *child_ns;
	struct pid_namespace *other_child_ns;
	struct pid_namespace *deeper_ns;
	struct pid *pid;

	init_ns = pid_number_test_ns(test, 0);
	KUNIT_ASSERT_NOT_NULL(test, init_ns);
	child_ns = pid_number_test_ns(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, child_ns);
	other_child_ns = pid_number_test_ns(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, other_child_ns);
	deeper_ns = pid_number_test_ns(test, 2);
	KUNIT_ASSERT_NOT_NULL(test, deeper_ns);
	pid = pid_number_test_pid(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, pid);
	pid->numbers[0].nr = 42;
	pid->numbers[0].ns = init_ns;
	pid->numbers[1].nr = 7;
	pid->numbers[1].ns = child_ns;

	KUNIT_EXPECT_EQ(test, 0, pid_nr_ns(NULL, init_ns));
	KUNIT_EXPECT_EQ(test, 0, pid_nr_ns(pid, NULL));
	KUNIT_EXPECT_EQ(test, 0, pid_nr_ns(pid, deeper_ns));
	KUNIT_EXPECT_EQ(test, 0, pid_nr_ns(pid, other_child_ns));
}

static void pid_number_test_resolve_task_types(struct kunit *test)
{
	struct pid_namespace *child_ns;
	struct signal_struct *signal;
	struct task_struct *task;
	struct pid *pid;
	enum pid_type type;

	task = kunit_kzalloc(test, sizeof(*task), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, task);
	signal = kunit_kzalloc(test, sizeof(*signal), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, signal);
	child_ns = pid_number_test_ns(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, child_ns);
	pid = pid_number_test_pid(test, 1);
	KUNIT_ASSERT_NOT_NULL(test, pid);
	task->signal = signal;
	pid->numbers[0].nr = 42;
	pid->numbers[0].ns = &init_pid_ns;
	pid->numbers[1].nr = 7;
	pid->numbers[1].ns = child_ns;

	task->thread_pid = pid;
	for (type = PIDTYPE_TGID; type < PIDTYPE_MAX; type++)
		signal->pids[type] = pid;

	for (type = PIDTYPE_PID; type < PIDTYPE_MAX; type++)
		KUNIT_EXPECT_EQ(test, 7,
				__task_pid_nr_ns(task, type, child_ns));
}

static void pid_number_test_current_namespace_fallback(struct kunit *test)
{
	struct pid_namespace *init_ns;
	struct task_struct *task;
	struct pid *pid;

	task = kunit_kzalloc(test, sizeof(*task), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, task);
	init_ns = pid_number_test_ns(test, 0);
	KUNIT_ASSERT_NOT_NULL(test, init_ns);
	pid = pid_number_test_pid(test, 0);
	KUNIT_ASSERT_NOT_NULL(test, pid);
	pid->numbers[0].nr = 42;
	pid->numbers[0].ns = &init_pid_ns;
	task->thread_pid = pid;

	KUNIT_EXPECT_EQ(test, 42, __task_pid_nr_ns(task, PIDTYPE_PID, NULL));
	KUNIT_EXPECT_EQ(test, 42, pid_vnr(pid));
	KUNIT_EXPECT_EQ(test, 0, pid_nr_ns(pid, init_ns));
}

static struct kunit_case pid_number_test_cases[] = {
	KUNIT_CASE(pid_number_test_resolve_in_namespace),
	KUNIT_CASE(pid_number_test_reject_invalid_namespace),
	KUNIT_CASE(pid_number_test_resolve_task_types),
	KUNIT_CASE(pid_number_test_current_namespace_fallback),
	{}
};

static struct kunit_suite pid_number_test_suite = {
	.name = "pid_number",
	.test_cases = pid_number_test_cases,
};

kunit_test_suite(pid_number_test_suite);

MODULE_DESCRIPTION("KUnit tests for PID namespace number resolution");
MODULE_LICENSE("GPL");
