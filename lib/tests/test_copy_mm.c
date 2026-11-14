// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/copy-mm-rust.h>
#include <linux/mm.h>
#include <linux/sched.h>

static void copy_mm_test_alloc(struct kunit *test)
{
	struct mm_struct *mm = mm_alloc();

	KUNIT_ASSERT_NOT_NULL(test, mm);
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_count));
	mmput(mm);
}

#ifdef CONFIG_RUST_COPY_MM
static void copy_mm_test_init_failures(struct kunit *test)
{
	static const u32 stages[] = {
		RUST_COPY_MM_TEST_FAIL_PGD,
		RUST_COPY_MM_TEST_FAIL_ID,
		RUST_COPY_MM_TEST_FAIL_CONTEXT,
		RUST_COPY_MM_TEST_FAIL_CID,
		RUST_COPY_MM_TEST_FAIL_SCHED,
		RUST_COPY_MM_TEST_FAIL_RSS,
	};
	u32 failure_count = rust_copy_mm_test_init_failure_count();
	struct mm_struct *mm;
	int i;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {
		rust_copy_mm_test_fail_mm_init(stages[i]);
		mm = mm_alloc();
		if (mm)
			mmput(mm);
		KUNIT_EXPECT_NULL(test, mm);
		KUNIT_EXPECT_EQ(test, failure_count + 1,
				rust_copy_mm_test_init_failure_count());
		failure_count++;
	}

	rust_copy_mm_test_fail_mm_init(0);
}

static void copy_mm_test_duplicate(struct kunit *test)
{
	struct mm_struct *oldmm = mm_alloc();
	struct mm_struct *mm;

	KUNIT_ASSERT_NOT_NULL(test, oldmm);
	mm = rust_copy_mm_test_dup(current, oldmm);
	KUNIT_ASSERT_NOT_NULL(test, mm);

	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_users));
	KUNIT_EXPECT_EQ(test, 1, atomic_read(&mm->mm_count));
	KUNIT_EXPECT_EQ(test, 0UL, get_mm_rss(mm));
	KUNIT_EXPECT_PTR_NE(test, oldmm, mm);

	mmput(mm);
	mmput(oldmm);
}

static void copy_mm_test_duplicate_failures(struct kunit *test)
{
	static const u32 stages[] = {
		RUST_COPY_MM_TEST_FAIL_DUP_MMAP,
		RUST_COPY_MM_TEST_FAIL_DUP_BINFMT,
	};
	u32 failure_count = rust_copy_mm_test_dup_failure_count();
	struct mm_struct *oldmm;
	struct mm_struct *mm;
	int i;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {
		oldmm = mm_alloc();
		KUNIT_ASSERT_NOT_NULL(test, oldmm);
		rust_copy_mm_test_fail_dup(stages[i]);
		mm = rust_copy_mm_test_dup(current, oldmm);
		if (mm)
			mmput(mm);
		mmput(oldmm);
		KUNIT_EXPECT_NULL(test, mm);
		KUNIT_EXPECT_EQ(test, failure_count + 1,
				rust_copy_mm_test_dup_failure_count());
		failure_count++;
	}

	rust_copy_mm_test_fail_dup(0);
}

static void copy_mm_test_kernel_thread(struct kunit *test)
{
	struct task_struct *task;

	task = kunit_kzalloc(test, sizeof(*task), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, task);
	task->min_flt = 1;
	task->maj_flt = 1;
	task->nvcsw = 1;
	task->nivcsw = 1;
#ifdef CONFIG_DETECT_HUNG_TASK
	task->last_switch_count = 1;
	task->last_switch_time = 1;
#endif

	KUNIT_EXPECT_EQ(test, 0, rust_copy_mm_test_kernel_thread(task));
	KUNIT_EXPECT_EQ(test, 0UL, task->min_flt);
	KUNIT_EXPECT_EQ(test, 0UL, task->maj_flt);
	KUNIT_EXPECT_EQ(test, 0UL, task->nvcsw);
	KUNIT_EXPECT_EQ(test, 0UL, task->nivcsw);
#ifdef CONFIG_DETECT_HUNG_TASK
	KUNIT_EXPECT_EQ(test, 0UL, task->last_switch_count);
	KUNIT_EXPECT_EQ(test, 0UL, task->last_switch_time);
#endif
	KUNIT_EXPECT_PTR_EQ(test, NULL, task->mm);
	KUNIT_EXPECT_PTR_EQ(test, NULL, task->active_mm);
}

#endif

static struct kunit_case copy_mm_test_cases[] = {
	KUNIT_CASE(copy_mm_test_alloc),
#ifdef CONFIG_RUST_COPY_MM
	KUNIT_CASE(copy_mm_test_init_failures),
	KUNIT_CASE(copy_mm_test_duplicate),
	KUNIT_CASE(copy_mm_test_duplicate_failures),
	KUNIT_CASE(copy_mm_test_kernel_thread),
#endif
	{}
};

static struct kunit_suite copy_mm_test_suite = {
	.name = "copy_mm",
	.test_cases = copy_mm_test_cases,
};

kunit_test_suite(copy_mm_test_suite);

MODULE_DESCRIPTION("KUnit tests for address-space construction and copying");
MODULE_LICENSE("GPL");
