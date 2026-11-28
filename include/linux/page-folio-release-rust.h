/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Internal helpers for the native Rust page and folio release lifecycle.
 */

#ifndef _LINUX_PAGE_FOLIO_RELEASE_RUST_H
#define _LINUX_PAGE_FOLIO_RELEASE_RUST_H

#include <linux/folio_batch.h>
#include <linux/mm_types.h>
#include <linux/types.h>

struct folio;
struct lruvec;
struct page;

#ifdef CONFIG_RUST_PAGE_FOLIO_RELEASE

bool rust_helper_page_folio_release_is_zone_device(const struct folio *folio);
bool rust_helper_page_folio_release_is_hugetlb(const struct folio *folio);
bool rust_helper_page_folio_release_is_huge_zero(const struct folio *folio);
bool rust_helper_page_folio_release_folio_ref_sub_and_test(struct folio *folio,
							   unsigned int refs);
void rust_helper_page_folio_release_zone_device(struct folio *folio);
void rust_helper_page_folio_release_hugetlb(struct folio *folio);
void rust_helper_page_folio_release_deferred(struct folio *folio);
void rust_helper_page_folio_release_cache_locked(struct folio *folio,
						 struct lruvec **lruvec,
						 unsigned long *flags);
void rust_helper_page_folio_release_unlock(struct lruvec **lruvec,
					   unsigned long *flags);
void rust_helper_page_folio_release_uncharge(const struct folio *folio);
void rust_helper_page_folio_release_uncharge_batch(const struct folio_batch *folios);
void rust_helper_page_folio_release_frozen(struct folio *folio);
void rust_helper_page_folio_release_unref_batch(const struct folio_batch *folios);
void rust_helper_page_folio_release_lru_drain(void);

struct folio *rust_helper_page_folio_release_page_folio(struct page *page);
struct page *rust_helper_page_folio_release_encoded_ptr(struct encoded_page *page);
unsigned long
rust_helper_page_folio_release_encoded_flags(struct encoded_page *page);
unsigned long
rust_helper_page_folio_release_encoded_nr_pages(struct encoded_page *page);

#ifdef CONFIG_PAGE_FOLIO_RELEASE_KUNIT_TEST
#define RUST_PAGE_FOLIO_RELEASE_TEST_FAIL_PREPARE	1

bool rust_helper_page_folio_release_test_fail(unsigned int stage);
void rust_page_folio_release_test_fail_next(unsigned int stage);
#endif

#endif /* CONFIG_RUST_PAGE_FOLIO_RELEASE */

#endif /* _LINUX_PAGE_FOLIO_RELEASE_RUST_H */
