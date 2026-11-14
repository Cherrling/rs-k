// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/dup-mmap-rust.h>
#include <linux/mm.h>

#define DUP_ACCOUNT_START	0x10000000UL
#define DUP_DONTCOPY_START	0x20000000UL
#define DUP_WIPE_START		0x30000000UL
#define DUP_REGULAR_START	0x40000000UL
#define DUP_TEST_SIZE		PAGE_SIZE

static int add_test_vma(struct mm_struct *mm, unsigned long start,
			vm_flags_t flags)
{
	return rust_helper_dup_mmap_test_add_anon(mm, start,
						  start + DUP_TEST_SIZE,
						  flags);
}

static int add_test_vmas(struct mm_struct *mm)
{
	int error;

	mmap_write_lock(mm);
	error = add_test_vma(mm, DUP_ACCOUNT_START, VM_ACCOUNT);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, DUP_DONTCOPY_START, VM_DONTCOPY);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, DUP_WIPE_START, VM_WIPEONFORK);
	if (error)
		goto out_unlock;
	error = add_test_vma(mm, DUP_REGULAR_START, 0);

out_unlock:
	mmap_write_unlock(mm);
	return error;
}

static void test_dup_mmap_success(struct kunit *test)
{
	struct vm_area_struct *vma;
	struct mm_struct *oldmm;
	struct mm_struct *mm;
	struct vma_iterator vmi;
	unsigned long starts[3] = {
		DUP_ACCOUNT_START,
		DUP_WIPE_START,
		DUP_REGULAR_START,
	};
	int count = 0;

	oldmm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, oldmm);
	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);

	KUNIT_ASSERT_EQ(test, 0, add_test_vmas(oldmm));
	KUNIT_EXPECT_EQ(test, 4UL, oldmm->map_count);
	KUNIT_EXPECT_EQ(test, 4UL, oldmm->total_vm);
	KUNIT_EXPECT_EQ(test, 4UL, oldmm->data_vm);
	KUNIT_EXPECT_EQ(test, 0, rust_dup_mmap(mm, oldmm));
	KUNIT_EXPECT_EQ(test, 3UL, mm->map_count);
	KUNIT_EXPECT_EQ(test, 3UL, mm->total_vm);
	KUNIT_EXPECT_EQ(test, 3UL, mm->data_vm);

	mmap_read_lock(mm);
	vma_iter_init(&vmi, mm, 0);
	while ((vma = vma_next(&vmi))) {
		if (count >= ARRAY_SIZE(starts)) {
			KUNIT_FAIL(test, "too many destination VMAs");
			break;
		}
		KUNIT_EXPECT_EQ(test, starts[count], vma->vm_start);
		count++;
	}
	mmap_read_unlock(mm);
	KUNIT_EXPECT_EQ(test, 3, count);

	mmput(mm);
	mmput(oldmm);
}

static void test_dup_mmap_failures(struct kunit *test)
{
	static const struct {
		u32 stage;
		unsigned long map_count;
		bool unstable;
	} stages[] = {
		{ RUST_DUP_MMAP_TEST_FAIL_TREE, 0, false },
		{ RUST_DUP_MMAP_TEST_FAIL_SOURCE_WRITE, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_DONTCOPY, 1, true },
		{ RUST_DUP_MMAP_TEST_FAIL_ACCOUNT, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_VMA, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_POLICY, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_USERFAULTFD, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_ANON_VMA, 0, true },
		{ RUST_DUP_MMAP_TEST_FAIL_PAGE_RANGE, 1, true },
		{ RUST_DUP_MMAP_TEST_FAIL_ARCH, 3, true },
	};
	u32 failure_count = rust_dup_mmap_test_failure_count();
	int i;

	for (i = 0; i < ARRAY_SIZE(stages); i++) {
		struct mm_struct *oldmm;
		struct mm_struct *mm;
		struct vma_iterator vmi;
		int error;

		oldmm = mm_alloc();
		KUNIT_ASSERT_NOT_NULL(test, oldmm);
		mm = mm_alloc();
		KUNIT_ASSERT_NOT_NULL(test, mm);
		KUNIT_ASSERT_EQ(test, 0, add_test_vmas(oldmm));

		rust_dup_mmap_test_fail(oldmm, stages[i].stage);
		error = rust_dup_mmap(mm, oldmm);
		KUNIT_EXPECT_NE(test, 0, error);
		KUNIT_EXPECT_EQ(test, stages[i].map_count, mm->map_count);
		KUNIT_EXPECT_EQ(test, stages[i].unstable,
				mm_flags_test(MMF_UNSTABLE, mm));
		KUNIT_EXPECT_EQ(test, stages[i].unstable,
				mm_flags_test(MMF_OOM_SKIP, mm));
		mmap_read_lock(mm);
		vma_iter_init(&vmi, mm, 0);
		KUNIT_EXPECT_NULL(test, vma_next(&vmi));
		mmap_read_unlock(mm);
		KUNIT_EXPECT_EQ(test, ++failure_count,
				rust_dup_mmap_test_failure_count());

		mmput(mm);
		mmput(oldmm);
	}

	rust_dup_mmap_test_fail(NULL, 0);
}

static struct kunit_case dup_mmap_test_cases[] = {
	KUNIT_CASE(test_dup_mmap_success),
	KUNIT_CASE(test_dup_mmap_failures),
	{}
};

static struct kunit_suite dup_mmap_test_suite = {
	.name = "dup_mmap",
	.test_cases = dup_mmap_test_cases,
};

kunit_test_suite(dup_mmap_test_suite);

MODULE_DESCRIPTION("KUnit tests for Rust VMA-tree duplication");
MODULE_LICENSE("GPL");
