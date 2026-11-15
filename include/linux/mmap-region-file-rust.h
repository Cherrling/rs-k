/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_FILE_RUST_H
#define _LINUX_MMAP_REGION_FILE_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;

int
rust_mmap_region_file(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_FILE

bool
rust_helper_mmap_file_needs_get(struct rust_mmap_create_state *state);
void
rust_helper_mmap_file_set(struct rust_mmap_create_state *state);
void
rust_helper_mmap_file_get(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_file_has_callback(struct rust_mmap_create_state *state);
int
rust_helper_mmap_file_map(struct rust_mmap_create_state *state);
void
rust_helper_mmap_file_put(struct rust_mmap_create_state *state);
void
rust_helper_mmap_file_unmap_partial(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_file_address_changed(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_file_adds_write(struct rust_mmap_create_state *s);
void
rust_helper_mmap_file_prepare_unmap(struct rust_mmap_create_state *state);
void
rust_helper_mmap_file_warn_addr(struct rust_mmap_create_state *s);
void
rust_helper_mmap_file_warn_write(struct rust_mmap_create_state *s);
void
rust_helper_mmap_file_sync_state(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_FILE */

#endif /* _LINUX_MMAP_REGION_FILE_RUST_H */
