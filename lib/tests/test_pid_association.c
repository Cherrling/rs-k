// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/err.h>
#include <linux/pid.h>
#include <linux/pid_namespace.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/spinlock.h>

static struct task_struct *pid_association_test_task(struct kunit *test)
{
	struct task_struct *task;
	struct signal_struct *signal;

	task = kunit_kzalloc(test, sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	signal = kunit_kzalloc(test, sizeof(*signal), GFP_KERNEL);
	if (!signal)
		return NULL;

	task->signal = signal;
	return task;
}

static void pid_association_test_attach_detach(struct kunit *test)
{
	struct task_struct *task;
	struct pid *pids[PIDTYPE_MAX] = {};
	struct pid *pid;
	unsigned long flags;

	task = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, task);
	pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid));
	task->signal->pids[PIDTYPE_PGID] = pid;

	write_lock_irqsave(&tasklist_lock, flags);
	attach_pid(task, PIDTYPE_PGID);
	KUNIT_EXPECT_PTR_EQ(test, task, pid_task(pid, PIDTYPE_PGID));
	KUNIT_EXPECT_TRUE(test, pid_has_task(pid, PIDTYPE_PGID));
	detach_pid(pids, task, PIDTYPE_PGID);
	KUNIT_EXPECT_FALSE(test, pid_has_task(pid, PIDTYPE_PGID));
	KUNIT_EXPECT_NULL(test, task->signal->pids[PIDTYPE_PGID]);
	KUNIT_EXPECT_PTR_EQ(test, pid, pids[PIDTYPE_PGID]);
	write_unlock_irqrestore(&tasklist_lock, flags);

	pids[PIDTYPE_PGID] = NULL;
	free_pid(pid);
	rcu_barrier();
}

static void pid_association_test_change(struct kunit *test)
{
	struct task_struct *task;
	struct pid *pids[PIDTYPE_MAX] = {};
	struct pid *pid1;
	struct pid *pid2;
	unsigned long flags;

	task = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, task);
	pid1 = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid1));
	pid2 = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid2));
	task->signal->pids[PIDTYPE_PGID] = pid1;

	write_lock_irqsave(&tasklist_lock, flags);
	attach_pid(task, PIDTYPE_PGID);
	change_pid(pids, task, PIDTYPE_PGID, pid2);
	KUNIT_EXPECT_PTR_EQ(test, pid2, task->signal->pids[PIDTYPE_PGID]);
	KUNIT_EXPECT_FALSE(test, pid_has_task(pid1, PIDTYPE_PGID));
	KUNIT_EXPECT_TRUE(test, pid_has_task(pid2, PIDTYPE_PGID));
	KUNIT_EXPECT_PTR_EQ(test, pid1, pids[PIDTYPE_PGID]);

	pids[PIDTYPE_PGID] = NULL;
	detach_pid(pids, task, PIDTYPE_PGID);
	KUNIT_EXPECT_PTR_EQ(test, pid2, pids[PIDTYPE_PGID]);
	write_unlock_irqrestore(&tasklist_lock, flags);

	pids[PIDTYPE_PGID] = NULL;
	free_pid(pid1);
	free_pid(pid2);
	rcu_barrier();
}

static void pid_association_test_orphan_scan(struct kunit *test)
{
	struct task_struct *task1;
	struct task_struct *task2;
	struct pid *pgid_pids[PIDTYPE_MAX] = {};
	struct pid *sid_pids[PIDTYPE_MAX] = {};
	struct pid *pid;
	unsigned long flags;

	task1 = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, task1);
	task2 = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, task2);
	pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid));

	task1->signal->pids[PIDTYPE_PGID] = pid;
	task2->signal->pids[PIDTYPE_SID] = pid;

	write_lock_irqsave(&tasklist_lock, flags);
	attach_pid(task1, PIDTYPE_PGID);
	attach_pid(task2, PIDTYPE_SID);
	detach_pid(pgid_pids, task1, PIDTYPE_PGID);
	KUNIT_EXPECT_NULL(test, pgid_pids[PIDTYPE_PGID]);
	KUNIT_EXPECT_TRUE(test, pid_has_task(pid, PIDTYPE_SID));
	detach_pid(sid_pids, task2, PIDTYPE_SID);
	KUNIT_EXPECT_PTR_EQ(test, pid, sid_pids[PIDTYPE_SID]);
	write_unlock_irqrestore(&tasklist_lock, flags);

	sid_pids[PIDTYPE_SID] = NULL;
	free_pid(pid);
	rcu_barrier();
}

static void pid_association_test_exchange(struct kunit *test)
{
	struct task_struct *left;
	struct task_struct *right;
	struct pid *left_pids[PIDTYPE_MAX] = {};
	struct pid *right_pids[PIDTYPE_MAX] = {};
	struct pid *left_pid;
	struct pid *right_pid;
	unsigned long flags;
	pid_t left_nr;
	pid_t right_nr;

	left = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, left);
	right = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, right);
	left_pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(left_pid));
	right_pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(right_pid));

	left_nr = pid_nr(left_pid);
	right_nr = pid_nr(right_pid);
	left->thread_pid = left_pid;
	right->thread_pid = right_pid;
	left->pid = left_nr;
	right->pid = right_nr;

	write_lock_irqsave(&tasklist_lock, flags);
	attach_pid(left, PIDTYPE_PID);
	attach_pid(right, PIDTYPE_PID);
	exchange_tids(left, right);
	KUNIT_EXPECT_PTR_EQ(test, right_pid, left->thread_pid);
	KUNIT_EXPECT_PTR_EQ(test, left_pid, right->thread_pid);
	KUNIT_EXPECT_PTR_EQ(test, left, pid_task(right_pid, PIDTYPE_PID));
	KUNIT_EXPECT_PTR_EQ(test, right, pid_task(left_pid, PIDTYPE_PID));
	KUNIT_EXPECT_EQ(test, right_nr, left->pid);
	KUNIT_EXPECT_EQ(test, left_nr, right->pid);

	detach_pid(left_pids, left, PIDTYPE_PID);
	detach_pid(right_pids, right, PIDTYPE_PID);
	KUNIT_EXPECT_PTR_EQ(test, right_pid, left_pids[PIDTYPE_PID]);
	KUNIT_EXPECT_PTR_EQ(test, left_pid, right_pids[PIDTYPE_PID]);
	write_unlock_irqrestore(&tasklist_lock, flags);

	left_pids[PIDTYPE_PID] = NULL;
	right_pids[PIDTYPE_PID] = NULL;
	free_pid(left_pid);
	free_pid(right_pid);
	rcu_barrier();
}

static void pid_association_test_transfer(struct kunit *test)
{
	struct task_struct *old;
	struct task_struct *new;
	struct pid *pids[PIDTYPE_MAX] = {};
	struct pid *pid;
	unsigned long flags;

	old = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, old);
	new = pid_association_test_task(test);
	KUNIT_ASSERT_NOT_NULL(test, new);
	pid = alloc_pid(&init_pid_ns, NULL, 0);
	KUNIT_ASSERT_FALSE(test, IS_ERR(pid));

	old->signal->pids[PIDTYPE_PGID] = pid;
	new->signal->pids[PIDTYPE_PGID] = pid;

	write_lock_irqsave(&tasklist_lock, flags);
	attach_pid(old, PIDTYPE_PGID);
	transfer_pid(old, new, PIDTYPE_PGID);
	KUNIT_EXPECT_PTR_EQ(test, new, pid_task(pid, PIDTYPE_PGID));
	old->signal->pids[PIDTYPE_PGID] = NULL;
	detach_pid(pids, new, PIDTYPE_PGID);
	KUNIT_EXPECT_PTR_EQ(test, pid, pids[PIDTYPE_PGID]);
	write_unlock_irqrestore(&tasklist_lock, flags);

	pids[PIDTYPE_PGID] = NULL;
	free_pid(pid);
	rcu_barrier();
}

static struct kunit_case pid_association_test_cases[] = {
	KUNIT_CASE(pid_association_test_attach_detach),
	KUNIT_CASE(pid_association_test_change),
	KUNIT_CASE(pid_association_test_orphan_scan),
	KUNIT_CASE(pid_association_test_exchange),
	KUNIT_CASE(pid_association_test_transfer),
	{}
};

static struct kunit_suite pid_association_test_suite = {
	.name = "pid_association",
	.test_cases = pid_association_test_cases,
};

kunit_test_suite(pid_association_test_suite);

MODULE_DESCRIPTION("KUnit tests for PID task association");
MODULE_LICENSE("GPL");
