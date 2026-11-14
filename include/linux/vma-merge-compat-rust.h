/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VMA_MERGE_COMPAT_RUST_H
#define _LINUX_VMA_MERGE_COMPAT_RUST_H

#include <linux/types.h>

struct anon_vma;
struct file;
struct vma_merge_struct;
struct vm_area_struct;

#ifdef CONFIG_RUST_VMA_MERGE_COMPAT

bool rust_vma_merge_compat_can_merge_left(struct vma_merge_struct *vmg);
bool rust_vma_merge_compat_can_merge_right(struct vma_merge_struct *vmg,
					   bool can_merge_left);

struct vm_area_struct *
rust_helper_vma_merge_compat_vmg_prev(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_merge_compat_vmg_next(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_merge_compat_vmg_middle(struct vma_merge_struct *vmg);
struct vm_area_struct *
rust_helper_vma_merge_compat_vmg_copied_from(struct vma_merge_struct *vmg);
bool rust_helper_vma_merge_compat_mempolicy_equal(struct vma_merge_struct *vmg,
						  struct vm_area_struct *vma);
bool rust_helper_vma_merge_compat_flags_compatible(struct vma_merge_struct *vmg,
						   struct vm_area_struct *vma);
struct file *
rust_helper_vma_merge_compat_vmg_file(struct vma_merge_struct *vmg);
struct file *
rust_helper_vma_merge_compat_vma_file(struct vm_area_struct *vma);
bool rust_helper_vma_merge_compat_userfaultfd_compatible(struct vma_merge_struct *vmg,
							 struct vm_area_struct *vma);
bool rust_helper_vma_merge_compat_anon_name_equal(struct vma_merge_struct *vmg,
						  struct vm_area_struct *vma);
bool rust_helper_vma_merge_compat_vma_has_anon_vma(struct vm_area_struct *vma);
bool rust_helper_vma_merge_compat_anon_vma_chain_is_singular(struct vm_area_struct *vma);
struct anon_vma *
rust_helper_vma_merge_compat_vmg_anon_vma(struct vma_merge_struct *vmg);
struct anon_vma *
rust_helper_vma_merge_compat_vma_anon_vma(struct vm_area_struct *vma);
void
rust_helper_vma_merge_compat_validate_anon_vma(struct vma_merge_struct *vmg);
bool
rust_helper_vma_merge_compat_vmg_is_cow_mapping(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_start(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_end(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_start_pgoff(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_end_pgoff(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_start_anon_pgoff(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vmg_end_anon_pgoff(struct vma_merge_struct *vmg);
unsigned long
rust_helper_vma_merge_compat_vma_start(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_merge_compat_vma_end(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_merge_compat_vma_start_pgoff(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_merge_compat_vma_end_pgoff(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_merge_compat_vma_start_anon_pgoff(struct vm_area_struct *vma);
unsigned long
rust_helper_vma_merge_compat_vma_end_anon_pgoff(struct vm_area_struct *vma);

#endif /* CONFIG_RUST_VMA_MERGE_COMPAT */

#endif /* _LINUX_VMA_MERGE_COMPAT_RUST_H */
