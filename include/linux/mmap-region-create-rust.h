/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_CREATE_RUST_H
#define _LINUX_MMAP_REGION_CREATE_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;

unsigned long
rust_mmap_region_create(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_CREATE

int
rust_helper_mmap_create_setup(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_have_prepare(struct rust_mmap_create_state *state);
int
rust_helper_mmap_create_prepare(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_check_ksm_early(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_update_ksm_flags(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_has_adjacent(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_try_merge(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_has_vma(struct rust_mmap_create_state *state);
int
rust_helper_mmap_create_new_vma(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_set_user_fields(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_complete(struct rust_mmap_create_state *state);
int
rust_helper_mmap_create_action_complete(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_charged(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_unaccount(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_create_file_changed(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_put_changed_file(struct rust_mmap_create_state *state);
void
rust_helper_mmap_create_abort_munmap(struct rust_mmap_create_state *state);
unsigned long
rust_helper_mmap_create_addr(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_CREATE */

#endif /* _LINUX_MMAP_REGION_CREATE_RUST_H */
