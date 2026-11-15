/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MOVE_PAGE_TABLES_RUST_H
#define _LINUX_MOVE_PAGE_TABLES_RUST_H

#include <linux/move-pgt-entry-rust.h>
#include <linux/types.h>

struct rust_move_page_tables_state;

#ifdef CONFIG_RUST_MOVE_PAGE_TABLES

unsigned long rust_move_page_tables(struct rust_move_page_tables_state *state);

unsigned long rust_helper_move_page_tables_len(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_is_hugetlb(struct rust_move_page_tables_state *state);
unsigned long
rust_helper_move_page_tables_move_hugetlb(struct rust_move_page_tables_state *state);
void rust_helper_move_page_tables_realign_pmd(struct rust_move_page_tables_state *state);
void rust_helper_move_page_tables_start(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_done(struct rust_move_page_tables_state *state);
void rust_helper_move_page_tables_next(struct rust_move_page_tables_state *state,
				       unsigned long extent);
unsigned long rust_helper_move_page_tables_old_addr(struct rust_move_page_tables_state *state);
unsigned long rust_helper_move_page_tables_old_end(struct rust_move_page_tables_state *state);
unsigned long rust_helper_move_page_tables_new_addr(struct rust_move_page_tables_state *state);
unsigned long rust_helper_move_page_tables_pmd_size(void);
unsigned long rust_helper_move_page_tables_pmd_mask(void);
unsigned long rust_helper_move_page_tables_pud_size(void);
unsigned long rust_helper_move_page_tables_pud_mask(void);
unsigned long rust_helper_move_page_tables_hpage_pmd_size(void);
unsigned long rust_helper_move_page_tables_hpage_pud_size(void);
void rust_helper_move_page_tables_cond_resched(void);
bool rust_helper_move_page_tables_get_old_pud(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_alloc_new_pud(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_pud_trans_huge(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_can_move_pud(void);
bool rust_helper_move_page_tables_get_old_pmd(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_alloc_new_pmd(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_pmd_is_huge(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_can_move_pmd(void);
bool rust_helper_move_page_tables_move_entry(struct rust_move_page_tables_state *state,
					     enum pgt_entry entry);
void
rust_helper_move_page_tables_split_huge_pmd(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_pmd_none(struct rust_move_page_tables_state *state);
bool rust_helper_move_page_tables_pte_alloc(struct rust_move_page_tables_state *state);
int rust_helper_move_page_tables_move_ptes(struct rust_move_page_tables_state *state,
					   unsigned long extent);
void rust_helper_move_page_tables_end(struct rust_move_page_tables_state *state);
void rust_helper_move_page_tables_warn_invalid(void);

#endif /* CONFIG_RUST_MOVE_PAGE_TABLES */

#endif /* _LINUX_MOVE_PAGE_TABLES_RUST_H */
