/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MMAP_REGION_NEW_VMA_RUST_H
#define _LINUX_MMAP_REGION_NEW_VMA_RUST_H

#include <linux/types.h>

struct rust_mmap_create_state;
struct vm_area_struct;

int
rust_mmap_region_new_vma(struct rust_mmap_create_state *state);

#ifdef CONFIG_RUST_MMAP_REGION_NEW_VMA

bool
rust_helper_mmap_new_vma_is_anon(struct rust_mmap_create_state *state);
struct vm_area_struct *
rust_helper_mmap_new_vma_alloc(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_init(struct rust_mmap_create_state *state,
			      bool is_anon);
int
rust_helper_mmap_new_vma_prealloc(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_new_vma_has_file(struct rust_mmap_create_state *state);
int
rust_helper_mmap_new_vma_map_file(struct rust_mmap_create_state *state);
int
rust_helper_mmap_new_vma_setup_shmem(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_update_ksm(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_arch_validate(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_install(struct rust_mmap_create_state *state);
bool
rust_helper_mmap_new_vma_is_anonymous(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_enter_khugepaged(struct rust_mmap_create_state *s);
void
rust_helper_mmap_new_vma_free_iter(struct rust_mmap_create_state *state);
void
rust_helper_mmap_new_vma_free(struct rust_mmap_create_state *state);

#endif /* CONFIG_RUST_MMAP_REGION_NEW_VMA */

#endif /* _LINUX_MMAP_REGION_NEW_VMA_RUST_H */
