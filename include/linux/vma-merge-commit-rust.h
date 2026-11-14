/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MERGE_COMMIT_RUST_H
#define _LINUX_VMA_MERGE_COMMIT_RUST_H

#include <linux/types.h>

struct anon_vma;
struct address_space;
struct file;
struct vm_area_struct;
struct vma_merge_struct;

/*
 * VMA lock generalization.
 *
 * Rust owns this object while CONFIG_RUST_VMA_MERGE_COMMIT is enabled. C owns
 * the object when the original commit path is selected.
 */
struct vma_prepare {
	struct vm_area_struct *vma;
	struct vm_area_struct *adj_next;
	struct file *file;
	struct address_space *mapping;
	struct anon_vma *anon_vma;
	struct vm_area_struct *insert;
	struct vm_area_struct *remove;
	struct vm_area_struct *remove2;

	bool skip_vma_uprobe :1;
};

#ifdef CONFIG_RUST_VMA_MERGE_COMMIT

int rust_vma_merge_commit(struct vma_merge_struct *vmg);

bool rust_helper_vma_merge_commit_adjust_next_start(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_merge_commit_middle(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_merge_commit_target(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_commit_config_next(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_commit_config_range(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_commit_init_prepare(struct vma_prepare *vp,
					       struct vm_area_struct *vma,
					       struct vma_merge_struct *vmg);
int rust_helper_vma_merge_commit_prealloc(struct vma_merge_struct *vmg,
					  struct vm_area_struct *vma);
void rust_helper_vma_merge_commit_prepare(struct vma_prepare *vp);
void rust_helper_vma_merge_commit_adjust_huge(struct vma_merge_struct *vmg,
					      struct vm_area_struct *vma);
void rust_helper_vma_merge_commit_set_range(struct vma_merge_struct *vmg,
					    struct vm_area_struct *vma);
void rust_helper_vma_merge_commit_adjust_set_range(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_commit_store(struct vma_merge_struct *vmg);
void rust_helper_vma_merge_commit_complete(struct vma_prepare *vp,
					   struct vma_merge_struct *vmg,
					   struct vm_area_struct *vma);

#endif /* CONFIG_RUST_VMA_MERGE_COMMIT */

#endif /* _LINUX_VMA_MERGE_COMMIT_RUST_H */
