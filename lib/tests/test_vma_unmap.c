// SPDX-License-Identifier: GPL-2.0

#include <kunit/test.h>
#include <linux/mm.h>
#include <linux/vma-unmap-rust.h>

#define VMA_UNMAP_TEST_TASK_SIZE	0x10000000UL

struct vma_unmap_range_case {
	unsigned long start;
	size_t len;
	unsigned long task_size;
	int error;
	unsigned long end;
};

static void test_vma_unmap_range(struct kunit *test)
{
	static const struct vma_unmap_range_case cases[] = {
		{
			.start = 0x1000,
			.len = PAGE_SIZE,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.end = 0x2000,
		},
		{
			.start = 0x2000,
			.len = PAGE_SIZE + 1,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.end = 0x4000,
		},
		{
			.start = 0,
			.len = VMA_UNMAP_TEST_TASK_SIZE,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.end = VMA_UNMAP_TEST_TASK_SIZE,
		},
		{
			.start = 0x1001,
			.len = PAGE_SIZE,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.error = -EINVAL,
		},
		{
			.start = VMA_UNMAP_TEST_TASK_SIZE + PAGE_SIZE,
			.len = PAGE_SIZE,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.error = -EINVAL,
		},
		{
			.start = PAGE_SIZE,
			.len = VMA_UNMAP_TEST_TASK_SIZE,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.error = -EINVAL,
		},
		{
			.start = 0x1000,
			.len = 0,
			.task_size = VMA_UNMAP_TEST_TASK_SIZE,
			.error = -EINVAL,
		},
		{
			.start = 0,
			.len = ULONG_MAX,
			.task_size = ULONG_MAX,
			.error = -EINVAL,
		},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		unsigned long end = 0;
		int error;

		error = rust_vma_unmap_test_range(cases[i].start, cases[i].len,
						  cases[i].task_size, &end);
		KUNIT_EXPECT_EQ(test, cases[i].error, error);
		if (!error)
			KUNIT_EXPECT_EQ(test, cases[i].end, end);
	}
}

static struct kunit_case vma_unmap_test_cases[] = {
	KUNIT_CASE(test_vma_unmap_range),
	{}
};

static struct kunit_suite vma_unmap_test_suite = {
	.name = "vma_unmap",
	.test_cases = vma_unmap_test_cases,
};

kunit_test_suite(vma_unmap_test_suite);

MODULE_DESCRIPTION("KUnit tests for Rust address-space range unmapping");
MODULE_LICENSE("GPL");
