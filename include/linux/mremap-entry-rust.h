/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MREMAP_ENTRY_RUST_H
#define _LINUX_MREMAP_ENTRY_RUST_H

#include <linux/types.h>

struct rust_mremap_entry_state;

#ifdef CONFIG_RUST_MREMAP_ENTRY

unsigned long rust_mremap_entry(struct rust_mremap_entry_state *state);

void rust_helper_mremap_entry_align_lengths(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_unsupported_flags(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_addr_aligned(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_new_len_invalid(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_implies_new_addr(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_new_addr_invalid(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_has_maymove(struct rust_mremap_entry_state *state);
bool
rust_helper_mremap_entry_dontunmap_resize_invalid(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_overlaps(struct rust_mremap_entry_state *state);
int
rust_helper_mremap_entry_acquire_mmap_write_lock(struct rust_mremap_entry_state *state);
bool
rust_helper_mremap_entry_map_count_allows_split(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_move_only(struct rust_mremap_entry_state *state);
unsigned long
rust_helper_mremap_entry_remap_move(struct rust_mremap_entry_state *state);
void rust_helper_mremap_entry_lookup_source(struct rust_mremap_entry_state *state);
unsigned long
rust_helper_mremap_entry_check_prepare(struct rust_mremap_entry_state *state);
unsigned long
rust_helper_mremap_entry_mremap_to(struct rust_mremap_entry_state *state);
unsigned long
rust_helper_mremap_entry_mremap_at(struct rust_mremap_entry_state *state);
bool rust_helper_mremap_entry_is_error_value(unsigned long value);
bool rust_helper_mremap_entry_mmap_locked(struct rust_mremap_entry_state *state);
void rust_helper_mremap_entry_unlock(struct rust_mremap_entry_state *state);
bool
rust_helper_mremap_entry_needs_populate(struct rust_mremap_entry_state *state);
void rust_helper_mremap_entry_populate(struct rust_mremap_entry_state *state);
void rust_helper_mremap_entry_notify_uffd(struct rust_mremap_entry_state *state,
					  bool failed);

#endif /* CONFIG_RUST_MREMAP_ENTRY */

#endif /* _LINUX_MREMAP_ENTRY_RUST_H */
