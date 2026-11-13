// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/mm.h>
#include <linux/mm-rust.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>

#ifdef CONFIG_RUST_MM_LIFETIME
static void mm_lifetime_test_get_task_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_MM_GET_ACTION_NONE,
			rust_mm_get_task_plan_select(true, true));
	KUNIT_EXPECT_EQ(test, RUST_MM_GET_ACTION_NONE,
			rust_mm_get_task_plan_select(true, false));
	KUNIT_EXPECT_EQ(test, RUST_MM_GET_ACTION_NONE,
			rust_mm_get_task_plan_select(false, false));
	KUNIT_EXPECT_EQ(test, RUST_MM_GET_ACTION_ACQUIRE,
			rust_mm_get_task_plan_select(false, true));
}

static void mm_lifetime_test_put_plan(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, RUST_MM_PUT_ACTION_RETURN,
			rust_mm_put_plan_select(false, false));
	KUNIT_EXPECT_EQ(test, RUST_MM_PUT_ACTION_RETURN,
			rust_mm_put_plan_select(false, true));
	KUNIT_EXPECT_EQ(test, RUST_MM_PUT_ACTION_FINALIZE,
			rust_mm_put_plan_select(true, false));
	KUNIT_EXPECT_EQ(test, RUST_MM_PUT_ACTION_ASYNC,
			rust_mm_put_plan_select(true, true));
}
#endif

static void mm_lifetime_test_get_task_synthetic(struct kunit *test)
{
	struct mm_struct *mm;
	struct mm_struct *acquired;
	struct task_struct *task;

	task = kzalloc_obj(*task);
	KUNIT_ASSERT_NOT_NULL(test, task);

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);

	spin_lock_init(&task->alloc_lock);
	task->mm = mm;
	task->flags = PF_KTHREAD;

	KUNIT_EXPECT_PTR_EQ(test, NULL, get_task_mm(task));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));

	task->flags = 0;
	acquired = get_task_mm(task);
	KUNIT_EXPECT_PTR_EQ(test, mm, acquired);
	KUNIT_EXPECT_EQ(test, 2, atomic_read(&mm->mm_users));

	mmput(acquired);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));

	task->mm = NULL;
	mmput(mm);
	kfree(task);
}

static void mm_lifetime_test_put_sync(struct kunit *test)
{
	struct mm_struct *mm;

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);

	mmget(mm);
	KUNIT_EXPECT_EQ(test, 2, atomic_read(&mm->mm_users));
	mmput(mm);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));
	mmput(mm);
}

static void mm_lifetime_test_put_async(struct kunit *test)
{
	struct mm_struct *mm;

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);

	mmget(mm);
	mmput_async(mm);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));
	flush_workqueue(system_wq);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));
	mmput(mm);

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);
	mmput_async(mm);
	flush_workqueue(system_wq);
}

static struct kunit_case mm_lifetime_test_cases[] = {
#ifdef CONFIG_RUST_MM_LIFETIME
	KUNIT_CASE(mm_lifetime_test_get_task_plan),
	KUNIT_CASE(mm_lifetime_test_put_plan),
#endif
	KUNIT_CASE(mm_lifetime_test_get_task_synthetic),
	KUNIT_CASE(mm_lifetime_test_put_sync),
	KUNIT_CASE(mm_lifetime_test_put_async),
	{}
};

static struct kunit_suite mm_lifetime_test_suite = {
	.name = "mm_lifetime",
	.test_cases = mm_lifetime_test_cases,
};

kunit_test_suite(mm_lifetime_test_suite);

MODULE_DESCRIPTION("KUnit tests for address-space reference lifetime");
MODULE_LICENSE("GPL");
