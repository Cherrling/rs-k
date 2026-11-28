/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal helpers for the native Rust page allocation and free policy.
 */

#ifndef _LINUX_PAGE_ALLOC_FREE_RUST_H
#define _LINUX_PAGE_ALLOC_FREE_RUST_H

#include <linux/gfp_types.h>
#include <linux/nodemask_types.h>
#include <linux/types.h>

struct folio;
struct page;

#define RUST_PAGE_ALLOC_FREE_ALLOC_DEFAULT	0U
#define RUST_PAGE_ALLOC_FREE_ALLOC_NOLOCK	0x400U
#define RUST_PAGE_ALLOC_FREE_ALLOC_NO_CODETAG	0x1000U

#ifdef CONFIG_RUST_PAGE_ALLOC_FREE

struct page *__alloc_pages_noprof(gfp_t gfp, unsigned int order,
				  int preferred_nid, nodemask_t *nodemask,
				  unsigned int alloc_flags);
struct page *alloc_pages_node_noprof(int nid, gfp_t gfp_mask,
				     unsigned int order);
struct folio *__folio_alloc_noprof(gfp_t gfp, unsigned int order,
				   int preferred_nid, nodemask_t *nodemask);
void __free_pages(struct page *page, unsigned int order);
void free_pages_nolock(struct page *page, unsigned int order);
void free_pages(unsigned long addr, unsigned int order);
struct page *alloc_pages_nolock_noprof(gfp_t gfp_flags, int nid,
				       unsigned int order);

struct page *
rust_helper_page_alloc_free_frozen(gfp_t gfp, unsigned int order, int nid,
				   nodemask_t *nodemask,
				   unsigned int alloc_flags);
gfp_t rust_helper_page_alloc_free_compound_flags(gfp_t gfp);
void rust_helper_page_alloc_free_set_refcounted(struct page *page);
int rust_helper_page_alloc_free_numa_mem_id(void);
int rust_helper_page_alloc_free_numa_node_id(void);
void rust_helper_page_alloc_free_warn_if_node_offline(int nid, gfp_t gfp);
struct folio *rust_helper_page_alloc_free_rmappable_folio(struct page *page);

bool rust_helper_page_alloc_free_page_head(struct page *page);
unsigned long rust_helper_page_alloc_free_tag_get(struct page *page);
bool rust_helper_page_alloc_free_put_page_testzero(struct page *page);
void rust_helper_page_alloc_free_tag_sub_pages(unsigned long tag,
					       unsigned int nr_pages);
void rust_helper_page_alloc_free_clear_tail_tag(struct page *page);
void rust_helper_page_alloc_free_frozen_page(struct page *page,
					     unsigned int order);
void rust_helper_page_alloc_free_frozen_page_nolock(struct page *page,
						    unsigned int order);
struct page *rust_helper_page_alloc_free_virt_to_page(unsigned long addr);

#ifdef CONFIG_PAGE_ALLOC_FREE_KUNIT_TEST
#define RUST_PAGE_ALLOC_FREE_TEST_FAIL_ALLOC		1
#define RUST_PAGE_ALLOC_FREE_TEST_FAIL_REFCOUNT		2

bool rust_helper_page_alloc_free_test_fail(unsigned int stage);
void rust_page_alloc_free_test_fail_next(unsigned int stage);
unsigned int rust_page_alloc_free_test_failure_count(void);
#endif

#endif /* CONFIG_RUST_PAGE_ALLOC_FREE */

#endif /* _LINUX_PAGE_ALLOC_FREE_RUST_H */
