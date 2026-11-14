// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/exit-mmap-rust.h>
#include <linux/mm.h>

#define EXIT_ACCOUNT_START	0x10000000UL
#define EXIT_REGULAR_START	0x20000000UL
#define EXIT_ACCOUNT_TWO_START	0x30000000UL
#define EXIT_REGULAR_TWO_START	0x40000000UL
#define EXIT_TEST_PAGES		2
#define EXIT_TEST_SIZE		(EXIT_TEST_PAGES * PAGE_SIZE)

static int add_test_vma(struct mm_struct *mm, unsigned long start,
			vm_flags_t flags)
{
	return rust_helper_exit_mmap_test_add_anon(mm, start,
						  start + EXIT_TEST_SIZE,
						  flags);
}

static int add_test_vmas(struct mm_struct *mm)
{
	int error;

	mmap_write_lock(mm);
	error = add_test_vma(mm, EXIT_ACCOUNT_START, VM_ACCOUNT);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, EXIT_REGULAR_START, 0);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, EXIT_ACCOUNT_TWO_START, VM_ACCOUNT);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, EXIT_REGULAR_TWO_START, 0);

out_unlock:
	mmap_write_unlock(mm);
	return error;
}

static void test_exit_mmap_empty(struct kunit *test)
{
	struct mm_struct *mm;

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);

	rust_exit_mmap_test_watch(mm);
	mmput(mm);

	KUNIT_EXPECT_EQ(test, 0, rust_exit_mmap_test_vma_runs());
	KUNIT_EXPECT_EQ(test, 1, rust_exit_mmap_test_empty_runs());
	KUNIT_EXPECT_EQ(test, 0, rust_exit_mmap_test_vma_count());
	KUNIT_EXPECT_EQ(test, 0, rust_exit_mmap_test_accounted_pages());
}

static void test_exit_mmap_populated(struct kunit *test)
{
	struct mm_struct *mm;

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);
	KUNIT_ASSERT_EQ(test, 0, add_test_vmas(mm));
	KUNIT_EXPECT_EQ(test, 4, mm->map_count);

	rust_exit_mmap_test_watch(mm);
	mmput(mm);

	KUNIT_EXPECT_EQ(test, 1, rust_exit_mmap_test_vma_runs());
	KUNIT_EXPECT_EQ(test, 0, rust_exit_mmap_test_empty_runs());
	KUNIT_EXPECT_EQ(test, 4, rust_exit_mmap_test_vma_count());
	KUNIT_EXPECT_EQ(test, 2 * EXIT_TEST_PAGES,
			rust_exit_mmap_test_accounted_pages());
}

static struct kunit_case exit_mmap_test_cases[] = {
	KUNIT_CASE(test_exit_mmap_empty),
	KUNIT_CASE(test_exit_mmap_populated),
	{}
};

static struct kunit_suite exit_mmap_test_suite = {
	.name = "exit_mmap",
	.test_cases = exit_mmap_test_cases,
};

kunit_test_suite(exit_mmap_test_suite);

MODULE_DESCRIPTION("KUnit tests for Rust address-space mapping teardown");
MODULE_LICENSE("GPL");
