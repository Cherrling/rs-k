/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_COMPLETE_RUST_H
#define _LINUX_MMAP_REGION_COMPLETE_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;

void
rust_mmap_region_complete(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_COMPLETE

void
rust_helper_mmap_complete_perf_event(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_unmap_overlap(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_account_stats(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_complete_is_locked(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_complete_supports_mlock(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_account_locked(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_clear_locked(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_complete_has_file(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_uprobe(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_complete_supports_soft_dirty(void);
void
rust_helper_mmap_complete_set_soft_dirty(struct rust_mmap_create_state *state);
void
rust_helper_mmap_complete_set_page_prot(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_COMPLETE */

#endif /* _LINUX_MMAP_REGION_COMPLETE_RUST_H */
