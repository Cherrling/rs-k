/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_PREPARE_RUST_H
#define _LINUX_MMAP_REGION_PREPARE_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;

int
rust_mmap_region_prepare(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_PREPARE

int
rust_helper_mmap_prepare_call_file(struct rust_mmap_create_state *state);
int
rust_helper_mmap_prepare_call_action(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_prepare_file_changed(struct rust_mmap_create_state *state);
void
rust_helper_mmap_prepare_adopt_file(struct rust_mmap_create_state *state);
void
rust_helper_mmap_prepare_sync_fields(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_PREPARE */

#endif /* _LINUX_MMAP_REGION_PREPARE_RUST_H */
