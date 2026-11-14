/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MODIFY_RUST_H
#define _LINUX_VMA_MODIFY_RUST_H

#include <linux/types.h>

struct vm_area_struct;
struct vma_merge_struct;

#ifdef CONFIG_RUST_VMA_MODIFY

struct vm_area_struct *rust_vma_modify(struct vma_merge_struct *vmg);

struct vm_area_struct *
rust_helper_vma_modify_middle(struct vma_merge_struct *vmg);
unsigned long rust_helper_vma_modify_start(struct vma_merge_struct *vmg);
unsigned long rust_helper_vma_modify_end(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_modify_merge(struct vma_merge_struct *vmg);
int rust_helper_vma_modify_backup_error(struct vma_merge_struct *vmg);
void
rust_helper_vma_modify_warn_if_give_up_on_oom(struct vma_merge_struct *vmg);
unsigned long rust_helper_vma_modify_vma_start(struct vm_area_struct *vma);
unsigned long rust_helper_vma_modify_vma_end(struct vm_area_struct *vma);
int rust_helper_vma_modify_split(struct vma_merge_struct *vmg,
				 struct vm_area_struct *vma,
				 unsigned long addr, int new_below);
struct vm_area_struct *rust_helper_vma_modify_error(int err);

#endif /* CONFIG_RUST_VMA_MODIFY */

#endif /* _LINUX_VMA_MODIFY_RUST_H */
