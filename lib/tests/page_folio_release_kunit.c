// SPDX-License-Identifier: GPL-2.0-only

#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/file.h>
#include <linux/folio_batch.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/huge_mm.h>
#include <linux/kthread.h>
#include <linux/page_ref.h>
#include <linux/page-folio-release-rust.h>
#include <linux/sched.h>
#include <linux/shmem_fs.h>
#include <linux/swap.h>

#define PAGE_FOLIO_RELEASE_TEST_TIMEOUT		(5 * HZ)
#define PAGE_FOLIO_RELEASE_TEST_WORKERS		4
#define PAGE_FOLIO_RELEASE_TEST_ITERATIONS	64
#define PAGE_FOLIO_RELEASE_TEST_BATCH_COUNT	40

struct page_folio_release_test_worker {
	struct completion done;
	atomic_t errors;
};

static void page_folio_release_test_final_order_zero(struct kunit *test)
{
	struct folio *folio;
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	folio = page_folio(page);
	KUNIT_EXPECT_EQ(test, 0, folio_order(folio));
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));
	folio_put(folio);
}

static void page_folio_release_test_final_order_one(struct kunit *test)
{
	struct folio *folio;

	folio = folio_alloc(GFP_KERNEL, 1);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	KUNIT_EXPECT_EQ(test, 1, folio_order(folio));
	KUNIT_EXPECT_EQ(test, 2, folio_nr_pages(folio));
	folio_put(folio);
}

static void page_folio_release_test_nonfinal_reference(struct kunit *test)
{
	struct folio_batch fbatch;
	struct folio *folio;
	struct page *page;
	unsigned int refs;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	folio = page_folio(page);
	folio_get(folio);
	KUNIT_ASSERT_EQ(test, 2, folio_ref_count(folio));

	folio_batch_init(&fbatch);
	folio_batch_add(&fbatch, folio);
	refs = 1;
	folios_put_refs(&fbatch, &refs);
	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));

	folio_put(folio);
}

static void page_folio_release_test_batch_refs(struct kunit *test)
{
	struct folio_batch fbatch;
	struct folio *folios[2];
	struct page *pages[2];
	unsigned int refs[2] = { 1, 1 };

	pages[0] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[0]);
	pages[1] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[1]);
	folios[0] = page_folio(pages[0]);
	folios[1] = page_folio(pages[1]);
	folio_get(folios[0]);

	folio_batch_init(&fbatch);
	folio_batch_add(&fbatch, folios[0]);
	folio_batch_add(&fbatch, folios[1]);
	folios_put_refs(&fbatch, refs);
	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folios[0]));

	folio_batch_add(&fbatch, folios[0]);
	folios_put_refs(&fbatch, NULL);
}

static void page_folio_release_test_empty_and_huge_zero(struct kunit *test)
{
	struct folio_batch fbatch;
	struct folio *huge_zero;
	int refs_before = -1;

	huge_zero = get_persistent_huge_zero_folio();
	if (huge_zero)
		refs_before = folio_ref_count(huge_zero);

	folio_batch_init(&fbatch);
	folio_batch_add(&fbatch, NULL);
	if (huge_zero)
		folio_batch_add(&fbatch, huge_zero);
	folios_put_refs(&fbatch, NULL);

	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));
	if (huge_zero)
		KUNIT_EXPECT_EQ(test, refs_before,
				folio_ref_count(huge_zero));
}

static void page_folio_release_test_release_pages(struct kunit *test)
{
	struct page *pages[2];

	pages[0] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[0]);
	pages[1] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[1]);
	release_pages(pages, ARRAY_SIZE(pages));
}

#ifdef CONFIG_RUST_PAGE_FOLIO_RELEASE
static void page_folio_release_test_failure_injection(struct kunit *test)
{
	struct folio *folio;
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	folio = page_folio(page);
	KUNIT_ASSERT_EQ(test, 1, folio_ref_count(folio));

	rust_page_folio_release_test_fail_next(
		RUST_PAGE_FOLIO_RELEASE_TEST_FAIL_PREPARE);
	release_pages(&page, 1);
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));
	folio_put(folio);
}
#endif

static void page_folio_release_test_release_folios(struct kunit *test)
{
	struct folio *folios[2];
	struct page *pages[2];

	pages[0] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[0]);
	pages[1] = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, pages[1]);
	folios[0] = page_folio(pages[0]);
	folios[1] = page_folio(pages[1]);
	release_pages(folios, ARRAY_SIZE(folios));
}

static void page_folio_release_test_release_encoded(struct kunit *test)
{
	struct encoded_page *encoded[2];
	struct page *page;

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	encoded[0] = encode_page(page, 0);
	encoded[1] = encode_page(alloc_pages(GFP_KERNEL, 0), 0);
	KUNIT_ASSERT_NOT_NULL(test, encoded_page_ptr(encoded[1]));
	release_pages(encoded, ARRAY_SIZE(encoded));
}

static void page_folio_release_test_encoded_nr_pages(struct kunit *test)
{
	struct encoded_page *encoded[2];
	struct folio *folio;

	folio = folio_alloc(GFP_KERNEL, 1);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	folio_ref_add(folio, 1);
	KUNIT_ASSERT_EQ(test, 2, folio_ref_count(folio));
	encoded[0] = encode_page(&folio->page, ENCODED_PAGE_BIT_NR_PAGES_NEXT);
	encoded[1] = encode_nr_pages(folio_nr_pages(folio));
	release_pages(encoded, ARRAY_SIZE(encoded));
}

static void page_folio_release_test_batch_boundary(struct kunit *test)
{
	struct folio_batch fbatch;
	struct folio *folios[PAGE_FOLIO_RELEASE_TEST_BATCH_COUNT];
	unsigned int index;

	for (index = 0; index < ARRAY_SIZE(folios); index++) {
		struct page *page = alloc_pages(GFP_KERNEL, 0);

		KUNIT_ASSERT_NOT_NULL(test, page);
		folios[index] = page_folio(page);
	}

	folio_batch_init(&fbatch);
	for (index = 0; index < ARRAY_SIZE(folios); index++) {
		folio_batch_add(&fbatch, folios[index]);
		if (folio_batch_count(&fbatch) == FOLIO_BATCH_SIZE)
			folios_put_refs(&fbatch, NULL);
	}
	if (folio_batch_count(&fbatch))
		folios_put_refs(&fbatch, NULL);
	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));
}

static void page_folio_release_test_batch_release_drain(struct kunit *test)
{
	struct folio_batch fbatch;
	struct folio *folio;
	struct page *page;

	folio_batch_init(&fbatch);
	KUNIT_EXPECT_FALSE(test, fbatch.percpu_pvec_drained);

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	folio = page_folio(page);
	folio_batch_add(&fbatch, folio);
	__folio_batch_release(&fbatch);
	KUNIT_EXPECT_TRUE(test, fbatch.percpu_pvec_drained);
	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));

	page = alloc_pages(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, page);
	folio = page_folio(page);
	folio_batch_add(&fbatch, folio);
	__folio_batch_release(&fbatch);
	KUNIT_EXPECT_TRUE(test, fbatch.percpu_pvec_drained);
	KUNIT_EXPECT_EQ(test, 0, folio_batch_count(&fbatch));
}

static void page_folio_release_test_page_cache_release(struct kunit *test)
{
	struct address_space *mapping;
	struct folio *folio;
	struct file *file;
	int error;

	file = shmem_file_setup("[folio-release]", PAGE_SIZE, EMPTY_VMA_FLAGS);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, file);
	mapping = file->f_mapping;

	folio = folio_alloc(GFP_KERNEL, 0);
	KUNIT_ASSERT_NOT_NULL(test, folio);
	folio_lock(folio);
	error = filemap_add_folio(mapping, folio, 0, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, 0, error);
	folio_unlock(folio);
	KUNIT_EXPECT_PTR_EQ(test, mapping, folio->mapping);
	lru_add_drain_all();

	folio_lock(folio);
	filemap_remove_folio(folio);
	folio_unlock(folio);
	KUNIT_EXPECT_EQ(test, 1, folio_ref_count(folio));
	folio_put(folio);
	fput(file);
}

static int page_folio_release_test_worker(void *data)
{
	struct page_folio_release_test_worker *worker = data;
	unsigned int index;

	for (index = 0; index < PAGE_FOLIO_RELEASE_TEST_ITERATIONS; index++) {
		struct page *page = alloc_pages(GFP_KERNEL, 0);

		if (!page) {
			atomic_inc(&worker->errors);
			continue;
		}
		folio_put(page_folio(page));
	}

	complete(&worker->done);
	while (!kthread_should_stop())
		schedule();
	return 0;
}

static void page_folio_release_test_concurrent(struct kunit *test)
{
	struct page_folio_release_test_worker workers[PAGE_FOLIO_RELEASE_TEST_WORKERS] = {};
	struct task_struct *tasks[PAGE_FOLIO_RELEASE_TEST_WORKERS] = {};
	unsigned int index;
	bool timed_out = false;

	for (index = 0; index < PAGE_FOLIO_RELEASE_TEST_WORKERS; index++) {
		init_completion(&workers[index].done);
		tasks[index] = kthread_run(page_folio_release_test_worker,
					   &workers[index],
					   "folio-release-%u", index);
		KUNIT_ASSERT_FALSE(test, IS_ERR_OR_NULL(tasks[index]));
		get_task_struct(tasks[index]);
	}

	for (index = 0; index < PAGE_FOLIO_RELEASE_TEST_WORKERS; index++) {
		if (!wait_for_completion_timeout(&workers[index].done,
						 PAGE_FOLIO_RELEASE_TEST_TIMEOUT))
			timed_out = true;
	}
	for (index = 0; index < PAGE_FOLIO_RELEASE_TEST_WORKERS; index++) {
		KUNIT_EXPECT_EQ(test, 0, kthread_stop_put(tasks[index]));
		KUNIT_EXPECT_EQ(test, 0, atomic_read(&workers[index].errors));
	}
	KUNIT_EXPECT_FALSE(test, timed_out);
}

static struct kunit_case page_folio_release_test_cases[] = {
#ifdef CONFIG_RUST_PAGE_FOLIO_RELEASE
	KUNIT_CASE(page_folio_release_test_failure_injection),
#endif
	KUNIT_CASE(page_folio_release_test_final_order_zero),
	KUNIT_CASE(page_folio_release_test_final_order_one),
	KUNIT_CASE(page_folio_release_test_nonfinal_reference),
	KUNIT_CASE(page_folio_release_test_batch_refs),
	KUNIT_CASE(page_folio_release_test_empty_and_huge_zero),
	KUNIT_CASE(page_folio_release_test_release_pages),
	KUNIT_CASE(page_folio_release_test_release_folios),
	KUNIT_CASE(page_folio_release_test_release_encoded),
	KUNIT_CASE(page_folio_release_test_encoded_nr_pages),
	KUNIT_CASE(page_folio_release_test_batch_boundary),
	KUNIT_CASE(page_folio_release_test_batch_release_drain),
	KUNIT_CASE(page_folio_release_test_page_cache_release),
	KUNIT_CASE(page_folio_release_test_concurrent),
	{}
};

static struct kunit_suite page_folio_release_test_suite = {
	.name = "page_folio_release",
	.test_cases = page_folio_release_test_cases,
};

kunit_test_suite(page_folio_release_test_suite);

MODULE_DESCRIPTION("KUnit tests for page and folio release");
MODULE_LICENSE("GPL");
