/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_PREPARE_COMPLETE_RUST_H
#define _LINUX_VMA_PREPARE_COMPLETE_RUST_H

#include <linux/types.h>
#include <linux/vma-merge-commit-rust.h>

struct address_space;
struct anon_vma;
struct file;
struct mm_struct;
struct vma_iterator;
struct vm_area_struct;

#ifdef CONFIG_RUST_VMA_PREPARE_COMPLETE

void rust_vma_init_prepare(struct vma_prepare *vp,
			   struct vm_area_struct *vma,
			   struct vma_merge_struct *vmg);
void rust_vma_prepare(struct vma_prepare *vp);
void rust_vma_complete(struct vma_prepare *vp, struct vma_iterator *vmi,
		       struct mm_struct *mm);

bool rust_helper_vma_init_prepare_remove_middle(struct vma_merge_struct *vmg);
bool rust_helper_vma_init_prepare_remove_next(struct vma_merge_struct *vmg);
bool rust_helper_vma_init_prepare_adjust_middle(struct vma_merge_struct *vmg);
bool rust_helper_vma_init_prepare_adjust_next(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_init_prepare_middle(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_init_prepare_next(struct vma_merge_struct *vmg);
struct file *rust_helper_vma_init_prepare_vma_file(struct vm_area_struct *vma);
struct address_space *
rust_helper_vma_init_prepare_vma_mapping(struct vm_area_struct *vma);
void rust_helper_vma_init_prepare_warn_anon(struct vma_prepare *vp);
bool rust_helper_vma_init_prepare_skip_uprobe(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_prepare_vma_start(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_prepare_vma_end(struct vm_area_struct *vma);
void rust_helper_vma_prepare_uprobe_munmap(struct vm_area_struct *vma,
					   unsigned long start,
					   unsigned long end);
void rust_helper_vma_prepare_immap_lock_write(struct address_space *mapping);
void rust_helper_vma_prepare_link_file(struct vm_area_struct *vma,
				       struct address_space *mapping);
void rust_helper_vma_prepare_anon_lock_write(struct anon_vma *anon_vma);
void rust_helper_vma_prepare_anon_rmap_pre_update(struct vm_area_struct *vma);
void rust_helper_vma_prepare_dcache_lock(struct address_space *mapping);
void rust_helper_vma_prepare_mapping_remove(struct vm_area_struct *vma,
					    struct address_space *mapping);

void rust_helper_vma_prepare_mapping_insert(struct vm_area_struct *vma,
					    struct address_space *mapping);
void rust_helper_vma_prepare_dcache_unlock(struct address_space *mapping);
void rust_helper_vma_prepare_remove_shared(struct vm_area_struct *vma,
					   struct address_space *mapping);
void rust_helper_vma_prepare_iter_store(struct vma_iterator *vmi,
					struct vm_area_struct *vma);
void rust_helper_vma_prepare_inc_map_count(struct mm_struct *mm);
void rust_helper_vma_prepare_anon_rmap_post_update(struct vm_area_struct *vma);
void rust_helper_vma_prepare_anon_unlock_write(struct anon_vma *anon_vma);
void rust_helper_vma_prepare_immap_unlock_write(struct address_space *mapping);
bool rust_helper_vma_prepare_skip_uprobe(struct vma_prepare *vp);
void rust_helper_vma_prepare_uprobe_mmap(struct vm_area_struct *vma);
void rust_helper_vma_prepare_mark_detached(struct vm_area_struct *vma);
void rust_helper_vma_prepare_fput(struct file *file);
void rust_helper_vma_prepare_unlink_anon(struct vm_area_struct *vma);
void rust_helper_vma_prepare_dec_map_count(struct mm_struct *mm);
void rust_helper_vma_prepare_policy_put(struct vm_area_struct *vma);
void rust_helper_vma_prepare_warn_remove_end(struct vm_area_struct *vma,
					     struct vm_area_struct *remove);
void rust_helper_vma_prepare_free_vma(struct vm_area_struct *vma);

#endif /* CONFIG_RUST_VMA_PREPARE_COMPLETE */

#endif /* _LINUX_VMA_PREPARE_COMPLETE_RUST_H */
