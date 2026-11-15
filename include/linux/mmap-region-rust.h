/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_RUST_H
#define _LINUX_MMAP_REGION_RUST_H

#include <linux/types.h>

struct rust_mmap_region_state;

unsigned long rust_mmap_region(struct rust_mmap_region_state *state);

#ifdef CONFIG_RUST_MMAP_REGION

void
rust_helper_mmap_region_assert_write_locked(struct rust_mmap_region_state *state);
bool
rust_helper_mmap_region_deny_write_exec(struct rust_mmap_region_state *state);
bool rust_helper_mmap_region_arch_valid(struct rust_mmap_region_state *state);
bool
rust_helper_mmap_region_shared_maywrite(struct rust_mmap_region_state *state);
int
rust_helper_mmap_region_map_writable(struct rust_mmap_region_state *state);
void
rust_helper_mmap_region_unmap_writable(struct rust_mmap_region_state *state);
void rust_helper_mmap_region_create(struct rust_mmap_region_state *state);
unsigned long
rust_helper_mmap_region_ret(struct rust_mmap_region_state *state);
void
rust_helper_mmap_region_validate_mm(struct rust_mmap_region_state *state);

#endif /* CONFIG_RUST_MMAP_REGION */

#endif /* _LINUX_MMAP_REGION_RUST_H */
