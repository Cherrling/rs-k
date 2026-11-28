// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/file.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/page-alloc-free-rust.h>
#include <linux/page_ref.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>
#include <linux/topology.h>

#define PAGE_ALLOC_FREE_TEST_TIMEOUT		(5 * HZ)
#define PAGE_ALLOC_FREE_TEST_WORKERS		4
#define PAGE_ALLOC_FREE_TEST_ITERATIONS		96

struct page_alloc_free_test_worker {
	struct completion done;
	atomic_t errors;
};

static void page_alloc_free_test_order_zero(struct kunit *test)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	__free_pages(page, 0);
}

static void page_alloc_free_test_noncompound_high_order(struct kunit *test)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 1);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_FALSE(test, PageHead(page));
	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	__free_pages(page, 1);
}

static void page_alloc_free_test_compound_folio(struct kunit *test)
{
	struct folio *folio;

	folio = __folio_alloc(GFP_KERNEL, 1, numa_mem_id(), NULL);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));
	KUNIT_EXPECT_EQ(test, 2, folio_nr_pages(folio));
#ifdef CONFIG_TRANSPARENT_HUGEPAGE
	KUNIT_EXPECT_TRUE(test, folio_test_large_rmappable(folio));
#else
	KUNIT_EXPECT_FALSE(test, folio_test_large_rmappable(folio));
#endif
	folio_put(folio);
}

static void page_alloc_free_test_node_fallback(struct kunit *test)
{
	struct page *page;

	page = alloc_pages_node(NUMA_NO_NODE, GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	__free_pages(page, 0);
}

static void page_alloc_free_test_explicit_node(struct kunit *test)
{
	struct page *page;

	page = alloc_pages_node(numa_mem_id(), GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, numa_mem_id(), page_to_nid(page));
	__free_pages(page, 0);
}

static void page_alloc_free_test_nolock(struct kunit *test)
{
	struct page *page;

	page = alloc_pages_nolock(0, NUMA_NO_NODE, 0);
	if (!page)
		kunit_skip(test, "no-lock allocator refused the opportunistic request");

	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	free_pages_nolock(page, 0);
}

static void page_alloc_free_test_nonfinal_reference(struct kunit *test)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	page_ref_inc(page);
	KUNIT_ASSERT_EQ(test, 2, page_ref_count(page));

	__free_pages(page, 0);
	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	__free_pages(page, 0);
}

static void page_alloc_free_test_noncompound_tail_release(struct kunit *test)
{
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 1);
	KUNIT_ASSERT_NOT_NULL(test, page);
	page_ref_inc(page);
	KUNIT_ASSERT_EQ(test, 2, page_ref_count(page));

	__free_pages(page, 1);
	KUNIT_EXPECT_EQ(test, 1, page_ref_count(page));
	__free_pages(page, 0);
}

static void page_alloc_free_test_address_free(struct kunit *test)
{
	unsigned long addr;

	addr = __get_free_pages(GFP_KERNEL, 1);
	KUNIT_ASSERT_NE(test, 0, addr);
	free_pages(addr, 1);
}

static void page_alloc_free_test_zero_address(struct kunit *test)
{
	free_pages(0, 0);
	KUNIT_SUCCEED(test);
}

#ifdef CONFIG_RUST_PAGE_ALLOC_FREE
static void page_alloc_free_test_failure_alloc(struct kunit *test)
{
	struct page *page;
	unsigned int count;

	rust_page_alloc_free_test_fail_next(RUST_PAGE_ALLOC_FREE_TEST_FAIL_ALLOC);
	count = rust_page_alloc_free_test_failure_count();
	page = __alloc_pages_noprof(GFP_KERNEL, 0, numa_mem_id(), NULL, 0);
	KUNIT_EXPECT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, count + 1,
			rust_page_alloc_free_test_failure_count());

	page = __alloc_pages_noprof(GFP_KERNEL, 0, numa_mem_id(), NULL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	__free_pages(page, 0);
}

static void page_alloc_free_test_failure_refcount(struct kunit *test)
{
	struct page *page;
	unsigned int count;

	rust_page_alloc_free_test_fail_next(RUST_PAGE_ALLOC_FREE_TEST_FAIL_REFCOUNT);
	count = rust_page_alloc_free_test_failure_count();
	page = __alloc_pages_noprof(GFP_KERNEL, 1, numa_mem_id(), NULL, 0);
	KUNIT_EXPECT_NULL(test, page);
	KUNIT_EXPECT_EQ(test, count + 1,
			rust_page_alloc_free_test_failure_count());

	page = __alloc_pages_noprof(GFP_KERNEL, 1, numa_mem_id(), NULL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	__free_pages(page, 1);
}
#endif

static void page_alloc_free_test_page_cache_roundtrip(struct kunit *test)
{
	struct address_space *mapping;
	struct folio *folio;
	struct file *file;
	int error;

	file = shmem_file_setup("[page-alloc-free]", PAGE_SIZE, EMPTY_VMA_FLAGS);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, file);
	mapping = file->f_mapping;

	folio = folio_alloc(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	folio_lock(folio);
	error = filemap_add_folio(mapping, folio, 0, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, 0, error);
	folio_unlock(folio);
	KUNIT_ASSERT_PTR_EQ(test, mapping, folio->mapping);
	lru_add_drain_all();

	folio_lock(folio);
	filemap_remove_folio(folio);
	folio_unlock(folio);
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));
	folio_put(folio);
	fput(file);
}

static int page_alloc_free_test_worker(void *data)
{
	struct page_alloc_free_test_worker *worker = data;
	unsigned int index;

	for (index = 0; index < PAGE_ALLOC_FREE_TEST_ITERATIONS; index++) {
		struct page *page = alloc_pages(GFP_KERNEL, index & 1);

		if (!page) {
			atomic_inc(&worker->errors);
			continue;
		}
		__free_pages(page, index & 1);
	}

	complete(&worker->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void page_alloc_free_test_concurrent(struct kunit *test)
{
	struct page_alloc_free_test_worker workers[PAGE_ALLOC_FREE_TEST_WORKERS] = {};
	struct task_struct *tasks[PAGE_ALLOC_FREE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;

	for (index = 0; index < PAGE_ALLOC_FREE_TEST_WORKERS; index++) {
		init_completion(&workers[index].done);
		tasks[index] = kthread_run(page_alloc_free_test_worker,
					   &workers[index],
					   "page-alloc-free-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < PAGE_ALLOC_FREE_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&workers[index].done,
						 PAGE_ALLOC_FREE_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < PAGE_ALLOC_FREE_TEST_WORKERS; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&workers[index].errors));
	}
	KUNIT_EXPECT_FALSE(test, timed_out);
}

static struct kunit_case page_alloc_free_test_cases[] = {
#ifdef CONFIG_RUST_PAGE_ALLOC_FREE
	KUNIT_CASE(page_alloc_free_test_failure_alloc),
	KUNIT_CASE(page_alloc_free_test_failure_refcount),
#endif
	KUNIT_CASE(page_alloc_free_test_order_zero),
	KUNIT_CASE(page_alloc_free_test_noncompound_high_order),
	KUNIT_CASE(page_alloc_free_test_compound_folio),
	KUNIT_CASE(page_alloc_free_test_node_fallback),
	KUNIT_CASE(page_alloc_free_test_explicit_node),
	KUNIT_CASE(page_alloc_free_test_nolock),
	KUNIT_CASE(page_alloc_free_test_nonfinal_reference),
	KUNIT_CASE(page_alloc_free_test_noncompound_tail_release),
	KUNIT_CASE(page_alloc_free_test_address_free),
	KUNIT_CASE(page_alloc_free_test_zero_address),
	KUNIT_CASE(page_alloc_free_test_page_cache_roundtrip),
	KUNIT_CASE(page_alloc_free_test_concurrent),
	{}
};

static struct kunit_suite page_alloc_free_test_suite = {
	.name = "page_alloc_free",
	.test_cases = page_alloc_free_test_cases,
};

kunit_test_suite(page_alloc_free_test_suite);

MODULE_DESCRIPTION("KUnit tests for page allocation and free policy");
MODULE_LICENSE("GPL");
