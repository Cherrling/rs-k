/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_SETUP_RUST_H
#define _LINUX_MMAP_REGION_SETUP_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;

int
rust_mmap_region_setup(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_SETUP

bool
rust_helper_mmap_setup_find_overlap(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_init_unmap(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_init_detach(struct rust_mmap_create_state *state);
int
rust_helper_mmap_setup_gather(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_clear_unmap_pages(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_adopt_unmap_neighbors(struct rust_mmap_create_state *s);
void
rust_helper_mmap_setup_select_adjacent(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_setup_may_expand(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_setup_accountable(struct rust_mmap_create_state *state);
unsigned long
rust_helper_mmap_setup_uncharged(struct rust_mmap_create_state *state);
int
rust_helper_mmap_setup_charge(struct rust_mmap_create_state *state,
			      unsigned long pages);
void
rust_helper_mmap_setup_commit_accounting(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_clean_area(struct rust_mmap_create_state *state);
void
rust_helper_mmap_setup_sync_desc(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_SETUP */

#endif /* _LINUX_MMAP_REGION_SETUP_RUST_H */
