/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MOVE_PGT_ENTRY_RUST_H
#define _LINUX_MOVE_PGT_ENTRY_RUST_H

#include <linux/types.h>

struct pagetable_move_control;
struct rust_move_pgt_entry_state;

enum pgt_entry {
	NORMAL_PMD,
	HPAGE_PMD,
	NORMAL_PUD,
	HPAGE_PUD,
};

#ifdef CONFIG_RUST_MOVE_PGT_ENTRY

bool rust_move_pgt_entry(struct rust_move_pgt_entry_state *state,
			 enum pgt_entry entry, void *old_entry, void *new_entry);

bool rust_helper_move_pgt_entry_need_rmap_locks(struct rust_move_pgt_entry_state *state);
void rust_helper_move_pgt_entry_take_rmap_locks(struct rust_move_pgt_entry_state *state);
void rust_helper_move_pgt_entry_drop_rmap_locks(struct rust_move_pgt_entry_state *state);
bool rust_helper_move_pgt_entry_normal_pmd(struct rust_move_pgt_entry_state *state,
					   void *old_entry, void *new_entry);
bool rust_helper_move_pgt_entry_normal_pud(struct rust_move_pgt_entry_state *state,
					   void *old_entry, void *new_entry);
bool rust_helper_move_pgt_entry_huge_pmd(struct rust_move_pgt_entry_state *state,
					 void *old_entry, void *new_entry);
bool rust_helper_move_pgt_entry_huge_pud(struct rust_move_pgt_entry_state *state,
					 void *old_entry, void *new_entry);
void rust_helper_move_pgt_entry_warn_invalid(void);

#endif /* CONFIG_RUST_MOVE_PGT_ENTRY */

#endif /* _LINUX_MOVE_PGT_ENTRY_RUST_H */
